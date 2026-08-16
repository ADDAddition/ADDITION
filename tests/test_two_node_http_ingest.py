#!/usr/bin/env python3
"""Local two-node HELLO + HTTP ingest.

Research testnet only. Write RPC stays 127.0.0.1. This does not claim
public P2P 28545 works (that path can timeout or be filtered). HTTP
ingest is the reliable join path; HELLO is checked on localhost only.
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
SEED_ADVERTISED = "34.27.30.115:28545"
DISABLED = "error: command disabled on public RPC"


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.4):
            return True
    except OSError:
        return False


def pick_ports() -> dict[str, int]:
    # Public port is intentionally not 38545 and not p2p+10000 so the HELLO
    # phase can close HTTP without the hardcoded :38545 / p2p+10000 fallbacks.
    preferred = {
        "a_write": 19045,
        "a_p2p": 29045,
        "a_pub": 39111,
        "b_write": 19046,
        "b_p2p": 29046,
        "c_write": 19047,
        "c_p2p": 29047,
    }
    if not any(port_open("127.0.0.1", p) for p in preferred.values()):
        return preferred
    return {
        "a_write": 19145,
        "a_p2p": 29145,
        "a_pub": 39211,
        "b_write": 19146,
        "b_p2p": 29146,
        "c_write": 19147,
        "c_p2p": 29147,
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


def http_request(port: int, method: str, path: str) -> tuple[int, dict[str, str], str]:
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=6)
    try:
        conn.request(method, path)
        resp = conn.getresponse()
        headers = {k.lower(): v for k, v in resp.getheaders()}
        body = resp.read().decode("utf-8", errors="replace")
        return resp.status, headers, body
    finally:
        conn.close()


def start_node(args: list[str], log_path: Path, extra_env: dict[str, str] | None = None) -> subprocess.Popen:
    env = os.environ.copy()
    env["ADDITION_ENABLE_P2P_RPC"] = "1"
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
    if extra_env:
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


def assert_no_self(label: str, text: str) -> str | None:
    tokens = [part.split("=", 1)[-1] for part in text.replace(",", " ").split() if part]
    for token in tokens:
        if token == "self" or token.startswith("self") or token.startswith("probe-self"):
            return "%s listed self: %s" % (label, text)
        if token.startswith("n-") and ":" not in token:
            return "%s listed node-id: %s" % (label, text)
    if "127.0.0.1" in text or "localhost" in text.lower():
        return "%s listed loopback: %s" % (label, text)
    return None


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        print("build: cmake -S . -B build && cmake --build build --target additiond", file=sys.stderr)
        return 2

    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-http-ingest-"))
    node_a_dir = tmp / "node-a"
    node_b_dir = tmp / "node-b"
    node_c_dir = tmp / "node-c"
    node_a_dir.mkdir()
    node_b_dir.mkdir()
    node_c_dir.mkdir()
    proc_a = None
    proc_b = None
    proc_c = None
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
            {"ADDITION_ADVERTISED_P2P": SEED_ADVERTISED},
        )
        wait_port("127.0.0.1", ports["a_write"])
        wait_port("127.0.0.1", ports["a_pub"])

        pub_info = tcp_rpc("127.0.0.1", ports["a_pub"], "getinfo")
        if "network=testnet" not in pub_info:
            return fail("public getinfo: " + pub_info)
        if field(pub_info, "advertised_p2p") != SEED_ADVERTISED:
            return fail("public getinfo missing advertised_p2p: " + pub_info)
        if field(pub_info, "peers") != "1":
            return fail("public getinfo peers count: " + pub_info)
        leak = assert_no_self("public getinfo", pub_info)
        if leak:
            return fail(leak)

        pub_peers = tcp_rpc("127.0.0.1", ports["a_pub"], "peers")
        if pub_peers != SEED_ADVERTISED:
            return fail("public peers: " + pub_peers)
        leak = assert_no_self("public peers", pub_peers)
        if leak:
            return fail(leak)

        status, _, body = http_request(ports["a_pub"], "GET", "/rpc?cmd=getinfo")
        if status != 200 or SEED_ADVERTISED not in body or "self" in body.lower().split():
            return fail("public HTTP getinfo: %s %s" % (status, body))
        _, _, http_peers = http_request(ports["a_pub"], "GET", "/rpc?cmd=peers")
        if SEED_ADVERTISED not in http_peers or "127.0.0.1" in http_peers:
            return fail("public HTTP peers: " + http_peers)

        write_info = tcp_rpc("127.0.0.1", ports["a_write"], "getinfo")
        if "network=testnet" not in write_info:
            return fail("write getinfo: " + write_info)
        mine_pub = tcp_rpc("127.0.0.1", ports["a_pub"], "mine miner1")
        if mine_pub != DISABLED:
            return fail("public mine must stay disabled: " + mine_pub)

        for i in range(3):
            mined = tcp_rpc("127.0.0.1", ports["a_write"], "mine miner1", timeout=35.0)
            if mined.startswith("error:"):
                return fail("mine %s: %s" % (i + 1, mined))

        info_a = tcp_rpc("127.0.0.1", ports["a_write"], "getinfo")
        height_a = field(info_a, "height")
        if not height_a.isdigit() or int(height_a) < 3:
            return fail("node A height after mine: " + info_a)

        # Phase 1: HTTP ingest. Point the :80 fallback at A's public-read port.
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
        wait_port("127.0.0.1", ports["b_write"])
        info_b = tcp_rpc("127.0.0.1", ports["b_write"], "getinfo")
        if field(info_b, "height") != "0":
            return fail("node B must start at height 0: " + info_b)

        add = tcp_rpc("127.0.0.1", ports["b_write"], "addpeer 127.0.0.1:%s" % ports["a_p2p"])
        if add not in {"ok", "error: invalid/duplicate peer"}:
            return fail("addpeer B: " + add)
        trusted_peers = tcp_rpc("127.0.0.1", ports["b_write"], "peers")
        if ("127.0.0.1:%s" % ports["a_p2p"]) not in trusted_peers:
            return fail("trusted write peers must keep loopback for sync: " + trusted_peers)

        sync_http = tcp_rpc("127.0.0.1", ports["b_write"], "sync", timeout=45.0)
        if sync_http == "ok:height=0":
            return fail("HTTP ingest returned ok:height=0 while A is at height %s" % height_a)
        if not sync_http.startswith("ok:height="):
            return fail("HTTP ingest sync: " + sync_http)
        if sync_http.split("=", 1)[1] != height_a:
            return fail("HTTP ingest height %s != A %s" % (sync_http, height_a))

        # Phase 2: HELLO on localhost when HTTP ingest cannot reach A.
        proc_c = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(node_c_dir),
                "--local-rpc-port",
                str(ports["c_write"]),
                "--p2p-port",
                str(ports["c_p2p"]),
                "--bootstrap",
                "127.0.0.1:%s" % ports["a_p2p"],
            ],
            tmp / "node-c.log",
            {"ADDITION_PUBLIC_HTTP_PORT": "9"},
        )
        wait_port("127.0.0.1", ports["c_write"])
        info_c = tcp_rpc("127.0.0.1", ports["c_write"], "getinfo")
        if field(info_c, "height") != "0":
            return fail("node C must start at height 0: " + info_c)
        add_c = tcp_rpc("127.0.0.1", ports["c_write"], "addpeer 127.0.0.1:%s" % ports["a_p2p"])
        if add_c not in {"ok", "error: invalid/duplicate peer"}:
            return fail("addpeer C: " + add_c)

        sync_hello = tcp_rpc("127.0.0.1", ports["c_write"], "sync", timeout=60.0)
        if sync_hello == "ok:height=0":
            return fail("HELLO sync returned ok:height=0 while A is at height %s" % height_a)
        if not sync_hello.startswith("ok:height="):
            return fail("HELLO sync: " + sync_hello)
        if sync_hello.split("=", 1)[1] != height_a:
            return fail("HELLO height %s != A %s" % (sync_hello, height_a))

        print("http ingest:", sync_http)
        print("local hello:", sync_hello)
        print("public peers advertised", SEED_ADVERTISED, "without self")
        print("write RPC stayed 127.0.0.1; public 28545 was not claimed")
        print("ports", ports)
        return 0
    finally:
        stop_node(proc_c)
        stop_node(proc_b)
        stop_node(proc_a)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
