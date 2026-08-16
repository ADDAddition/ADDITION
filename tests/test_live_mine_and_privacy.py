#!/usr/bin/env python3
"""Live additiond: mine a real block, confirm a PQ transfer, run SHA3 opening privacy.

This talks to a running daemon. It does not claim Groth16, Bulletproofs, or ZK-Shield.
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
MINE_TIMEOUT_SEC = 30.0
PRIVACY_KEY = "addition-research-privacy-master-key-32"


def field(text: str, name: str) -> str:
    match = re.search(r"(?:^|\s)%s=(\S+)" % re.escape(name), text)
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


def start_node(args: list[str], data_dir: Path, log_path: Path) -> subprocess.Popen:
    env = os.environ.copy()
    env["ADDITION_PRIVACY_MASTER_KEY"] = PRIVACY_KEY
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


def pick_port() -> int:
    preferred = 18545
    try:
        with socket.create_connection(("127.0.0.1", preferred), timeout=0.2):
            pass
        return 19145
    except OSError:
        return preferred


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        print("build: cmake -S . -B build -DADDITION_BUILD_TESTS=ON && cmake --build build", file=sys.stderr)
        return 2

    port = pick_port()
    tmp = Path(tempfile.mkdtemp(prefix="addition-live-mine-"))
    data_dir = tmp / "node"
    data_dir.mkdir()
    proc = None
    try:
        proc = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(data_dir),
                "--local-rpc-port",
                str(port),
            ],
            data_dir,
            tmp / "node.log",
        )
        info0 = wait_port("127.0.0.1", port)
        print("CMD getinfo")
        print(info0)
        if "network=testnet" not in info0:
            return fail("getinfo network: " + info0)
        if "pow_algorithm=sha3_512" not in info0:
            return fail("expected pow_algorithm=sha3_512: " + info0)
        if "privacy_verifier=sha3_opening" not in info0:
            return fail("expected privacy_verifier=sha3_opening: " + info0)
        if field(info0, "height") != "0":
            return fail("fresh node height must be 0: " + info0)

        print("CMD createwallet alice")
        alice = tcp_rpc("127.0.0.1", port, "createwallet alice")
        print(alice)
        alice_addr = field(alice, "address")
        if not alice_addr or "algo=ml-dsa-87" not in alice:
            return fail("createwallet alice: " + alice)

        print("CMD createwallet bob")
        bob = tcp_rpc("127.0.0.1", port, "createwallet bob")
        print(bob)
        bob_addr = field(bob, "address")
        if not bob_addr:
            return fail("createwallet bob: " + bob)

        print("CMD mine", alice_addr)
        t0 = time.monotonic()
        mined = tcp_rpc("127.0.0.1", port, "mine " + alice_addr, timeout=MINE_TIMEOUT_SEC + 5.0)
        mine_ms = int((time.monotonic() - t0) * 1000)
        print(mined)
        print("mine_elapsed_ms=%s bound_ms=30000" % mine_ms)
        if not mined.startswith("mined block"):
            return fail("mine did not produce a block in %ss: %s" % (MINE_TIMEOUT_SEC, mined))
        if mine_ms > int(MINE_TIMEOUT_SEC * 1000):
            return fail("mine exceeded bounded time: %sms" % mine_ms)

        info1 = tcp_rpc("127.0.0.1", port, "getinfo")
        print("CMD getinfo")
        print(info1)
        height1 = int(field(info1, "height") or "0")
        if height1 < 1:
            return fail("height after mine: " + info1)

        print("CMD getbalance", alice_addr)
        bal_alice = tcp_rpc("127.0.0.1", port, "getbalance " + alice_addr)
        print(bal_alice)
        if bal_alice.strip() != "50" and field(bal_alice, "confirmed") != "50":
            if bal_alice.strip() != "50":
                return fail("alice coinbase balance: " + bal_alice)

        print("CMD wallet_send alice", bob_addr, "10 1")
        sent = tcp_rpc("127.0.0.1", port, "wallet_send alice %s 10 1" % bob_addr)
        print(sent)
        tx_hash = field(sent, "hash")
        if "ok:gossiped" not in sent or not tx_hash:
            return fail("wallet_send: " + sent)

        print("CMD mine", alice_addr)
        t1 = time.monotonic()
        mined2 = tcp_rpc("127.0.0.1", port, "mine " + alice_addr, timeout=MINE_TIMEOUT_SEC + 5.0)
        mine2_ms = int((time.monotonic() - t1) * 1000)
        print(mined2)
        print("mine2_elapsed_ms=%s" % mine2_ms)
        if not mined2.startswith("mined block"):
            return fail("second mine failed: " + mined2)

        print("CMD getbalance", bob_addr)
        bal_bob = tcp_rpc("127.0.0.1", port, "getbalance " + bob_addr)
        print(bal_bob)
        if bal_bob.strip() != "10":
            return fail("bob confirmed PQ transfer: " + bal_bob)

        print("CMD tx_status", tx_hash)
        status = tcp_rpc("127.0.0.1", port, "tx_status " + tx_hash)
        print(status)
        if "status=mined" not in status and "block_height=" not in status:
            return fail("tx_status not mined: " + status)

        print("CMD privacy_note_prepare 25")
        prep = tcp_rpc("127.0.0.1", port, "privacy_note_prepare 25")
        print(prep)
        trapdoor = field(prep, "trapdoor")
        commitment = field(prep, "commitment")
        nullifier = field(prep, "nullifier")
        if "verifier=sha3_opening" not in prep or not trapdoor or not commitment or not nullifier:
            return fail("privacy_note_prepare: " + prep)
        if "claim=opening_not_zk" not in prep:
            return fail("prepare must say opening_not_zk: " + prep)

        print("CMD privacy_mint_open alice 25 <cm> <nf> 00..00")
        garbage = tcp_rpc(
            "127.0.0.1",
            port,
            "privacy_mint_open alice 25 %s %s %s" % (commitment, nullifier, "00" * 32),
        )
        print(garbage)
        if "opening relation rejected" not in garbage:
            return fail("garbage trapdoor must be rejected: " + garbage)

        print("CMD privacy_mint_open alice 25 <cm> <nf> <trapdoor>")
        minted_note = tcp_rpc(
            "127.0.0.1",
            port,
            "privacy_mint_open alice 25 %s %s %s" % (commitment, nullifier, trapdoor),
        )
        print(minted_note)
        note_id = field(minted_note, "note_id")
        if not minted_note.startswith("ok:note_id=") or not note_id:
            return fail("privacy_mint_open: " + minted_note)

        print("CMD privacy_spend_open alice", note_id, "bob 10 <trapdoor>")
        spent = tcp_rpc(
            "127.0.0.1",
            port,
            "privacy_spend_open alice %s bob 10 %s" % (note_id, trapdoor),
        )
        print(spent)
        if "ok:spent" not in spent or "verifier=sha3_opening" not in spent:
            return fail("privacy_spend_open: " + spent)
        if "new_trapdoor=" not in spent or "change_trapdoor=" not in spent:
            return fail("spend must return new openings: " + spent)

        print("CMD privacy_status")
        pstatus = tcp_rpc("127.0.0.1", port, "privacy_status")
        print(pstatus)
        if "opening_verifier=sha3_opening" not in pstatus or "used_nullifiers=1" not in pstatus:
            return fail("privacy_status: " + pstatus)

        print("live mine + PQ transfer + sha3_opening privacy ok")
        print("ports", {"local_rpc": port})
        return 0
    finally:
        stop_node(proc)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
