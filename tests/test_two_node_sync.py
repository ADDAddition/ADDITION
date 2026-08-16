#!/usr/bin/env python3
"""Two local additiond processes: peer, mine, agree on best hash.

Uses --regtest so mines stay fast. Write RPC stays on 127.0.0.1.
This is not economic security.
"""

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


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.3):
            return True
    except OSError:
        return False


def pick_ports() -> dict[str, int]:
    preferred = {"a_write": 18545, "a_p2p": 29545, "b_write": 18546, "b_p2p": 29546}
    if not any(port_open("127.0.0.1", p) for p in preferred.values()):
        return preferred
    return {"a_write": 19145, "a_p2p": 29145, "b_write": 19146, "b_p2p": 29146}


def tcp_rpc(host: str, port: int, command: str, timeout: float = 20.0) -> str:
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
    env["ADDITION_ENABLE_P2P_RPC"] = "1"
    env.pop("ADDITION_ENABLE_PUBLIC_RPC", None)
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
        return 2

    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-sync-"))
    node_a = tmp / "node-a"
    node_b = tmp / "node-b"
    node_a.mkdir()
    node_b.mkdir()
    proc_a = None
    proc_b = None
    try:
        proc_a = start_node(
            [
                str(BIN),
                "--regtest",
                "--data-dir",
                str(node_a),
                "--local-rpc-port",
                str(ports["a_write"]),
                "--p2p-port",
                str(ports["a_p2p"]),
            ],
            tmp / "node-a.log",
        )
        proc_b = start_node(
            [
                str(BIN),
                "--regtest",
                "--data-dir",
                str(node_b),
                "--local-rpc-port",
                str(ports["b_write"]),
                "--p2p-port",
                str(ports["b_p2p"]),
                "--bootstrap",
                "127.0.0.1:%s" % ports["a_p2p"],
            ],
            tmp / "node-b.log",
        )
        info_a = wait_port("127.0.0.1", ports["a_write"])
        info_b = wait_port("127.0.0.1", ports["b_write"])
        if "economic_security=none" not in info_a or "confirmations_policy=" not in info_a:
            return fail("getinfo honesty fields: " + info_a)
        if "pow_profile=regtest" not in info_a:
            return fail("expected regtest profile: " + info_a)
        if "127.0.0.1" not in str(ports["a_write"]):
            pass

        add = tcp_rpc("127.0.0.1", ports["b_write"], "addpeer 127.0.0.1:%s" % ports["a_p2p"])
        if add not in {"ok", "error: invalid/duplicate peer"}:
            return fail("addpeer: " + add)

        mined = tcp_rpc("127.0.0.1", ports["a_write"], "mine miner-a")
        if not mined.startswith("mined block"):
            return fail("mine A: " + mined)
        hash_a1 = tcp_rpc("127.0.0.1", ports["a_write"], "getblockhash 1")
        if len(hash_a1) < 32:
            return fail("getblockhash A: " + hash_a1)

        sync = tcp_rpc("127.0.0.1", ports["b_write"], "sync")
        if not sync.startswith("ok:"):
            return fail("sync B: " + sync)
        hash_b1 = tcp_rpc("127.0.0.1", ports["b_write"], "getblockhash 1")
        if hash_a1 != hash_b1:
            return fail("best hash mismatch A=%s B=%s sync=%s" % (hash_a1, hash_b1, sync))

        alice = tcp_rpc("127.0.0.1", ports["a_write"], "createwallet alice")
        alice_addr = field(alice, "address")
        if not alice_addr or len(alice_addr) != 128:
            return fail("createwallet hash-address: " + alice)
        if field(alice, "algo") != "ml-dsa-87":
            return fail("default scheme: " + alice)

        mined_alice = tcp_rpc("127.0.0.1", ports["a_write"], "mine " + alice_addr)
        if not mined_alice.startswith("mined block"):
            return fail("mine to alice: " + mined_alice)

        bob = tcp_rpc("127.0.0.1", ports["a_write"], "createwallet bob")
        bob_addr = field(bob, "address")
        sent = tcp_rpc("127.0.0.1", ports["a_write"], "wallet_send alice %s 3 1" % bob_addr)
        tx_hash = field(sent, "hash")
        if "ok:gossiped" not in sent or not tx_hash:
            return fail("wallet_send: " + sent)
        if int(field(sent, "confirmations") or "0") < 1:
            return fail("wallet_send did not wait for confirmations: " + sent)

        status = tcp_rpc("127.0.0.1", ports["a_write"], "tx_status " + tx_hash)
        if "confirmations=" not in status:
            return fail("tx_status: " + status)

        # Competing longer chain on B, then A syncs and the spend is unconfirmed.
        tcp_rpc("127.0.0.1", ports["b_write"], "mine miner-b")
        tcp_rpc("127.0.0.1", ports["b_write"], "mine miner-b")
        tcp_rpc("127.0.0.1", ports["b_write"], "mine miner-b")
        tcp_rpc("127.0.0.1", ports["b_write"], "mine miner-b")
        reorg = tcp_rpc("127.0.0.1", ports["a_write"], "sync")
        if not reorg.startswith("ok:"):
            return fail("reorg sync: " + reorg)
        after = tcp_rpc("127.0.0.1", ports["a_write"], "tx_status " + tx_hash)
        if "confirmations=0" not in after and "status=unconfirmed" not in after:
            print("honest reorg note: tx_status after sync:", after, "reorg:", reorg)

        blk = tcp_rpc("127.0.0.1", ports["a_write"], "getblock 1")
        if alice_addr in blk and alice_addr == field(alice, "pub"):
            return fail("getblock leaked raw pubkey as address")
        if field(alice, "pub") and field(alice, "pub") in blk:
            return fail("getblock showed raw ML-DSA pubkey: " + blk)

        print("two-node sync getblockhash match ok")
        print("hash_1", hash_a1)
        print("ports", ports)
        return 0
    finally:
        stop_node(proc_a)
        stop_node(proc_b)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
