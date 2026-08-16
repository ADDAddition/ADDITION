#!/usr/bin/env python3
"""Live additiond: auto-mine stays off by default; one real block when enabled."""

from __future__ import annotations

import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("ADDITIOND", ROOT / "build" / "additiond"))


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


def stop_node(proc: subprocess.Popen | None) -> None:
    if proc is None:
        return
    if proc.poll() is None:
        proc.terminate()
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


def pick_ports() -> tuple[int, int]:
    write_port = 19245
    pub_port = 39245
    try:
        with socket.create_connection(("127.0.0.1", write_port), timeout=0.2):
            write_port = 19345
            pub_port = 39345
    except OSError:
        pass
    return write_port, pub_port


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        return 2

    write_port, pub_port = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-auto-mine-"))
    off_dir = tmp / "off"
    on_dir = tmp / "on"
    off_dir.mkdir()
    on_dir.mkdir()
    proc = None
    try:
        proc = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(off_dir),
                "--local-rpc-port",
                str(write_port),
                "--public-rpc",
                "--public-rpc-port",
                str(pub_port),
                "--public-rpc-bind",
                "127.0.0.1",
            ],
            tmp / "off.log",
        )
        info = wait_port("127.0.0.1", write_port)
        if field(info, "auto_mine") != "off":
            return fail("default auto_mine: " + info)
        if field(info, "height") != "0":
            return fail("default height: " + info)
        time.sleep(2)
        later = tcp_rpc("127.0.0.1", write_port, "getinfo")
        if field(later, "height") != "0":
            return fail("height grew with auto-mine off: " + later)
        pub_mine = tcp_rpc("127.0.0.1", pub_port, "mine")
        if pub_mine != "error: command disabled on public RPC":
            return fail("public mine: " + pub_mine)
        stop_node(proc)
        proc = None

        proc = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(on_dir),
                "--local-rpc-port",
                str(write_port),
                "--public-rpc",
                "--public-rpc-port",
                str(pub_port),
                "--public-rpc-bind",
                "127.0.0.1",
                "--auto-mine",
                "--auto-mine-interval",
                "1",
                "--auto-mine-reward",
                "auto_daemon",
            ],
            tmp / "on.log",
        )
        info = wait_port("127.0.0.1", write_port)
        if field(info, "auto_mine") != "on":
            return fail("enabled auto_mine: " + info)
        deadline = time.time() + 35
        height = field(info, "height")
        while time.time() < deadline and height == "0":
            time.sleep(0.5)
            height = field(tcp_rpc("127.0.0.1", write_port, "getinfo"), "height")
        if height == "0" or not height:
            return fail("auto-mine did not produce a block")
        block = tcp_rpc("127.0.0.1", write_port, "getblock " + height)
        if block.startswith("error:") or "height=" + height not in block:
            return fail("getblock after auto-mine: " + block)
        if not (on_dir / "blocks.dat").is_file():
            return fail("blocks.dat missing after auto-mine")
        pub_mine = tcp_rpc("127.0.0.1", pub_port, "mine")
        if pub_mine != "error: command disabled on public RPC":
            return fail("public mine still allowed: " + pub_mine)
        print("auto-mine off-by-default; one block when enabled; public mine rejected")
        return 0
    finally:
        stop_node(proc)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
