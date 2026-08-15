#!/usr/bin/env python3
"""ADDITION EVM JSON-RPC bootstrap. Not a full EVM. eth_sendRawTransaction disabled."""

from __future__ import annotations

import json
import os
import socket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CHAIN_ID = 424242
LISTEN_HOST = os.environ.get("ADDITION_EVM_BIND", "127.0.0.1")
LISTEN_PORT = int(os.environ.get("ADDITION_EVM_PORT", "9545"))
NODE_HOST = os.environ.get("ADDITION_LOCAL_RPC_HOST", "127.0.0.1")
NODE_PORT = int(os.environ.get("ADDITION_LOCAL_RPC_PORT", "8545"))
CLIENT_VERSION = "addition-evm-bridge-bootstrap/0.1"


def tcp_rpc(command: str, timeout: float = 4.0) -> str:
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
    return hex(n)


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


def native_block(height: int) -> dict[str, str] | None:
    raw = tcp_rpc("getblock %s" % height)
    if raw.startswith("error:"):
        return None
    fields = kv_map(raw)
    if "height" not in fields or "hash" not in fields:
        return None
    return fields


def handle(method: str, params: list) -> object:
    if method in {"web3_clientVersion"}:
        return CLIENT_VERSION
    if method in {"eth_chainId"}:
        return hex_qty(CHAIN_ID)
    if method in {"net_version"}:
        return str(CHAIN_ID)
    if method == "eth_syncing":
        return False
    if method == "eth_accounts":
        return []
    if method == "eth_requestAccounts":
        return []
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
        raw = tcp_rpc("getbalance %s" % params[0])
        if raw.startswith("error:"):
            raise RuntimeError(raw)
        return hex_qty(int(raw))
    if method == "eth_getTransactionCount":
        raise RuntimeError("not a full EVM: no account nonce mapping")
    if method == "eth_estimateGas":
        raise RuntimeError("not a full EVM: eth_estimateGas unsupported")
    if method == "eth_getCode":
        return "0x"
    if method == "eth_call":
        raise RuntimeError("not a full EVM: eth_call unsupported")
    if method == "eth_sendRawTransaction":
        raise RuntimeError("eth_sendRawTransaction disabled until native execution mapping")
    if method == "eth_getBlockByNumber":
        if not params:
            raise ValueError("missing block tag")
        height = parse_block_number(str(params[0]))
        if height is None:
            return None
        fields = native_block(height)
        if fields is None:
            return None
        tx_hashes = [h for h in fields.get("tx_hashes", "").split(",") if h]
        return {
            "number": hex_qty(int(fields["height"])),
            "hash": "0x" + fields["hash"] if not fields["hash"].startswith("0x") else fields["hash"],
            "parentHash": fields.get("previous_hash", "0x0"),
            "timestamp": hex_qty(int(fields.get("timestamp", "0"))),
            "nonce": hex_qty(int(fields.get("nonce", "0"))),
            "difficulty": hex_qty(int(fields.get("difficulty_target", "0"))),
            "transactions": tx_hashes,
        }
    if method in {"eth_getTransactionByHash", "eth_getTransactionReceipt"}:
        if not params:
            raise ValueError("missing tx hash")
        raw = tcp_rpc("tx_status %s" % params[0])
        fields = kv_map(raw)
        if fields.get("status") in {None, "unknown"}:
            return None
        return {
            "transactionHash": fields.get("tx_hash", params[0]),
            "status": fields.get("status"),
            "blockNumber": fields.get("block_height"),
            "transactionIndex": fields.get("tx_index"),
        }
    if method == "eth_feeHistory":
        raise RuntimeError("not a full EVM: eth_feeHistory unsupported")
    if method in {"wallet_addEthereumChain", "wallet_switchEthereumChain"}:
        return None
    raise RuntimeError("method not available on EVM bootstrap: %s" % method)


class Handler(BaseHTTPRequestHandler):
    server_version = CLIENT_VERSION

    def log_message(self, fmt: str, *args) -> None:
        print("%s - %s" % (self.address_string(), fmt % args))

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length") or "0")
        raw = self.rfile.read(max(0, length)).decode("utf-8", errors="replace")
        try:
            req = json.loads(raw)
        except json.JSONDecodeError:
            self._json({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}})
            return
        req_id = req.get("id")
        method = req.get("method")
        params = req.get("params") or []
        try:
            result = handle(method, params)
            self._json({"jsonrpc": "2.0", "id": req_id, "result": result})
        except Exception as exc:
            self._json({"jsonrpc": "2.0", "id": req_id, "error": {"code": -32000, "message": str(exc)}})

    def _json(self, payload: dict) -> None:
        data = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data)


def main() -> None:
    print(
        "ADDITION EVM bootstrap on http://%s:%s -> native RPC %s:%s (not a full EVM; sendRaw disabled)"
        % (LISTEN_HOST, LISTEN_PORT, NODE_HOST, NODE_PORT)
    )
    ThreadingHTTPServer((LISTEN_HOST, LISTEN_PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
