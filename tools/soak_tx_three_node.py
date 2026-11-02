#!/usr/bin/env python3
"""Local three-node --regtest soak with transactions (loopback only, throwaway wallets).

Each node mines in turn to its own wallet, then every round each node sends a
transaction to the next node's wallet. The soak checks, per transaction:
  * gossip: the tx reaches every peer mempool (tx_status=mempool) before mining,
  * inclusion: after the next block it is status=mined on all three nodes,
and per round that all three nodes agree on height and tip hash.

Not a public network, not mainnet. Never binds outside 127.0.0.1.
Usage: tools/soak_tx_three_node.py [--minutes 20] [--interval 20]
Exit 0 only if every tx gossiped + was included and tips never diverged.
"""
from __future__ import annotations

import argparse
import json
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from test_regtest_staking_three_node import (  # noqa: E402
    BIN, field, pick_ports, start_node, stop_node, tcp_rpc, wait_port)

NODES = "abc"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=float, default=20.0)
    ap.add_argument("--interval", type=float, default=20.0, help="seconds per block")
    ap.add_argument("--report", default="")
    ap.add_argument("--debug", action="store_true")
    args = ap.parse_args()

    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-soak-tx-"))
    procs: dict = {}
    rpc = {n: (lambda p: (lambda c: tcp_rpc("127.0.0.1", p, c)))(ports[n + "_write"]) for n in NODES}
    stats = {"blocks": 0, "tx_sent": 0, "tx_gossip_ok": 0, "tx_included_all": 0,
             "tx_send_errors": 0, "tip_divergence": 0, "gossip_ms": [], "failures": []}
    try:
        for n in NODES:
            d = tmp / ("node-" + n)
            d.mkdir()
            a = [str(BIN), "--regtest", "--data-dir", str(d), "--p2p-bind", "127.0.0.1",
                 "--local-rpc-port", str(ports[n + "_write"]), "--p2p-port", str(ports[n + "_p2p"])]
            for o in NODES:
                if o != n:
                    a += ["--bootstrap", "127.0.0.1:%s" % ports[o + "_p2p"]]
            procs[n] = start_node(a, tmp / ("node-%s.log" % n))
        for n in NODES:
            wait_port("127.0.0.1", ports[n + "_write"])
        addr = {}
        for n, w in zip(NODES, ("alice", "bob", "carol")):
            addr[n] = field(rpc[n]("createwallet " + w), "address")
            if not addr[n]:
                raise RuntimeError("createwallet failed on " + n)
        wallets = dict(zip(NODES, ("alice", "bob", "carol")))

        def heights():
            return [int(field(rpc[n]("getinfo"), "height") or -1) for n in NODES]

        def sync():
            for n in NODES:
                rpc[n]("gossip_flush")
                rpc[n]("sync")

        def wait_status(h, want, timeout=15.0):
            end = time.time() + timeout
            while time.time() < end:
                if all(("status=" + want) in rpc[n]("tx_status " + h) for n in NODES):
                    return True
                sync()
                time.sleep(0.25)
            return False

        t0 = time.time()
        rnd = 0
        pending: list[str] = []
        while time.time() - t0 < args.minutes * 60:
            start = time.time()
            miner = NODES[rnd % 3]
            out = rpc[miner]("mine " + addr[miner])
            if not out.startswith("mined block"):
                stats["failures"].append("mine %s: %s" % (miner, out[:160]))
            else:
                stats["blocks"] += 1
                if "--debug" in sys.argv: print(out[:300], rpc[miner]("getinfo")[:120])
            sync()
            for h in pending:
                if wait_status(h, "mined"):
                    stats["tx_included_all"] += 1
                else:
                    stats["failures"].append("not mined everywhere: %s %s" % (h[:16], [rpc[n]("tx_status " + h)[:40] for n in NODES]))
            pending = []
            hs = heights()
            m = min(hs)
            tips = {rpc[n]("getblockhash %d" % m).strip() for n in NODES}
            if len(tips) != 1:
                stats["tip_divergence"] += 1
            for i, n in enumerate(NODES):
                to = addr[NODES[(i + 1) % 3]]
                r = rpc[n]("wallet_send %s %s 1 1" % (wallets[n], to))
                if not r.startswith("ok:"):
                    # Coinbase rewards must mature first; count but don't fail early rounds.
                    stats["tx_send_errors"] += 1
                    continue
                h = field(r, "hash")
                stats["tx_sent"] += 1
                g0 = time.time()
                if wait_status(h, "mempool"):
                    stats["tx_gossip_ok"] += 1
                    stats["gossip_ms"].append(int((time.time() - g0) * 1000))
                    pending.append(h)
                else:
                    stats["failures"].append("gossip timeout: " + h)
            print("t=%ds round=%d heights=%s tips_agree=%s sent=%d gossip_ok=%d included=%d send_err=%d"
                  % (time.time() - t0, rnd, hs, len(tips) == 1, stats["tx_sent"], stats["tx_gossip_ok"],
                     stats["tx_included_all"], stats["tx_send_errors"]), flush=True)
            rnd += 1
            time.sleep(max(0.0, args.interval - (time.time() - start)))
        # final block to include the last batch
        rpc[NODES[rnd % 3]]("mine " + addr[NODES[rnd % 3]])
        sync()
        for h in pending:
            if wait_status(h, "mined"):
                stats["tx_included_all"] += 1
            else:
                stats["failures"].append("not mined everywhere: " + h)
        g = stats.pop("gossip_ms")
        stats["gossip_ms_max"] = max(g) if g else None
        stats["gossip_ms_avg"] = int(sum(g) / len(g)) if g else None
        stats["final_heights"] = heights()
        stats["logs"] = str(tmp)
    finally:
        for p in procs.values():
            stop_node(p)
    ok = (not stats["failures"] and stats["tip_divergence"] == 0 and stats["tx_sent"] > 0
          and stats["tx_included_all"] == stats["tx_sent"] == stats["tx_gossip_ok"])
    stats["result"] = "PASS" if ok else "FAIL"
    print(json.dumps(stats, indent=2))
    if args.report:
        Path(args.report).write_text(json.dumps(stats, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
