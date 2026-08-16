#!/usr/bin/env python3
"""Prove additiond keeps height + block hashes across a kill/restart.

Mines N blocks, SIGKILLs the process (no shutdown save), starts again with the
same --data-dir, and checks getinfo height plus getblock hashes.
"""

from __future__ import annotations

import os
import re
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
MINE_TIMEOUT_SEC = 30.0
BLOCKS_TO_MINE = 2


def field(text: str, name: str) -> str:
    match = re.search(r"(?:^|[\s:])%s=(\S+)" % re.escape(name), text)
    return match.group(1) if match else ""


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


def start_node(args: list[str], log_path: Path) -> subprocess.Popen:
    env = os.environ.copy()
    env.pop("ADDITION_ENABLE_PUBLIC_RPC", None)
    env.pop("ADDITION_PUBLIC_RPC_PORT", None)
    env.pop("ADDITION_PUBLIC_RPC_BIND", None)
    env.pop("ADDITION_LOCAL_RPC_PORT", None)
    env.pop("ADDITION_P2P_PORT", None)
    env.pop("ADDITION_ENABLE_P2P_RPC", None)
    env.pop("ADDITION_AUTO_MINE", None)
    env.pop("ADDITION_AUTO_MINE_INTERVAL", None)
    env.pop("ADDITION_AUTO_MINE_REWARD", None)
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


def close_log(proc: subprocess.Popen | None) -> None:
    if proc is None:
        return
    handle = getattr(proc, "_log_handle", None)
    if handle is not None:
        handle.close()
        proc._log_handle = None  # type: ignore[attr-defined]


def kill_hard(proc: subprocess.Popen | None) -> None:
    if proc is None or proc.poll() is not None:
        close_log(proc)
        return
    proc.send_signal(signal.SIGKILL)
    proc.wait(timeout=4)
    close_log(proc)


def fail(msg: str) -> int:
    print("test failed:", msg, file=sys.stderr)
    return 1


def pick_port() -> int:
    preferred = 19545
    try:
        with socket.create_connection(("127.0.0.1", preferred), timeout=0.2):
            pass
        return 19645
    except OSError:
        return preferred


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        print("build: cmake -S . -B build -DADDITION_BUILD_TESTS=ON && cmake --build build", file=sys.stderr)
        return 2

    port = pick_port()
    tmp = Path(tempfile.mkdtemp(prefix="addition-persist-restart-"))
    data_dir = tmp / "node"
    data_dir.mkdir()
    proc = None
    try:
        args = [
            str(BIN),
            "--network",
            "testnet",
            "--data-dir",
            str(data_dir),
            "--local-rpc-port",
            str(port),
        ]
        proc = start_node(args, tmp / "node1.log")
        info0 = wait_port("127.0.0.1", port)
        print("CMD getinfo")
        print(info0)
        if "network=testnet" not in info0:
            return fail("getinfo network: " + info0)
        if field(info0, "height") != "0":
            return fail("fresh node height must be 0: " + info0)

        hashes: list[str] = []
        for i in range(BLOCKS_TO_MINE):
            print("CMD mine persist_miner")
            mined = tcp_rpc("127.0.0.1", port, "mine persist_miner", timeout=MINE_TIMEOUT_SEC + 5.0)
            print(mined)
            if not mined.startswith("mined block"):
                return fail("mine %s failed: %s" % (i + 1, mined))
            block_hash = field(mined, "hash")
            if not block_hash:
                return fail("mine reply missing hash: " + mined)
            hashes.append(block_hash)

        info1 = tcp_rpc("127.0.0.1", port, "getinfo")
        print("CMD getinfo")
        print(info1)
        height1 = int(field(info1, "height") or "0")
        if height1 != BLOCKS_TO_MINE:
            return fail("height after mine: " + info1)

        before: list[str] = []
        for h in range(1, height1 + 1):
            blk = tcp_rpc("127.0.0.1", port, "getblock %s" % h)
            print("CMD getblock", h)
            print(blk)
            got = field(blk, "hash")
            if got != hashes[h - 1]:
                return fail("getblock %s hash mismatch: %s" % (h, blk))
            before.append(blk)

        blocks_dat = data_dir / "blocks.dat"
        if not blocks_dat.is_file() or blocks_dat.stat().st_size == 0:
            return fail("blocks.dat missing or empty before kill: " + str(list(data_dir.iterdir())))

        print("SIGKILL additiond (no shutdown save)")
        kill_hard(proc)
        proc = None

        proc = start_node(args, tmp / "node2.log")
        info2 = wait_port("127.0.0.1", port)
        print("CMD getinfo after restart")
        print(info2)
        height2 = int(field(info2, "height") or "0")
        if height2 != height1:
            return fail("height after restart %s != %s: %s" % (height2, height1, info2))

        for h in range(1, height2 + 1):
            blk = tcp_rpc("127.0.0.1", port, "getblock %s" % h)
            print("CMD getblock after restart", h)
            print(blk)
            if field(blk, "hash") != hashes[h - 1]:
                return fail("restart getblock %s hash changed: %s" % (h, blk))
            if field(blk, "hash") != field(before[h - 1], "hash"):
                return fail("restart getblock %s does not match pre-kill block" % h)

        print("chain persist restart ok height=%s hashes=%s" % (height2, ",".join(hashes)))
        return 0
    finally:
        kill_hard(proc)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
