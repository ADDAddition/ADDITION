#!/usr/bin/env python3
"""Local write-RPC AMM: create pool, exact-in, live TVL, public writes refused."""

from __future__ import annotations

import os
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("ADDITIOND", ROOT / "build" / "additiond"))


def tcp_rpc(host: str, port: int, command: str, timeout: float = 8.0) -> str:
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
            last = tcp_rpc(host, port, "getinfo")
            if last:
                return last
        except OSError:
            time.sleep(0.2)
    raise TimeoutError("timeout waiting for %s:%s last=%s" % (host, port, last))


def pick_ports() -> dict[str, int]:
    preferred = {"write": 18645, "pub": 19645}
    busy = False
    for port in preferred.values():
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                busy = True
                break
        except OSError:
            pass
    if not busy:
        return preferred
    return {"write": 29245, "pub": 29246}


def start_node(args: list[str], log_path: Path) -> subprocess.Popen:
    env = os.environ.copy()
    for key in (
        "ADDITION_ENABLE_PUBLIC_RPC",
        "ADDITION_PUBLIC_RPC_PORT",
        "ADDITION_PUBLIC_RPC_BIND",
        "ADDITION_LOCAL_RPC_PORT",
        "ADDITION_P2P_PORT",
        "ADDITION_ENABLE_P2P_RPC",
        "ADDITION_AUTO_MINE",
        "ADDITION_AUTO_MINE_INTERVAL",
        "ADDITION_AUTO_MINE_REWARD",
        "ADDITION_RPC_TOKEN",
    ):
        env.pop(key, None)
    log = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(
        args,
        cwd=str(ROOT),
        stdin=subprocess.DEVNULL,
        stdout=log,
        stderr=subprocess.STDOUT,
        env=env,
    )
    proc._log_handle = log  # type: ignore[attr-defined]
    return proc


def stop_node(proc: subprocess.Popen | None, sig: int = signal.SIGTERM) -> None:
    if proc is None:
        return
    if proc.poll() is None:
        proc.send_signal(sig)
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=4)
    handle = getattr(proc, "_log_handle", None)
    if handle is not None:
        handle.close()


def fail(msg: str) -> int:
    print("test failed:", msg, file=sys.stderr)
    return 1


def main() -> int:
    if not BIN.is_file():
        return fail("missing additiond at %s" % BIN)

    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-amm-live-"))
    data_dir = tmp / "data"
    log_path = tmp / "node.log"
    node = None
    try:
        node = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(data_dir),
                "--local-rpc-port",
                str(ports["write"]),
                "--public-rpc",
                "--public-rpc-bind",
                "127.0.0.1",
                "--public-rpc-port",
                str(ports["pub"]),
            ],
            log_path,
        )
        info0 = wait_port("127.0.0.1", ports["write"])
        if "network=testnet" not in info0:
            return fail("getinfo: " + info0)
        wait_port("127.0.0.1", ports["pub"])

        log_text = log_path.read_text(encoding="utf-8", errors="replace")
        if ("local RPC listening on 127.0.0.1:%s" % ports["write"]) not in log_text:
            return fail("write RPC must bind 127.0.0.1: " + log_text)

        if tcp_rpc("127.0.0.1", ports["write"], "swap_tvl") != "tvl=0":
            return fail("swap_tvl empty")
        if tcp_rpc("127.0.0.1", ports["write"], "token_create AAA alice 100000 1000") != "ok":
            return fail("token_create AAA")
        if tcp_rpc("127.0.0.1", ports["write"], "token_create BBB alice 100000 1000") != "ok":
            return fail("token_create BBB")
        if tcp_rpc("127.0.0.1", ports["write"], "swap_pool_create AAA BBB 30") != "ok":
            return fail("swap_pool_create")
        if tcp_rpc("127.0.0.1", ports["write"], "add_liquidity AAA BBB alice 200 200") != "ok":
            return fail("add_liquidity")
        pool = tcp_rpc("127.0.0.1", ports["write"], "swap_pool_info AAA BBB")
        if "reserve_AAA=200" not in pool or "reserve_BBB=200" not in pool:
            return fail("swap_pool_info: " + pool)
        if tcp_rpc("127.0.0.1", ports["write"], "swap_tvl") != "tvl=400":
            return fail("swap_tvl live")

        swapped = tcp_rpc("127.0.0.1", ports["write"], "swap_exact_in AAA BBB alice 10 1")
        if not swapped.startswith("ok:amount_out="):
            return fail("swap_exact_in: " + swapped)
        if tcp_rpc("127.0.0.1", ports["write"], "token_balance AAA alice") != "790":
            return fail("token_balance after swap")
        tvl_after = tcp_rpc("127.0.0.1", ports["write"], "swap_tvl")
        if tvl_after in {"tvl=0", "tvl=400"} or not tvl_after.startswith("tvl="):
            return fail("swap_tvl after exact-in: " + tvl_after)

        for blocked in (
            "swap_pool_create AAA BBB 30",
            "swap_exact_in AAA BBB alice 10 1",
            "add_liquidity AAA BBB alice 1 1",
        ):
            pub = tcp_rpc("127.0.0.1", ports["pub"], blocked)
            if "command disabled on public RPC" not in pub:
                return fail("public %s: %s" % (blocked.split()[0], pub))

        for bad in (
            "swap_pool_create AAA BBB 30 EXTRA",
            "swap_pool_create AAA BBB 30abc",
            "swap_pool_create AAA AAA 30",
            "swap_exact_in AAA BBB alice 10 1 EXTRA",
            "swap_exact_in AAA BBB alice -1 1",
            "add_liquidity AAA BBB alice 10 0",
        ):
            reply = tcp_rpc("127.0.0.1", ports["write"], bad)
            if not reply.startswith("error:"):
                return fail("mutated input must be rejected: %s -> %s" % (bad, reply))

        stop_node(node)
        node = None
        tokens_dat = data_dir / "tokens.dat"
        if not tokens_dat.is_file():
            return fail("tokens.dat missing after SIGTERM")
        dumped = tokens_dat.read_text(encoding="utf-8", errors="replace")
        if "P|AAA|BBB|" not in dumped:
            return fail("tokens.dat missing pool line: " + dumped)
        if "P|AAA|BBB|AAA|BBB|" in dumped:
            return fail("tokens.dat still embeds split pool key")

        node = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(data_dir),
                "--local-rpc-port",
                str(ports["write"]),
                "--public-rpc",
                "--public-rpc-bind",
                "127.0.0.1",
                "--public-rpc-port",
                str(ports["pub"]),
            ],
            tmp / "node2.log",
        )
        wait_port("127.0.0.1", ports["write"])
        restored = tcp_rpc("127.0.0.1", ports["write"], "swap_tvl")
        if restored != tvl_after:
            return fail("swap_tvl after restart: %s want %s" % (restored, tvl_after))
        pool2 = tcp_rpc("127.0.0.1", ports["write"], "swap_pool_info AAA BBB")
        if "reserve_AAA=" not in pool2 or "reserve_BBB=" not in pool2:
            return fail("pool after restart: " + pool2)

        print("amm live: create + exact-in + tvl + public writes refused + persist")
        return 0
    finally:
        stop_node(node)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
