#!/usr/bin/env python3
"""Local three-node --regtest hybrid PoW/PoS staking: stake, relay votes, quorum, unstake.

Write RPC stays 127.0.0.1. This is not economic security, not ZK, not
cross-chain, and not a mainnet claim. Public live RPC stays testnet.
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
    for base in (18645, 19245, 19845):
        cand = {}
        for i, n in enumerate("abc"):
            cand[n + "_write"] = base + i
            cand[n + "_p2p"] = base + 11000 + i
        if not any(port_open("127.0.0.1", p) for p in cand.values()):
            return cand
    raise RuntimeError("no free port block")


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
    env.pop("ADDITION_AUTO_MINE", None)
    env.pop("ADDITION_MAINNET_MODE", None)
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



STAKING_TOML = """
staking_activation_height = 0
stake_min_amount = 10
stake_maturity = 2
stake_unbond_delay = 3
stake_votes_per_block = 3
stake_vote_quorum = 2
stake_min_live_tickets = 3
stake_reward_bps = 3000
"""


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        return 2
    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-regtest-stake-"))
    cfg = tmp / "staking.toml"
    cfg.write_text(STAKING_TOML, encoding="utf-8")
    procs = {}
    rpc = {}
    try:
        for n in "abc":
            d = tmp / ("node-" + n)
            d.mkdir()
            args = [str(BIN), "--regtest", "--config", str(cfg), "--data-dir", str(d),
                    "--local-rpc-port", str(ports[n + "_write"]), "--p2p-port", str(ports[n + "_p2p"])]
            if n != "a":
                args += ["--bootstrap", "127.0.0.1:%s" % ports["a_p2p"]]
            procs[n] = start_node(args, tmp / ("node-%s.log" % n))
            rpc[n] = (lambda port: (lambda c: tcp_rpc("127.0.0.1", port, c)))(ports[n + "_write"])
        for n in "abc":
            wait_port("127.0.0.1", ports[n + "_write"])
        for src_n, dst in (("b", "a"), ("c", "a"), ("b", "c"), ("c", "b"), ("a", "b"), ("a", "c")):
            rpc[src_n]("addpeer 127.0.0.1:%s" % ports[dst + "_p2p"])

        info = rpc["a"]("stake_info")
        if "active=1" not in info and "active=true" not in info:
            return fail("staking should be active via toml: " + info)

        def sync_all() -> None:
            for n in "bc":
                rpc[n]("sync")

        def flush_all() -> None:
            for n in "abc":
                rpc[n]("gossip_flush")

        def mine() -> str:
            # Votes travel over P2P gossip; a real miner waits for quorum, so retry briefly.
            out = ""
            for _ in range(30):
                if voting[0]:
                    sync_all()
                flush_all()
                out = rpc["a"]("mine " + addr["a"])
                if "insufficient stake votes" not in out or not voting[0]:
                    break
                time.sleep(0.5)
            sync_all()
            return out

        addr = {}
        voting = [False]
        for n, w in (("a", "alice"), ("b", "bob"), ("c", "carol")):
            addr[n] = field(rpc[n]("createwallet " + w), "address")
            if not addr[n]:
                return fail("createwallet " + w)
        for _ in range(3):
            if not mine().startswith("mined block"):
                return fail("premine")
        for n in "bc":
            sent = rpc["a"]("wallet_send alice %s 40 1" % addr[n])
            if "ok:" not in sent:
                return fail("fund %s: %s" % (n, sent))
            if not mine().startswith("mined block"):
                return fail("fund mine")
        for n, w in (("a", "alice"), ("b", "bob"), ("c", "carol")):
            bal = rpc[n]("getbalance " + addr[n])
            out = rpc[n]("stake %s 20 1" % w)
            if not out.startswith("ok:stake_lock"):
                return fail("stake %s (bal %s): %s" % (w, bal, out))
        # Lock txs gossip from B/C to A; allow a few blocks for propagation.
        for _ in range(5):
            out = mine()
            tickets = rpc["a"]("stake_tickets")
            if tickets.count("ticket=") >= 3:
                break
            time.sleep(0.5)  # gossip_flush rebroadcasts pending mempool txs
        if tickets.count(addr["a"][:12]) + tickets.count(addr["b"][:12]) + tickets.count(addr["c"][:12]) < 3:
            return fail("expected 3 tickets after mine (%s): %s" % (out, tickets))
        for n in "bc":
            if not any(t in rpc[n]("staked " + addr[n]) for t in ("10", "20", "30")):
                return fail("staked on %s: %s" % (n, rpc[n]("staked " + addr[n])))
        for _ in range(2):
            if not mine().startswith("mined block"):
                return fail("maturity mine")
        sel = rpc["a"]("stake_selected")
        # Without votes, quorum blocks the miner (auto-vote off everywhere).
        blocked = rpc["a"]("mine " + addr["a"])
        if "insufficient stake votes" not in blocked:
            return fail("expected quorum failure without votes: %s (selected %s)" % (blocked, sel))
        for n, w in (("a", "alice"), ("b", "bob"), ("c", "carol")):
            out = rpc[n]("stake_autovote on " + w)
            if not out.startswith("ok"):
                return fail("autovote %s: %s" % (w, out))
        voting[0] = True
        before_b = rpc["b"]("getbalance " + addr["b"])
        voted_blocks = 0
        for _ in range(3):
            out = mine()
            if not out.startswith("mined block"):
                return fail("mine with relayed votes: %s pool=%s" % (out, rpc["a"]("stake_info")))
            voted_blocks += 1
        after_b = rpc["b"]("getbalance " + addr["b"])
        if before_b == after_b:
            return fail("bob earned no vote reward: %s -> %s" % (before_b, after_b))
        h = field(rpc["a"]("getinfo"), "height")
        hashes = {rpc[n]("getblockhash " + h) for n in "abc"}
        if len(hashes) != 1:
            return fail("tip mismatch at %s: %s" % (h, hashes))
        # Unstake carol's ticket from node C (signed locally there).
        tid = ""
        for tok in rpc["c"]("stake_tickets " + addr["c"]).split():
            if ":" in tok and len(tok) > 20:
                tid = tok.split("=", 1)[-1]
                break
        if not tid:
            return fail("carol ticket id: " + rpc["c"]("stake_tickets " + addr["c"]))
        diag = []
        out = rpc["c"]("unstake carol %s 1" % tid)
        if not out.startswith("ok:stake_unlock"):
            return fail("unstake: " + out)
        for _ in range(5):
            out = mine()
            if not out.startswith("mined block"):
                return fail("mine unstake: %s info=%s" % (out, rpc["a"]("stake_info")))
            if "20" not in rpc["c"]("staked " + addr["c"]).split("=")[-1]:
                break
            # Tx gossip is one-shot best effort; re-announce if A never saw it.
            diag.append(rpc["c"]("unstake carol %s 1" % tid)[:120])
            diag.append("flush=" + rpc["c"]("gossip_flush")[:120])
        if "20" in rpc["c"]("staked " + addr["c"]).split("=")[-1]:
            return fail("carol still staked: %s diag=%s tickets_a=%s" % (rpc["c"]("staked " + addr["c"]), diag, rpc["a"]("stake_tickets " + addr["c"])[:300]))
        print("regtest three-node staking ok: blocks_with_votes=%d bob %s -> %s height=%s" % (voted_blocks, before_b, after_b, h))
        return 0
    finally:
        for p in procs.values():
            stop_node(p)
        if os.environ.get("KEEP_TMP"):
            print("kept", tmp)
        else:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
