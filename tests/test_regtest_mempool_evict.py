#!/usr/bin/env python3
"""Regression: a block received over P2P must evict its txs from peer mempools.

Three local --regtest nodes (127.0.0.1 only). Node b sends a tx, it gossips to
every mempool, node a mines it, and all three must then report status=mined
with an empty mempool (previously b/c kept it as status=mempool forever).
"""
from __future__ import annotations

import shutil
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_regtest_staking_three_node import (  # noqa: E402
    BIN, fail, field, pick_ports, start_node, stop_node, tcp_rpc, wait_port)


def main() -> int:
    if not Path(BIN).exists():
        return fail("additiond not found: %s" % BIN)
    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-evict-"))
    procs, rpc = {}, {}
    try:
        for n in "abc":
            (tmp / n).mkdir()
            a = [str(BIN), "--regtest", "--data-dir", str(tmp / n), "--p2p-bind", "127.0.0.1",
                 "--local-rpc-port", str(ports[n + "_write"]), "--p2p-port", str(ports[n + "_p2p"])]
            for o in "abc":
                if o != n:
                    a += ["--bootstrap", "127.0.0.1:%s" % ports[o + "_p2p"]]
            procs[n] = start_node(a, tmp / (n + ".log"))
            rpc[n] = (lambda p: (lambda c: tcp_rpc("127.0.0.1", p, c)))(ports[n + "_write"])
        for n in "abc":
            wait_port("127.0.0.1", ports[n + "_write"])
        addr = {n: field(rpc[n]("createwallet w" + n), "address") for n in "abc"}

        def sync() -> None:
            for n in "abc":
                rpc[n]("gossip_flush")
                rpc[n]("sync")

        def all_status(h: str, want: str) -> bool:
            for _ in range(40):
                if all(("status=" + want) in rpc[n]("tx_status " + h) for n in "abc"):
                    return True
                sync()
                time.sleep(0.25)
            return False

        for n in "abc":
            if not rpc[n]("mine " + addr[n]).startswith("mined block"):
                return fail("premine " + n)
            sync()
        sent = rpc["b"]("wallet_send wb %s 1 1" % addr["c"])
        if not sent.startswith("ok:"):
            return fail("wallet_send: " + sent)
        h = field(sent, "hash")
        if not all_status(h, "mempool"):
            return fail("tx did not gossip to every mempool")
        if not rpc["a"]("mine " + addr["a"]).startswith("mined block"):
            return fail("mine on a")
        sync()
        if not all_status(h, "mined"):
            return fail("tx not mined on every node: %s" % [rpc[n]("tx_status " + h)[:40] for n in "abc"])
        for n in "abc":
            if field(rpc[n]("getinfo"), "mempool") != "0":
                return fail("mempool not empty on %s" % n)
        # Next block from another node must still connect (no stale tx in its template).
        if not rpc["b"]("mine " + addr["b"]).startswith("mined block"):
            return fail("mine on b after eviction")
        print("mempool eviction on P2P block: ok")
        return 0
    finally:
        for p in procs.values():
            stop_node(p)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
