#!/usr/bin/env python3
"""ADDITION local EVM JSON-RPC surface (bootstrap → wallet-facing).

Honest labels only:
- Bind is loopback-only (127.0.0.1:9545 by default). Refuses 0.0.0.0.
- chainId / net_version = 424242 (ADDITION local testnet profile).
- Not Ethereum mainnet. Not a live Uniswap / ETH / XMR bridge.
- eth_sendRawTransaction stays disabled: no native EVM execution mapping.
- Reads native TEXT RPC on loopback (getinfo / getblock / getbalance / …).
"""

from __future__ import annotations

import json
import os
import socket
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from ipaddress import ip_address
from typing import Any

CHAIN_ID = 424242
LISTEN_HOST = os.environ.get("ADDITION_EVM_BIND", "127.0.0.1")
LISTEN_PORT = int(os.environ.get("ADDITION_EVM_PORT", "9545"))
NODE_HOST = os.environ.get("ADDITION_LOCAL_RPC_HOST", "127.0.0.1")
NODE_PORT = int(os.environ.get("ADDITION_LOCAL_RPC_PORT", "8545"))
CLIENT_VERSION = "addition-evm-bridge/0.2-local"
NETWORK_NAME = "ADDITION local testnet (send disabled)"
DISCLAIMER = (
    "local bootstrap JSON-RPC; bind 127.0.0.1; chainId 424242; "
    "not Ethereum mainnet; not live Uniswap/ETH/XMR; "
    "eth_sendRawTransaction disabled; TEXT node on loopback only"
)

# Methods exposed to MetaMask / local wallet tooling (read-mostly).
RPC_MODULES = {
    "web3": "1.0",
    "net": "1.0",
    "eth": "1.0",
    "wallet": "1.0",
    "addition": "1.0",
}


def is_loopback_host(host: str) -> bool:
    if host in {"localhost", "127.0.0.1", "::1"}:
        return True
    try:
        return ip_address(host).is_loopback
    except ValueError:
        return False


def require_loopback_bind(host: str) -> None:
    if not is_loopback_host(host):
        raise SystemExit(
            "error: EVM bridge bind must be loopback (got %s). "
            "Do not expose 9545 on 0.0.0.0." % host
        )


def tcp_rpc(command: str, timeout: float = 4.0) -> str:
    if not is_loopback_host(NODE_HOST):
        raise RuntimeError("native TEXT RPC host must be loopback")
    payload = command.strip() + "\n"
    with socket.create_connection((NODE_HOST, NODE_PORT), timeout=timeout) as sock:
        sock.sendall(payload.encode("utf-8"))
        chunks: list[bytes] = []
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break
    return b"".join(chunks).decode("utf-8", errors="replace").strip()


def kv_map(line: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for part in line.split():
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        out[key] = value
    return out


def hex_qty(n: int) -> str:
    return hex(int(n))


def ensure_0x(value: str) -> str:
    v = value.strip()
    if not v:
        return "0x0"
    if v.startswith("0x") or v.startswith("0X"):
        return "0x" + v[2:]
    return "0x" + v


def native_address(raw: str) -> str:
    addr = raw.strip()
    if addr.startswith("0x") or addr.startswith("0X"):
        addr = addr[2:]
    return addr


def parse_block_number(tag: str) -> int | None:
    if tag in {"latest", "pending", "safe", "finalized"}:
        info = kv_map(tcp_rpc("getinfo"))
        if "height" not in info:
            return None
        return int(info["height"])
    if tag == "earliest":
        return 0
    if tag.startswith("0x"):
        return int(tag, 16)
    if tag.isdigit():
        return int(tag)
    return None


def native_block(height_or_hash: str | int) -> dict[str, str] | None:
    raw = tcp_rpc("getblock %s" % height_or_hash)
    if raw.startswith("error:"):
        return None
    fields = kv_map(raw)
    if "height" not in fields or "hash" not in fields:
        return None
    return fields


def eth_block_from_native(fields: dict[str, str], full_txs: bool = False) -> dict[str, Any]:
    tx_hashes = [h for h in fields.get("tx_hashes", "").split(",") if h]
    parent = ensure_0x(fields.get("previous_hash", "0"))
    block_hash = ensure_0x(fields["hash"])
    miner = fields.get("reward_address") or fields.get("miner") or ""
    if miner and not miner.startswith("0x"):
        miner = "0x" + miner
    txs: list[Any] = tx_hashes if not full_txs else [
        {"hash": ensure_0x(h), "blockHash": block_hash, "blockNumber": hex_qty(int(fields["height"]))}
        for h in tx_hashes
    ]
    return {
        "number": hex_qty(int(fields["height"])),
        "hash": block_hash,
        "parentHash": parent,
        "nonce": hex_qty(int(fields.get("nonce", "0"))),
        "sha3Uncles": "0x" + ("0" * 64),
        "logsBloom": "0x" + ("0" * 512),
        "transactionsRoot": "0x" + ("0" * 64),
        "stateRoot": "0x" + ("0" * 64),
        "receiptsRoot": "0x" + ("0" * 64),
        "miner": miner or "0x" + ("0" * 40),
        "difficulty": hex_qty(int(fields.get("difficulty_target", "0"))),
        "totalDifficulty": hex_qty(int(fields.get("difficulty_target", "0"))),
        "extraData": "0x",
        "size": hex_qty(max(1, len(fields.get("hash", "")) + len(tx_hashes) * 64)),
        "gasLimit": hex_qty(30_000_000),
        "gasUsed": hex_qty(0),
        "timestamp": hex_qty(int(fields.get("timestamp", "0"))),
        "transactions": txs,
        "uncles": [],
        # Honest extension: native PoW algorithm from getblock/getinfo when present.
        "additionPowAlgorithm": fields.get("pow_algorithm", ""),
    }


def add_chain_params() -> dict:
    return {
        "chainId": hex_qty(CHAIN_ID),
        "chainName": NETWORK_NAME,
        "rpcUrls": ["http://127.0.0.1:%s" % LISTEN_PORT],
        "nativeCurrency": {"name": "ADD", "symbol": "ADD", "decimals": 18},
        "blockExplorerUrls": ["http://127.0.0.1:8080"],
        "iconUrls": [],
    }


def addition_network_info() -> dict[str, Any]:
    """Custom method: factual native getinfo + honest bootstrap flags."""
    info = kv_map(tcp_rpc("getinfo"))
    height = int(info["height"]) if "height" in info else None
    network = (info.get("network") or "").lower()
    return {
        "bridge": CLIENT_VERSION,
        "disclaimer": DISCLAIMER,
        "ethereumMainnet": False,
        "uniswapLive": False,
        "ethXmrBridgeLive": False,
        "sendRawEnabled": False,
        "bind": "http://%s:%s" % (LISTEN_HOST, LISTEN_PORT),
        "nativeRpc": "%s:%s" % (NODE_HOST, NODE_PORT),
        "chainId": CHAIN_ID,
        "network": network or None,
        "height": height,
        "mainnetHasBlocks": bool(network == "mainnet" and height is not None and height >= 1),
        "pow_algorithm": info.get("pow_algorithm"),
        "peers": info.get("peers"),
        "mempool": info.get("mempool"),
    }


def handle(method: str, params: list) -> object:
    if method in {"web3_clientVersion"}:
        return CLIENT_VERSION
    if method in {"eth_protocolVersion"}:
        return "0x41"  # cosmetic; not claiming Ethereum parity
    if method in {"eth_chainId"}:
        return hex_qty(CHAIN_ID)
    if method in {"net_version"}:
        return str(CHAIN_ID)
    if method == "net_listening":
        return True
    if method == "net_peerCount":
        info = kv_map(tcp_rpc("getinfo"))
        return hex_qty(int(info.get("peers", "0")))
    if method == "rpc_modules":
        return dict(RPC_MODULES)
    if method == "addition_disclaimer":
        return DISCLAIMER
    if method == "addition_networkInfo":
        return addition_network_info()
    if method == "eth_syncing":
        return False
    if method == "eth_accounts":
        return []
    if method == "eth_requestAccounts":
        return []
    if method == "eth_coinbase":
        return "0x" + ("0" * 40)
    if method == "eth_gasPrice":
        info = kv_map(tcp_rpc("fee_info"))
        if "recommended_min_fee" not in info:
            raise RuntimeError("RPC offline")
        return hex_qty(int(info["recommended_min_fee"]))
    if method in {"eth_maxPriorityFeePerGas"}:
        return hex_qty(0)
    if method == "eth_blockNumber":
        info = kv_map(tcp_rpc("getinfo"))
        if "height" not in info:
            raise RuntimeError("RPC offline")
        return hex_qty(int(info["height"]))
    if method == "eth_getBalance":
        if not params:
            raise ValueError("missing address")
        raw = tcp_rpc("getbalance %s" % native_address(str(params[0])))
        if raw.startswith("error:"):
            raise RuntimeError(raw)
        # Native balances are whole units (not wei). Return that quantity as hex — do not invent 1e18.
        return hex_qty(int(raw.strip().split()[0]))
    if method == "eth_getTransactionCount":
        raise RuntimeError("not a full EVM: no account nonce mapping")
    if method == "eth_estimateGas":
        raise RuntimeError("not a full EVM: eth_estimateGas unsupported")
    if method == "eth_getCode":
        return "0x"
    if method == "eth_call":
        raise RuntimeError("not a full EVM: eth_call unsupported")
    if method == "eth_sendRawTransaction":
        raise RuntimeError(
            "eth_sendRawTransaction disabled: local bootstrap only, "
            "no native EVM execution mapping; not Ethereum mainnet"
        )
    if method == "eth_sendTransaction":
        raise RuntimeError("eth_sendTransaction disabled: local bootstrap only")
    if method == "eth_getBlockByNumber":
        if not params:
            raise ValueError("missing block tag")
        height = parse_block_number(str(params[0]))
        if height is None:
            return None
        fields = native_block(height)
        if fields is None:
            return None
        full = bool(params[1]) if len(params) > 1 else False
        return eth_block_from_native(fields, full_txs=full)
    if method == "eth_getBlockByHash":
        if not params:
            raise ValueError("missing block hash")
        h = native_address(str(params[0]))
        fields = native_block(h)
        if fields is None:
            return None
        full = bool(params[1]) if len(params) > 1 else False
        return eth_block_from_native(fields, full_txs=full)
    if method in {"eth_getTransactionByHash", "eth_getTransactionReceipt"}:
        if not params:
            raise ValueError("missing tx hash")
        tx_hash = str(params[0])
        raw = tcp_rpc("tx_status %s" % native_address(tx_hash))
        fields = kv_map(raw)
        if fields.get("status") in {None, "unknown"}:
            return None
        status = fields.get("status", "")
        ok = status in {"confirmed", "included", "ok", "success"}
        block_hex = None
        if fields.get("block_height"):
            try:
                block_hex = hex_qty(int(fields["block_height"]))
            except ValueError:
                block_hex = None
        base = {
            "transactionHash": ensure_0x(fields.get("tx_hash", tx_hash)),
            "transactionIndex": hex_qty(int(fields["tx_index"])) if fields.get("tx_index", "").isdigit() else "0x0",
            "blockNumber": block_hex,
            "blockHash": ensure_0x(fields["block_hash"]) if fields.get("block_hash") else None,
            "from": ensure_0x(fields["from"]) if fields.get("from") else None,
            "to": ensure_0x(fields["to"]) if fields.get("to") else None,
            "gasUsed": hex_qty(0),
            "cumulativeGasUsed": hex_qty(0),
            "contractAddress": None,
            "logs": [],
            "status": "0x1" if ok else "0x0",
            "type": "0x0",
            "additionStatus": status,
        }
        if method == "eth_getTransactionByHash":
            return {
                "hash": base["transactionHash"],
                "nonce": "0x0",
                "blockHash": base["blockHash"],
                "blockNumber": base["blockNumber"],
                "transactionIndex": base["transactionIndex"],
                "from": base["from"],
                "to": base["to"],
                "value": "0x0",
                "gas": hex_qty(0),
                "gasPrice": hex_qty(0),
                "input": "0x",
                "additionStatus": status,
            }
        return base
    if method == "eth_feeHistory":
        raise RuntimeError("not a full EVM: eth_feeHistory unsupported")
    if method == "web3_sha3":
        raise RuntimeError(
            "web3_sha3 unsupported here (Ethereum keccak256 ≠ ADDITION SHA3-512); "
            "use native TEXT RPC for hashing"
        )
    if method == "wallet_addEthereumChain":
        return add_chain_params()
    if method == "wallet_switchEthereumChain":
        return None
    raise RuntimeError("method not available on EVM bootstrap: %s" % method)


def dispatch_one(req: dict) -> dict:
    req_id = req.get("id")
    method = req.get("method")
    params = req.get("params") or []
    if not isinstance(params, list):
        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "error": {"code": -32602, "message": "params must be an array"},
        }
    try:
        result = handle(str(method), params)
        return {"jsonrpc": "2.0", "id": req_id, "result": result}
    except Exception as exc:
        return {"jsonrpc": "2.0", "id": req_id, "error": {"code": -32000, "message": str(exc)}}


class Handler(BaseHTTPRequestHandler):
    server_version = CLIENT_VERSION

    def log_message(self, fmt: str, *args) -> None:
        print("%s - %s" % (self.address_string(), fmt % args))

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self) -> None:
        body = (
            "ADDITION EVM local JSON-RPC\n"
            "%s\n"
            "eth_chainId=%s net_version=%s\n"
            "methods: eth_*/net_*/web3_clientVersion/rpc_modules/"
            "addition_networkInfo/addition_disclaimer\n"
            % (DISCLAIMER, hex_qty(CHAIN_ID), CHAIN_ID)
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length") or "0")
        raw = self.rfile.read(max(0, length)).decode("utf-8", errors="replace")
        try:
            req = json.loads(raw)
        except json.JSONDecodeError:
            self._json({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}})
            return
        if isinstance(req, list):
            self._json([dispatch_one(item if isinstance(item, dict) else {}) for item in req])
            return
        if not isinstance(req, dict):
            self._json({"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "invalid request"}})
            return
        self._json(dispatch_one(req))

    def _json(self, payload: object) -> None:
        data = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data)


def main() -> None:
    require_loopback_bind(LISTEN_HOST)
    if not is_loopback_host(NODE_HOST):
        print("error: native TEXT RPC host must be loopback", file=sys.stderr)
        raise SystemExit(1)
    print(
        "ADDITION EVM local on http://%s:%s -> native RPC %s:%s (%s)"
        % (LISTEN_HOST, LISTEN_PORT, NODE_HOST, NODE_PORT, DISCLAIMER)
    )
    ThreadingHTTPServer((LISTEN_HOST, LISTEN_PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
