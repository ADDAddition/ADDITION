#!/usr/bin/env python3
"""Local additiond smoke: mine, PQ send, opening privacy, clean benchmark, two-node sync.

Reports only fields the node printed. Does not claim a live mainnet, ZK circuit, or 100000 TPS.
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


def start_node(args: list[str], data_dir: Path, log_path: Path) -> subprocess.Popen:
    env = os.environ.copy()
    env["ADDITION_PRIVACY_MASTER_KEY"] = PRIVACY_KEY
    env["ADDITION_ENABLE_P2P_RPC"] = "1"
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


def pick_ports() -> dict[str, int]:
    preferred = {"a_write": 19145, "a_p2p": 29145, "b_write": 19146, "b_p2p": 29146}

    def busy(port: int) -> bool:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return True
        except OSError:
            return False

    if not any(busy(p) for p in preferred.values()):
        return preferred
    return {"a_write": 19245, "a_p2p": 29245, "b_write": 19246, "b_p2p": 29246}


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        print("build: cmake -S . -B build -DADDITION_BUILD_TESTS=ON && cmake --build build", file=sys.stderr)
        return 2

    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-core-path-"))
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
                "--local-rpc-port",
                str(ports["a_write"]),
                "--p2p-port",
                str(ports["a_p2p"]),
                "--bootstrap",
                "127.0.0.1:%s" % ports["a_p2p"],
            ],
            node_a_dir,
            tmp / "node-a.log",
        )
        info0 = wait_port("127.0.0.1", ports["a_write"])
        print("CMD getinfo")
        print(info0)
        if "network=testnet" not in info0 or "pow_algorithm=sha3_512" not in info0:
            return fail("getinfo: " + info0)
        if "privacy_claim=opening_not_zk" not in info0:
            return fail("getinfo must claim opening_not_zk: " + info0)

        delpeer = tcp_rpc("127.0.0.1", ports["a_write"], "delpeer 127.0.0.1:%s" % ports["a_p2p"])
        print("CMD delpeer", delpeer)
        sync_none = tcp_rpc("127.0.0.1", ports["a_write"], "sync")
        print("CMD sync (no peer)", sync_none)
        if "error: no peer" not in sync_none:
            return fail("sync without peer: " + sync_none)

        alice = tcp_rpc("127.0.0.1", ports["a_write"], "createwallet alice")
        bob = tcp_rpc("127.0.0.1", ports["a_write"], "createwallet bob")
        alice_addr = field(alice, "address")
        bob_addr = field(bob, "address")
        if not alice_addr or not bob_addr or "algo=ml-dsa-87" not in alice:
            return fail("wallets: %s / %s" % (alice, bob))

        mined = tcp_rpc("127.0.0.1", ports["a_write"], "mine " + alice_addr, timeout=MINE_TIMEOUT_SEC + 5.0)
        print("CMD mine", mined)
        if not mined.startswith("mined block"):
            return fail("mine: " + mined)

        sent = tcp_rpc("127.0.0.1", ports["a_write"], "wallet_send alice %s 10 1" % bob_addr)
        print("CMD wallet_send", sent)
        if "ok:gossiped" not in sent:
            return fail("wallet_send: " + sent)
        mined2 = tcp_rpc("127.0.0.1", ports["a_write"], "mine " + alice_addr, timeout=MINE_TIMEOUT_SEC + 5.0)
        print("CMD mine2", mined2)
        if not mined2.startswith("mined block"):
            return fail("second mine: " + mined2)
        bal_bob = tcp_rpc("127.0.0.1", ports["a_write"], "getbalance " + bob_addr)
        if bal_bob.strip() != "10":
            return fail("bob PQ transfer: " + bal_bob)

        prep = tcp_rpc("127.0.0.1", ports["a_write"], "privacy_note_prepare 25")
        trapdoor = field(prep, "trapdoor")
        commitment = field(prep, "commitment")
        nullifier = field(prep, "nullifier")
        if "claim=opening_not_zk" not in prep or not trapdoor:
            return fail("privacy_note_prepare: " + prep)
        minted_note = tcp_rpc(
            "127.0.0.1",
            ports["a_write"],
            "privacy_mint_open alice 25 %s %s %s" % (commitment, nullifier, trapdoor),
        )
        note_id = field(minted_note, "note_id")
        if not minted_note.startswith("ok:note_id=") or not note_id:
            return fail("privacy_mint_open: " + minted_note)
        spent = tcp_rpc(
            "127.0.0.1",
            ports["a_write"],
            "privacy_spend_open alice %s bob 10 %s" % (note_id, trapdoor),
        )
        print("CMD privacy_spend_open", spent)
        if "ok:spent" not in spent or "claim=opening_not_zk" not in spent:
            return fail("privacy_spend_open: " + spent)

        bench = tcp_rpc("127.0.0.1", ports["a_write"], "benchmark_objective 1 2", timeout=60.0)
        print("CMD benchmark_objective", bench)
        if "bench_submitted=0" not in bench or "bench_verify_ok=2" not in bench:
            return fail("benchmark: " + bench)
        if "objective_tps_ok" in bench:
            return fail("benchmark treated 100000 TPS as a fact: " + bench)
        if "research_goal_is_not_a_measurement=true" not in bench:
            return fail("benchmark missing research-goal label: " + bench)

        mined3 = tcp_rpc("127.0.0.1", ports["a_write"], "mine " + alice_addr, timeout=MINE_TIMEOUT_SEC + 5.0)
        print("CMD mine after benchmark", mined3)
        if not mined3.startswith("mined block"):
            return fail("mine after benchmark: " + mined3)

        status = tcp_rpc("127.0.0.1", ports["a_write"], "protocol_status")
        print("CMD protocol_status", status)
        if "measured_last_mine_ms=" not in status or "privacy_claim=opening_not_zk" not in status:
            return fail("protocol_status: " + status)
        if "objective_tps_ok" in status or "objective_100_ok" in status:
            return fail("protocol_status overclaim: " + status)

        info_a = tcp_rpc("127.0.0.1", ports["a_write"], "getinfo")
        height_a = field(info_a, "height")
        print("measured getinfo", info_a)
        if not height_a.isdigit() or int(height_a) < 3:
            return fail("node A height: " + info_a)

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
        wait_port("127.0.0.1", ports["b_write"])
        add = tcp_rpc("127.0.0.1", ports["b_write"], "addpeer 127.0.0.1:%s" % ports["a_p2p"])
        if add not in {"ok", "error: invalid/duplicate peer"}:
            return fail("addpeer: " + add)
        sync = tcp_rpc("127.0.0.1", ports["b_write"], "sync", timeout=45.0)
        print("CMD sync", sync)
        if sync == "ok:height=0":
            return fail("sync ok:height=0 while A is at %s" % height_a)
        if not sync.startswith("ok:height="):
            return fail("sync: " + sync)
        height_b = field(sync, "height") if "height=" in sync else sync.split("=", 1)[1]
        if height_b != height_a:
            return fail("B height %s != A height %s (sync=%s)" % (height_b, height_a, sync))

        print("core path smoke ok")
        print(
            "measured",
            {
                "height": height_a,
                "last_mine_ms": field(info_a, "last_mine_ms"),
                "last_verify_per_sec": field(info_a, "last_verify_per_sec"),
                "bench_verify_per_sec": field(bench, "bench_verify_per_sec"),
                "bench_mine_ms": field(bench, "bench_mine_ms"),
            },
        )
        return 0
    finally:
        stop_node(proc_b)
        stop_node(proc_a)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
