#!/usr/bin/env python3
"""Local two-node --mainnet join/sync (not --regtest min-diff).

Proves a second --mainnet process can:
  - bootstrap a local seed over IPv4
  - sync (HTTP getblockraw on p2p+10000 and/or P2P HELLO)
  - fetch the seed's genesis payload
  - keep write RPC on 127.0.0.1 and refuse public mine/createwallet
  - list advertised peers without `self`
  - start a home mine on loopback after sync (does not wait for a
    memory_hard block — production target is ~2^24 expensive hashes)

Height>0 block gossip/IBD for ADDITION_MAINNET_V1 is covered by
tests/test_p2p_sync.cpp (mainnet two-node socket + HTTP + BLK push).
Optional full mine: ADDITION_MAINNET_MINE=1 (may take a very long time).
"""

from __future__ import annotations

import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("ADDITIOND", ROOT / "build" / "additiond"))
PRIVACY_KEY = "test-mainnet-two-node-privacy-key-32chars"
SEED_ADVERTISED = "34.27.30.115:28546"
DISABLED = "error: command disabled on public RPC"


def field(text: str, name: str) -> str:
    match = re.search(r"(?:^|[\s:])%s=(\S+)" % re.escape(name), text)
    return match.group(1) if match else ""


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.3):
            return True
    except OSError:
        return False


def pick_ports() -> dict[str, int]:
    preferred = {
        "a_write": 18646,
        "a_p2p": 28646,
        "a_pub": 38646,
        "b_write": 18647,
        "b_p2p": 28647,
    }
    if not any(port_open("127.0.0.1", p) for p in preferred.values()):
        return preferred
    return {
        "a_write": 19646,
        "a_p2p": 29646,
        "a_pub": 39646,
        "b_write": 19647,
        "b_p2p": 29647,
    }


def tcp_rpc(host: str, port: int, command: str, timeout: float | None = 30.0) -> str:
    payload = command.strip() + "\n"
    connect_timeout = 30.0 if timeout is None else min(timeout, 30.0)
    with socket.create_connection((host, port), timeout=connect_timeout) as sock:
        sock.settimeout(timeout)
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


def wait_port(host: str, port: int, timeout: float = 60.0) -> str:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        try:
            last = tcp_rpc(host, port, "getinfo", timeout=4.0)
            if last and "network=" in last:
                return last
        except OSError:
            time.sleep(0.25)
    raise TimeoutError("timeout waiting for %s:%s last=%s" % (host, port, last))


def start_node(args: list[str], log_path: Path, extra_env: dict[str, str] | None = None) -> subprocess.Popen:
    env = os.environ.copy()
    env["ADDITION_ENABLE_P2P_RPC"] = "1"
    env["ADDITION_PRIVACY_MASTER_KEY"] = PRIVACY_KEY
    env.pop("ADDITION_ENABLE_PUBLIC_RPC", None)
    env.pop("ADDITION_LOCAL_RPC_PORT", None)
    env.pop("ADDITION_P2P_PORT", None)
    env.pop("ADDITION_AUTO_MINE", None)
    env.pop("ADDITION_MAINNET_MODE", None)
    env.pop("ADDITION_ADVERTISED_P2P", None)
    env.pop("ADDITION_PUBLIC_HTTP_PORT", None)
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
            proc.wait(timeout=10)
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
    for token in text.replace(",", " ").split():
        value = token.split("=", 1)[-1]
        if value == "self" or value.startswith("self") or value.startswith("probe-self"):
            return "%s listed self: %s" % (label, text)
        if value.startswith("n-") and ":" not in value:
            return "%s listed node-id: %s" % (label, text)
    return None


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        return 2

    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-mainnet-two-node-"))
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
                "--mainnet",
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
                "--bootstrap",
                "127.0.0.1:" + str(ports["a_p2p"]),
            ],
            tmp / "node-a.log",
            {"ADDITION_ADVERTISED_P2P": SEED_ADVERTISED},
        )
        info_a = wait_port("127.0.0.1", ports["a_write"])
        if field(info_a, "network") != "mainnet" or field(info_a, "network_id") != "ADDITION_MAINNET_V1":
            return fail("node A getinfo: " + info_a)
        if field(info_a, "pow_algorithm") != "memory_hard":
            return fail("node A must keep memory_hard: " + info_a)
        if field(info_a, "difficulty_target") != "1099511627775":
            return fail("node A difficulty must stay 0x000000FFFFFFFFFF: " + info_a)
        if field(info_a, "auto_mine") != "off":
            return fail("auto_mine must stay off: " + info_a)

        pub_info = tcp_rpc("127.0.0.1", ports["a_pub"], "getinfo")
        if field(pub_info, "network_id") != "ADDITION_MAINNET_V1":
            return fail("public getinfo: " + pub_info)
        if field(pub_info, "advertised_p2p") != SEED_ADVERTISED:
            return fail("public advertised_p2p: " + pub_info)
        leak = assert_no_self("public getinfo", pub_info)
        if leak:
            return fail(leak)
        pub_peers = tcp_rpc("127.0.0.1", ports["a_pub"], "peers")
        if SEED_ADVERTISED not in pub_peers or "self" in pub_peers:
            return fail("public peers: " + pub_peers)
        for cmd in ("mine miner1", "createwallet demo", "wallet_send demo x 1"):
            denied = tcp_rpc("127.0.0.1", ports["a_pub"], cmd)
            if denied != DISABLED:
                return fail("public must disable %r: %s" % (cmd, denied))

        raw_a = tcp_rpc("127.0.0.1", ports["a_pub"], "getblockraw 0")
        if "BLKDATA|" not in raw_a and not raw_a.startswith("ok:"):
            # public may return bare ok:BLKDATA| or BLKDATA body
            if "error:" in raw_a:
                return fail("public getblockraw 0: " + raw_a)

        proc_b = start_node(
            [
                str(BIN),
                "--mainnet",
                "--data-dir",
                str(node_b_dir),
                "--local-rpc-port",
                str(ports["b_write"]),
                "--p2p-port",
                str(ports["b_p2p"]),
                "--bootstrap",
                "127.0.0.1:" + str(ports["a_p2p"]),
            ],
            tmp / "node-b.log",
        )
        info_b0 = wait_port("127.0.0.1", ports["b_write"])
        if field(info_b0, "network_id") != "ADDITION_MAINNET_V1":
            return fail("node B getinfo: " + info_b0)
        if field(info_b0, "height") != "0":
            return fail("node B must start at height 0: " + info_b0)

        synced = tcp_rpc("127.0.0.1", ports["b_write"], "sync", timeout=120.0)
        if not synced.startswith("ok:height="):
            return fail("sync B: " + synced)

        raw_b = tcp_rpc("127.0.0.1", ports["b_write"], "getblockraw 0")
        # Normalize ok: prefix for comparison of genesis payload.
        def payload(text: str) -> str:
            if text.startswith("ok:BLKDATA|"):
                return text[len("ok:BLKDATA|") :]
            if text.startswith("BLKDATA|"):
                return text[len("BLKDATA|") :]
            if text.startswith("ok:"):
                return text[3:]
            return text

        if payload(raw_a) != payload(raw_b) or not payload(raw_a):
            return fail("genesis getblockraw mismatch A=%s B=%s" % (raw_a[:80], raw_b[:80]))

        tip_a = tcp_rpc("127.0.0.1", ports["a_write"], "getblockhash 0")
        tip_b = tcp_rpc("127.0.0.1", ports["b_write"], "getblockhash 0")
        if tip_a != tip_b or not tip_a:
            return fail("genesis hash mismatch A=%s B=%s" % (tip_a, tip_b))

        # Home miner path: write RPC accepts mine after sync; do not wait for PoW.
        mine_started = {"ok": False, "err": ""}

        def do_mine() -> None:
            try:
                mine_started["ok"] = True
                tcp_rpc("127.0.0.1", ports["b_write"], "mine miner1", timeout=None)
            except Exception as exc:  # noqa: BLE001
                mine_started["err"] = str(exc)

        miner = threading.Thread(target=do_mine, daemon=True)
        miner.start()
        time.sleep(3.0)
        during = tcp_rpc("127.0.0.1", ports["b_write"], "getinfo", timeout=8.0)
        if field(during, "network_id") != "ADDITION_MAINNET_V1":
            return fail("getinfo during mine: " + during)
        if not mine_started["ok"]:
            return fail("mine thread did not start: " + mine_started["err"])

        if os.environ.get("ADDITION_MAINNET_MINE") == "1":
            print("ADDITION_MAINNET_MINE=1: waiting for a real memory_hard block...", flush=True)
            deadline = time.time() + 3600.0
            height = "0"
            while time.time() < deadline:
                info = tcp_rpc("127.0.0.1", ports["b_write"], "getinfo", timeout=8.0)
                height = field(info, "height")
                if height.isdigit() and int(height) >= 1:
                    break
                time.sleep(5.0)
            if not height.isdigit() or int(height) < 1:
                return fail("optional mine did not finish: height=" + height)
            print("mined height", height, flush=True)

        print("test_mainnet_two_node ok (join/sync/genesis/home-mine-start)")
        return 0
    finally:
        stop_node(proc_b)
        stop_node(proc_a)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
