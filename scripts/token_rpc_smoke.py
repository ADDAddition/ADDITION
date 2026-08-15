#!/usr/bin/env python3
"""Live smoke: token CLI + JSON-RPC adapter against a local additiond.

Starts an isolated testnet daemon when --start-daemon is passed. Research
testnet only. No public peers, no DEX, no Ethereum JSON-RPC claims.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path
from typing import List, Optional

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from addition_jsonrpc_adapter import AdapterServer
from addition_text_rpc import TextRpcClient
from addition_tokens import TokenClient


def port_free(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("127.0.0.1", port))
        except OSError:
            return False
    return True


def wait_port(port: int, timeout: float = 20.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.4):
                return
        except OSError:
            time.sleep(0.15)
    raise RuntimeError(f"timeout waiting for 127.0.0.1:{port}")


def write_temp_config(tmp: Path, rpc_port: int) -> Path:
    cfg = tmp / "config.toml"
    cfg.write_text(
        "\n".join(
            [
                'network = "testnet"',
                'network_name = "addition-testnet"',
                'network_id = "ADDITION_TESTNET_V1"',
                f'data_dir = "{tmp / "data"}"',
                f'genesis_file = "{ROOT / "genesis.json"}"',
                "bootstrap_peers = [",
                '  "127.0.0.1:28545"',
                "]",
                "[ports]",
                f"local_rpc = {rpc_port}",
                "lan_rpc = 18545",
                "p2p = 28545",
                "",
            ]
        ),
        encoding="utf-8",
    )
    return cfg


def start_daemon(binary: Path, cfg: Path, data_dir: Path, log_path: Path) -> subprocess.Popen:
    env = os.environ.copy()
    env.pop("ADDITION_ENABLE_LAN_RPC", None)
    env.pop("ADDITION_ENABLE_P2P_RPC", None)
    env.pop("ADDITION_RPC_TOKEN", None)
    env.pop("ADDITION_MAINNET_MODE", None)
    log = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(
        [
            str(binary),
            "--network",
            "testnet",
            "--config",
            str(cfg),
            "--genesis",
            str(ROOT / "genesis.json"),
            "--data-dir",
            str(data_dir),
        ],
        cwd=str(ROOT),
        stdin=subprocess.PIPE,
        stdout=log,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )
    return proc


def stop_daemon(proc: subprocess.Popen) -> None:
    if proc.poll() is not None:
        return
    try:
        if proc.stdin:
            proc.stdin.write("quit\n")
            proc.stdin.flush()
        proc.wait(timeout=8)
    except Exception:
        proc.terminate()
        try:
            proc.wait(timeout=4)
        except Exception:
            proc.kill()


def jsonrpc(url: str, method: str, params: list, req_id: int = 1) -> dict:
    body = json.dumps({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params}).encode("utf-8")
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=8) as resp:
        return json.loads(resp.read().decode("utf-8"))


def run_path(rpc_port: int, adapter_port: int) -> List[str]:
    lines: List[str] = []
    rpc = TextRpcClient(host="127.0.0.1", port=rpc_port)
    tokens = TokenClient(rpc)

    info = tokens.getinfo()
    lines.append(f"getinfo {info}")
    if "network=testnet" not in info:
        raise RuntimeError("getinfo did not report network=testnet")

    create = tokens.create("SMOKE", "alice", 1_000_000, 1000)
    lines.append(f"token_create {create}")
    mint = tokens.mint("SMOKE", "alice", "bob", 50)
    lines.append(f"token_mint {mint}")
    xfer = tokens.transfer("SMOKE", "alice", "bob", 10)
    lines.append(f"token_transfer {xfer}")
    alice = tokens.balance("SMOKE", "alice")
    bob = tokens.balance("SMOKE", "bob")
    lines.append(f"token_balance alice={alice} bob={bob}")
    if alice != "990" or bob != "60":
        raise RuntimeError(f"unexpected balances alice={alice} bob={bob} (want 990 / 60)")
    info_tok = tokens.info("SMOKE")
    lines.append(f"token_info {info_tok}")
    if "symbol=SMOKE" not in info_tok or "total_supply=1050" not in info_tok:
        raise RuntimeError("token_info missing expected fields")

    tokens.nft_mint("COL", "item1", "alice", "smoke-meta")
    tokens.nft_transfer("COL", "item1", "alice", "bob")
    owner = tokens.nft_owner("COL", "item1")
    lines.append(f"nft_owner {owner}")
    if owner != "bob":
        raise RuntimeError(f"nft owner={owner} want bob")

    burnable = tokens.create_ex("BURN", "Burnable", "alice", 100, 20, 0, True, "", 0)
    lines.append(f"token_create_ex {burnable}")
    tokens.burn("BURN", "alice", 5)
    burn_bal = tokens.balance("BURN", "alice")
    lines.append(f"token_burn leftover={burn_bal}")
    if burn_bal != "15":
        raise RuntimeError(f"burn leftover={burn_bal} want 15")

    missing = rpc.call("token_balance NOSUCH alice")
    lines.append(f"missing_token_balance {missing}")
    if missing != "0":
        raise RuntimeError("missing token should report balance 0")

    adapter = AdapterServer(("127.0.0.1", adapter_port), rpc, allow_writes=True)
    thread = threading.Thread(target=adapter.serve_forever, daemon=True)
    thread.start()
    try:
        url = f"http://127.0.0.1:{adapter_port}/rpc"
        g = jsonrpc(url, "getinfo", [])
        lines.append(f"jsonrpc getinfo {g.get('result')}")
        if "error" in g:
            raise RuntimeError(g)
        bal = jsonrpc(url, "token_balance", ["SMOKE", "bob"], 2)
        lines.append(f"jsonrpc token_balance {bal.get('result')}")
        if bal.get("result") != "60":
            raise RuntimeError(f"jsonrpc balance {bal}")
        eth = jsonrpc(url, "eth_blockNumber", [], 3)
        lines.append(f"jsonrpc eth_blockNumber refused={eth.get('error', {}).get('code')}")
        if eth.get("error", {}).get("code") != -32601:
            raise RuntimeError(f"eth_* should be refused: {eth}")
        swap = jsonrpc(url, "swap_quote", ["A", "B", 1], 4)
        if swap.get("error", {}).get("code") != -32601:
            raise RuntimeError(f"swap_quote should not be forwarded as a DEX: {swap}")
        lines.append("jsonrpc swap_quote not_forwarded")
    finally:
        adapter.shutdown()
        adapter.server_close()

    return lines


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Live token TEXT RPC smoke test")
    parser.add_argument("--rpc-port", type=int, default=18547)
    parser.add_argument("--adapter-port", type=int, default=18647)
    parser.add_argument("--start-daemon", action="store_true")
    parser.add_argument(
        "--daemon",
        default=str(ROOT / "build" / "additiond"),
        help="path to additiond",
    )
    args = parser.parse_args(argv)

    proc: Optional[subprocess.Popen] = None
    tmp_dir: Optional[tempfile.TemporaryDirectory] = None
    try:
        if args.start_daemon:
            binary = Path(args.daemon)
            if not binary.is_file():
                print(f"error: additiond not found at {binary}", file=sys.stderr)
                return 2
            if not port_free(args.rpc_port):
                print(f"error: 127.0.0.1:{args.rpc_port} is already in use", file=sys.stderr)
                return 2
            tmp_dir = tempfile.TemporaryDirectory(prefix="addition-token-smoke-")
            tmp = Path(tmp_dir.name)
            data_dir = tmp / "data"
            data_dir.mkdir()
            cfg = write_temp_config(tmp, args.rpc_port)
            log_path = tmp / "daemon.log"
            proc = start_daemon(binary, cfg, data_dir, log_path)
            wait_port(args.rpc_port, timeout=25.0)
            print(f"daemon pid={proc.pid} rpc=127.0.0.1:{args.rpc_port} log={log_path}")

        lines = run_path(args.rpc_port, args.adapter_port)
        for line in lines:
            print(line)
        print("OK: token TEXT RPC + local JSON-RPC adapter smoke passed")
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        if proc is not None:
            stop_daemon(proc)
        if tmp_dir is not None:
            tmp_dir.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
