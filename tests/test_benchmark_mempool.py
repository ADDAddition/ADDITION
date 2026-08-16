#!/usr/bin/env python3
"""Live additiond: leftover mempool must not block mine; benchmark_objective must not fail."""

from __future__ import annotations

import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("ADDITIOND", ROOT / "build" / "additiond"))
PRIVACY_KEY = "addition-research-privacy-master-key-32"


def tcp_rpc(host: str, port: int, command: str, timeout: float = 45.0) -> str:
    payload = command.strip() + "\n"
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(payload.encode("utf-8"))
        chunks: list[bytes] = []
        while True:
            data = sock.recv(8192)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break
    return b"".join(chunks).decode("utf-8", errors="replace").strip()


def wait_port(host: str, port: int, timeout: float = 45.0) -> str:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        try:
            last = tcp_rpc(host, port, "getinfo", timeout=2.0)
            if last:
                return last
        except OSError:
            time.sleep(0.2)
    raise TimeoutError("timeout waiting for %s:%s last=%s" % (host, port, last))


def main() -> int:
    if not BIN.is_file():
        print("missing additiond at", BIN, file=sys.stderr)
        return 2
    data = Path(tempfile.mkdtemp(prefix="addition-bench-"))
    log_path = data / "node.log"
    port = 19147
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            port = 19247
    except OSError:
        pass
    env = os.environ.copy()
    env["ADDITION_PRIVACY_MASTER_KEY"] = PRIVACY_KEY
    for key in (
        "ADDITION_ENABLE_PUBLIC_RPC",
        "ADDITION_PUBLIC_RPC_PORT",
        "ADDITION_LOCAL_RPC_PORT",
        "ADDITION_P2P_PORT",
        "ADDITION_AUTO_MINE",
    ):
        env.pop(key, None)
    log = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(
        [
            str(BIN),
            "--network",
            "testnet",
            "--data-dir",
            str(data),
            "--local-rpc-port",
            str(port),
        ],
        cwd=str(ROOT),
        stdin=subprocess.DEVNULL,
        stdout=log,
        stderr=subprocess.STDOUT,
        env=env,
    )
    try:
        wait_port("127.0.0.1", port)
        created = tcp_rpc("127.0.0.1", port, "createwallet miner")
        if "address=" not in created:
            print("createwallet failed:", created, file=sys.stderr)
            return 1
        addr = created.split("address=", 1)[1].split()[0]
        mined = tcp_rpc("127.0.0.1", port, "mine " + addr, timeout=45.0)
        if not mined.startswith("mined block"):
            print("mine failed:", mined, file=sys.stderr)
            return 1
        sent = tcp_rpc("127.0.0.1", port, "wallet_send miner bob 10 1")
        if not sent.startswith("ok:gossiped"):
            print("wallet_send failed:", sent, file=sys.stderr)
            return 1
        sent2 = tcp_rpc("127.0.0.1", port, "wallet_send miner carol 5 1")
        mined2 = tcp_rpc("127.0.0.1", port, "mine " + addr, timeout=45.0)
        if not mined2.startswith("mined block"):
            print("leftover mempool blocked mine:", mined2, "second send=", sent2, file=sys.stderr)
            return 1
        bench = tcp_rpc("127.0.0.1", port, "benchmark_objective 1 2", timeout=45.0)
        if bench.startswith("error:") or "invalid or spent input" in bench:
            print("benchmark_objective failed:", bench, file=sys.stderr)
            return 1
        if "objective_privacy_ok=false" not in bench or "opening_hash_ok=true" not in bench:
            print("benchmark_objective overclaim or missing opening hash:", bench, file=sys.stderr)
            return 1
        status = tcp_rpc("127.0.0.1", port, "protocol_status")
        if "objective_privacy_ok=false" not in status or "privacy_claim=opening_not_zk" not in status:
            print("protocol_status overclaim:", status, file=sys.stderr)
            return 1
        zk = tcp_rpc("127.0.0.1", port, "privacy_mint_zk alice 1 aa bb cc dd")
        if not zk.startswith("error:"):
            print("zk_* must not fake success:", zk, file=sys.stderr)
            return 1
        print("benchmark leftover mempool live test passed")
        print(bench)
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=4)
        log.close()


if __name__ == "__main__":
    raise SystemExit(main())
