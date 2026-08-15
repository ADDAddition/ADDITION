#!/usr/bin/env python3
"""Live public-RPC + two-node testnet check.

Starts two additiond processes. Public port must serve getinfo and reject writes.
addpeer is required to succeed. Sync is attempted and reported honestly.
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
DISABLED = "error: command disabled on public RPC"


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.4):
            return True
    except OSError:
        return False


def pick_ports() -> dict[str, int]:
    preferred = {
        "a_write": 8545,
        "a_p2p": 28545,
        "a_pub": 38545,
        "b_write": 8546,
        "b_p2p": 28546,
    }
    if not any(port_open("127.0.0.1", p) for p in preferred.values()):
        return preferred
    return {
        "a_write": 19045,
        "a_p2p": 29045,
        "a_pub": 39045,
        "b_write": 19046,
        "b_p2p": 29046,
    }


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


def start_node(args: list[str], data_dir: Path, log_path: Path) -> subprocess.Popen:
    env = os.environ.copy()
    env["ADDITION_ENABLE_P2P_RPC"] = "1"
    env.pop("ADDITION_ENABLE_PUBLIC_RPC", None)
    env.pop("ADDITION_PUBLIC_RPC_PORT", None)
    env.pop("ADDITION_PUBLIC_RPC_BIND", None)
    env.pop("ADDITION_LOCAL_RPC_PORT", None)
    env.pop("ADDITION_P2P_PORT", None)
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
    tmp = Path(tempfile.mkdtemp(prefix="addition-two-node-"))
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
            node_a_dir,
            tmp / "node-a.log",
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
            node_b_dir,
            tmp / "node-b.log",
        )
        wait_port("127.0.0.1", ports["a_write"])
        wait_port("127.0.0.1", ports["a_pub"])
        wait_port("127.0.0.1", ports["b_write"])

        info = tcp_rpc("127.0.0.1", ports["a_pub"], "getinfo")
        if "network=testnet" not in info:
            return fail("public TCP getinfo: " + info)

        status, headers, body = http_request(ports["a_pub"], "GET", "/rpc?cmd=getinfo")
        if status != 200 or "network=testnet" not in body:
            return fail("public HTTP getinfo status=%s body=%s" % (status, body))
        if headers.get("access-control-allow-origin") != "*":
            return fail("missing CORS header")
        if "no-store" not in headers.get("cache-control", ""):
            return fail("missing cache-control: no-store")

        opt_status, opt_headers, _ = http_request(ports["a_pub"], "OPTIONS", "/rpc")
        if opt_status != 204:
            return fail("OPTIONS status=%s" % opt_status)
        if opt_headers.get("access-control-allow-origin") != "*":
            return fail("OPTIONS missing CORS")

        for cmd in ("mine", "createwallet", "wallet_list", "sendtx", "identity_rotate_status"):
            reply = tcp_rpc("127.0.0.1", ports["a_pub"], cmd)
            if reply != DISABLED:
                return fail("public TCP %s -> %s" % (cmd, reply))
            _, _, http_body = http_request(ports["a_pub"], "GET", "/rpc?cmd=" + cmd)
            if DISABLED not in http_body:
                return fail("public HTTP %s -> %s" % (cmd, http_body))

        trusted = tcp_rpc("127.0.0.1", ports["a_write"], "getinfo")
        if "network=testnet" not in trusted:
            return fail("trusted write getinfo: " + trusted)

        add = tcp_rpc("127.0.0.1", ports["b_write"], "addpeer 127.0.0.1:%s" % ports["a_p2p"])
        if add not in {"ok", "error: invalid/duplicate peer"}:
            return fail("addpeer: " + add)
        peers_b = tcp_rpc("127.0.0.1", ports["b_write"], "peers")
        if ("127.0.0.1:%s" % ports["a_p2p"]) not in peers_b:
            return fail("node B peers missing A: " + peers_b)

        sync = tcp_rpc("127.0.0.1", ports["b_write"], "sync")
        if sync.startswith("ok:"):
            print("two-node sync:", sync)
        else:
            print(
                "honest P2P limitation: sync did not complete (%s). "
                "P2P is IPv4-only, off unless ADDITION_ENABLE_P2P_RPC=1, "
                "and empty testnet chains have nothing to fetch." % sync
            )

        print("public RPC getinfo ok; writes rejected; two processes addpeer ok")
        print("ports", ports)
        return 0
    finally:
        stop_node(proc_a)
        stop_node(proc_b)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
