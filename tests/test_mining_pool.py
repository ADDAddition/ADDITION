#!/usr/bin/env python3
"""Tests for ADDITION local mining pool coordinator (not NiceHash)."""

from __future__ import annotations

import importlib.util
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
POOL = ROOT / "tools" / "mining_pool.py"


def load_pool():
    spec = importlib.util.spec_from_file_location("mining_pool", POOL)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    import sys

    sys.modules["mining_pool"] = mod
    spec.loader.exec_module(mod)
    return mod


class MiningPoolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.m = load_pool()

    def test_loopback_required(self) -> None:
        self.assertTrue(self.m.is_loopback_host("127.0.0.1"))
        self.assertFalse(self.m.is_loopback_host("0.0.0.0"))
        self.assertFalse(self.m.is_loopback_host("34.27.30.115"))
        with self.assertRaises(SystemExit):
            self.m.require_loopback("0.0.0.0", "pool bind")

    def test_refuse_public_and_lan_mine_ports(self) -> None:
        for port in (38545, 38546, 18545, 18547, 28545):
            with self.assertRaises(SystemExit):
                self.m.assert_safe_mine_target("127.0.0.1", port)
        with self.assertRaises(SystemExit):
            self.m.assert_safe_mine_target("34.27.30.115", 8545)
        # Allowed desktop write ports
        self.m.assert_safe_mine_target("127.0.0.1", 8545)
        self.m.assert_safe_mine_target("127.0.0.1", 8546)
        self.m.assert_safe_mine_target("127.0.0.1", 8547)

    def test_disclaimer_honest(self) -> None:
        low = self.m.DISCLAIMER.lower()
        self.assertIn("not nicehash", low)
        self.assertIn("loopback", low)
        self.assertNotIn("mainnet is live", low)

    def test_share_is_completed_attempt(self) -> None:
        state = self.m.PoolState("127.0.0.1", 8545, "miner1")

        def fake_rpc(host, port, command, timeout=120.0):
            if command.startswith("mine"):
                return "mined block 1 reward=miner1 threads=2 hash=abc"
            if command == "getinfo":
                return "network=testnet height=1 peers=0 mempool=0 pow_algorithm=sha3_512 last_mine_ms=12"
            return "error: unexpected %s" % command

        with mock.patch.object(self.m, "tcp_rpc", side_effect=fake_rpc):
            state.start()
            w = state.register("w1", 2)
            job = state.request_slot(w.worker_id, None, 2)
            for _ in range(50):
                with state.lock:
                    if state.jobs[job.job_id].done:
                        break
                time.sleep(0.05)
            with state.lock:
                done_job = state.jobs[job.job_id]
                self.assertTrue(done_job.done)
                self.assertTrue(done_job.ok)
                self.assertEqual(state.shares_total, 1)
                self.assertEqual(state.blocks_found, 1)
                self.assertEqual(state.workers[w.worker_id].shares, 1)
                self.assertEqual(state.workers[w.worker_id].blocks, 1)
            st = state.status()
            self.assertFalse(st["nicehash"])
            self.assertFalse(st["public_mine"])
            self.assertEqual(st["kind"], "local_coordinator")
            self.assertEqual(st["network"], "testnet")
            state.shutdown()

    def test_serialized_queue_does_not_overlap_mine(self) -> None:
        state = self.m.PoolState("127.0.0.1", 8545, "miner1")
        concurrent = {"n": 0, "max": 0}
        lock = threading.Lock()

        def fake_rpc(host, port, command, timeout=120.0):
            if command.startswith("mine"):
                with lock:
                    concurrent["n"] += 1
                    concurrent["max"] = max(concurrent["max"], concurrent["n"])
                time.sleep(0.08)
                with lock:
                    concurrent["n"] -= 1
                return "error: deadline"
            if command == "getinfo":
                return "network=testnet height=0 peers=0"
            return "ok"

        with mock.patch.object(self.m, "tcp_rpc", side_effect=fake_rpc):
            state.start()
            w = state.register("w", 1)
            ids = [state.request_slot(w.worker_id, None, 1).job_id for _ in range(3)]
            for _ in range(100):
                with state.lock:
                    if all(state.jobs[j].done for j in ids):
                        break
                time.sleep(0.05)
            self.assertEqual(concurrent["max"], 1)
            self.assertEqual(state.shares_total, 3)
            state.shutdown()

    def test_mainnet_height_flag_from_getinfo_only(self) -> None:
        state = self.m.PoolState("127.0.0.1", 8546, "miner1")
        with mock.patch.object(
            self.m,
            "tcp_rpc",
            return_value="network=mainnet height=0 peers=0",
        ):
            st = state.status()
        self.assertFalse(st["mainnetHasBlocks"])
        self.assertEqual(st["height"], 0)
        with mock.patch.object(
            self.m,
            "tcp_rpc",
            return_value="network=mainnet height=2 peers=0",
        ):
            st2 = state.status()
        self.assertTrue(st2["mainnetHasBlocks"])


if __name__ == "__main__":
    unittest.main()
