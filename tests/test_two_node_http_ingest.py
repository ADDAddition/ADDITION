#!/usr/bin/env python3
"""Local two-node HTTP ingest: B syncs from A over HTTP, heights match.

This is a local test only. It does not claim public 28545/38545 work.
If 28545 is filtered, ingest still uses the HTTP :80-style path
(ADDITION_PUBLIC_HTTP_PORT pointed at node A's local public-read port).
P2P is left off so HELLO on 28545 is not required.
"""

from __future__ import annotations

import http.client
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("ADDITIOND", ROOT / "build" / "additiond"))
ADVERTISED = "34.27.30.115:28545"


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.4):
            return True
    except OSError:
        return False


def pick_ports() -> dict[str, int]:
    preferred = {
        "a_write": 18545,
        "a_p2p": 19545,
        "a_pub": 18080,
        "b_write": 18546,
        "b_p2p": 19546,
    }
    if not any(port_open("127.0.0.1", p) for p in preferred.values()):
        return preferred
    return {
        "a_write": 19145,
        "a_p2p": 19147,
        "a_pub": 18180,
        "b_write": 19146,
        "b_p2p": 19148,
    }


def field(text: str, name: str) -> str:
    key = name + "="
    for part in text.replace(",", " ").split():
        if part.startswith(key):
            return part[len(key) :]
    return ""


def tcp_rpc(host: str, port: int, command: str, timeout: float = 6.0) -> str:
    payload = command.strip() + "\n"
    with socket.create_connection((host, port), timeout=timeout) as sock:
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


def wait_port(host: str, port: int, timeout: float = 45.0) -> None:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        try:
            last = tcp_rpc(host, port, "getinfo")
            if last:
                return
        except OSError:
            time.sleep(0.2)
    raise TimeoutError("timeout waiting for %s:%s last=%s" % (host, port, last))


def http_request(port: int, path: str) -> tuple[int, str]:
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=6)
    try:
        conn.request("GET", path)
        resp = conn.getresponse()
        body = resp.read().decode("utf-8", errors="replace")
        return resp.status, body
    finally:
        conn.close()


def start_node(args: list[str], log_path: Path, extra_env: dict[str, str]) -> subprocess.Popen:
    env = os.environ.copy()
    env.pop("ADDITION_ENABLE_P2P_RPC", None)
    env.pop("ADDITION_ENABLE_PUBLIC_RPC", None)
    env.pop("ADDITION_PUBLIC_RPC_PORT", None)
    env.pop("ADDITION_PUBLIC_RPC_BIND", None)
    env.pop("ADDITION_LOCAL_RPC_PORT", None)
    env.pop("ADDITION_P2P_PORT", None)
    env.pop("ADDITION_AUTO_MINE", None)
    env.pop("ADDITION_AUTO_MINE_INTERVAL", None)
    env.pop("ADDITION_AUTO_MINE_REWARD", None)
    env.pop("ADDITION_PUBLIC_HTTP_PORT", None)
    env.pop("ADDITION_ADVERTISED_P2P", None)
    env.update(extra_env)
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


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        print("build: cmake -S . -B build && cmake --build build --target additiond", file=sys.stderr)
        return 2

    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-http-ingest-"))
    node_a_dir = tmp / "node-a"
    node_b_dir = tmp / "node-b"
    node_a_dir.mkdir()
    node_b_dir.mkdir()
    proc_a = None
    proc_b = None
    try:
        proc_a = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(node_a_dir),
                "--public-rpc",
                "--public-rpc-port",
                str(ports["a_pub"]),
                "--public-rpc-bind",
                "127.0.0.1",
                "--local-rpc-port",
                str(ports["a_write"]),
                "--p2p-port",
                str(ports["a_p2p"]),
            ],
            tmp / "node-a.log",
            {"ADDITION_ADVERTISED_P2P": ADVERTISED},
        )
        proc_b = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(node_b_dir),
                "--local-rpc-port",
                str(ports["b_write"]),
                "--p2p-port",
                str(ports["b_p2p"]),
                "--bootstrap",
                "127.0.0.1:%s" % ports["a_p2p"],
            ],
            tmp / "node-b.log",
            {"ADDITION_PUBLIC_HTTP_PORT": str(ports["a_pub"])},
        )
        wait_port("127.0.0.1", ports["a_write"])
        wait_port("127.0.0.1", ports["a_pub"])
        wait_port("127.0.0.1", ports["b_write"])

        info = tcp_rpc("127.0.0.1", ports["a_pub"], "getinfo")
        if "network=testnet" not in info:
            return fail("public TCP getinfo: " + info)
        if "127.0.0.1" in info or "localhost" in info or " self" in (" " + info):
            return fail("public TCP getinfo leaked loopback/self: " + info)
        if "advertised_p2p=%s" % ADVERTISED not in info:
            return fail("public getinfo missing advertised_p2p: " + info)
        pub_peers = tcp_rpc("127.0.0.1", ports["a_pub"], "peers")
        if "127.0.0.1" in pub_peers or "localhost" in pub_peers or pub_peers == "self":
            return fail("public peers leaked loopback/self: " + pub_peers)
        if ADVERTISED not in pub_peers:
            return fail("public peers missing advertised P2P: " + pub_peers)

        status, body = http_request(ports["a_pub"], "/rpc?cmd=getinfo")
        if status != 200 or "network=testnet" not in body:
            return fail("public HTTP getinfo status=%s body=%s" % (status, body))
        if "127.0.0.1" in body:
            return fail("public HTTP getinfo leaked 127.0.0.1: " + body)
        if "advertised_p2p=%s" % ADVERTISED not in body:
            return fail("public HTTP getinfo missing advertised_p2p: " + body)

        for i in range(3):
            mined = tcp_rpc("127.0.0.1", ports["a_write"], "mine miner1", timeout=35.0)
            if mined.startswith("error:"):
                return fail("mine %s: %s" % (i + 1, mined))

        info_a = tcp_rpc("127.0.0.1", ports["a_write"], "getinfo")
        height_a = field(info_a, "height")
        if not height_a.isdigit() or int(height_a) < 3:
            return fail("node A height after mine: " + info_a)

        info_b = tcp_rpc("127.0.0.1", ports["b_write"], "getinfo")
        if field(info_b, "height") != "0":
            return fail("node B must start at height 0: " + info_b)

        add = tcp_rpc("127.0.0.1", ports["b_write"], "addpeer 127.0.0.1:%s" % ports["a_p2p"])
        if add not in {"ok", "error: invalid/duplicate peer"}:
            return fail("addpeer: " + add)

        sync = tcp_rpc("127.0.0.1", ports["b_write"], "sync", timeout=45.0)
        if sync == "ok:height=0":
            return fail("sync returned ok:height=0 while A is at height %s" % height_a)
        if not sync.startswith("ok:height="):
            return fail("sync: " + sync)
        height_b = sync.split("=", 1)[1]
        if height_b != height_a:
            return fail("B height %s != A height %s (sync=%s)" % (height_b, height_a, sync))

        print("local HTTP ingest sync:", sync)
        print("public peers listed", ADVERTISED, "(no 127.0.0.1 leak)")
        print("local ports", ports, "(not a public 28545/38545 claim)")
        return 0
    finally:
        stop_node(proc_a)
        stop_node(proc_b)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
