#!/usr/bin/env python3
"""Mocked TEXT RPC tests for the local ADDITION testnet wallet client."""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
if str(WEB) not in sys.path:
    sys.path.insert(0, str(WEB))

from addition_wallet import (  # noqa: E402
    WalletClient,
    WalletError,
    WalletRecord,
    WalletStore,
    TextRpcClient,
    derive_address,
    assert_loopback_host,
)


SECRET = "aa" * 32
PUB = "bb" * 32
TO_ADDR = "cc" * 20


def fake_record() -> WalletRecord:
    return WalletRecord(
        network="testnet",
        algorithm="ml-dsa-87",
        address=derive_address(PUB),
        public_key=PUB,
        private_key=SECRET,
        next_nonce=1,
    )


class FakeNode:
    def __init__(self) -> None:
        self.balance = "0"
        self.nonce_fail_once = False
        self.calls = []

    def __call__(self, wire: str) -> str:
        self.calls.append(wire)
        if SECRET in wire:
            raise AssertionError("private key appeared on the TEXT RPC line")
        parts = wire.split()
        cmd = parts[0] if parts else ""
        if cmd == "getinfo":
            return (
                "network=testnet network_name=addition-testnet height=1 peers=0 "
                "pq_mode=strict max_supply=50000000"
            )
        if cmd == "fee_info":
            return "base_min_fee=1 recommended_min_fee=1"
        if cmd == "getbalance":
            return self.balance
        if cmd == "tx_build":
            if self.nonce_fail_once and parts[-1] == "1":
                return "error: nonce replay or out-of-order"
            return "sign_hash=abc123def456"
        if cmd == "sendtx_signed_hash":
            if self.nonce_fail_once and parts[-2] == "1":
                self.nonce_fail_once = False
                return "error: nonce replay or out-of-order"
            return "deadbeef" * 8
        if cmd == "mine":
            self.balance = "50"
            return f"mined block 1 reward={parts[1]} hash=00ff"
        if cmd in {"sendtx", "sendtx_hash", "sign_message"}:
            raise AssertionError(f"insecure command used: {cmd}")
        return "error: unknown command"


class WalletClientTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory()
        self.wallet_path = Path(self.tmp.name) / "addition.wallet"
        self.node = FakeNode()
        self.rpc = TextRpcClient(transport=self.node)
        self.client = WalletClient(
            rpc=self.rpc,
            store=WalletStore(self.wallet_path),
            keygen=fake_record,
            signer=lambda _sk, message: ("s1g" + message.hex())[:32],
        )

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def test_address_derivation_matches_node_formula(self) -> None:
        # SHA3-512(scheme_id || 0x00 || pubkey_bytes), 128 hex — same as src/crypto.cpp
        addr = derive_address(PUB)
        self.assertEqual(len(addr), 128)
        self.assertEqual(addr, derive_address(PUB, "ml-dsa-87"))
        self.assertNotEqual(addr, derive_address(PUB, "slh-dsa-shake-256s"))
        self.assertNotEqual(addr, PUB)
        self.assertNotEqual(addr, derive_address("00" * 32))

    def test_save_works_when_fchmod_is_missing(self) -> None:
        def boom(*_args, **_kwargs):
            raise AttributeError("module 'os' has no attribute 'fchmod'")

        with mock.patch.object(os, "fchmod", boom, create=True):
            record = self.client.create()
        self.assertTrue(self.wallet_path.is_file())
        self.assertEqual(self.client.show().address, record.address)
        self.assertNotIn(SECRET, record.public_view())

    def test_createwallet_keeps_secret_on_disk_only(self) -> None:
        record = self.client.create()
        self.assertTrue(self.wallet_path.is_file())
        self.assertEqual(record.private_key, SECRET)
        self.assertNotIn(SECRET, record.public_view())
        self.assertIn("priv_printed=0", record.public_view())
        self.assertNotIn(" pub=", record.public_view())
        shown = self.client.show()
        self.assertEqual(shown.address, record.address)
        self.assertEqual(self.rpc.sent, [])

    def test_balance_and_mine_do_not_send_private_key(self) -> None:
        self.client.create()
        self.assertEqual(self.client.balance(), "0")
        mined = self.client.mine()
        self.assertIn("mined block", mined)
        self.assertEqual(self.client.balance(), "50")
        for command in self.rpc.sent:
            self.assertNotIn(SECRET, command)
            self.assertTrue(command.startswith(("getbalance ", "mine ")))

    def test_send_uses_tx_build_and_sendtx_signed_hash(self) -> None:
        self.client.create()
        txid = self.client.send(TO_ADDR, 10, fee=1)
        self.assertEqual(txid, "deadbeef" * 8)
        self.assertEqual(len(self.rpc.sent), 2)
        self.assertTrue(self.rpc.sent[0].startswith("tx_build "))
        self.assertTrue(self.rpc.sent[1].startswith("sendtx_signed_hash "))
        for command in self.rpc.sent:
            self.assertNotIn(SECRET, command)
            self.assertNotIn("sign_message", command)
            self.assertFalse(command.startswith("sendtx "))
        stored = self.client.show()
        self.assertEqual(stored.next_nonce, 2)

    def test_send_retries_nonce_without_leaking_key(self) -> None:
        self.node.nonce_fail_once = True
        self.client.create()
        txid = self.client.send(TO_ADDR, 10, fee=1)
        self.assertEqual(txid, "deadbeef" * 8)
        build_cmds = [c for c in self.rpc.sent if c.startswith("tx_build ")]
        self.assertGreaterEqual(len(build_cmds), 2)
        self.assertTrue(build_cmds[-1].endswith(" 2"))
        for command in self.rpc.sent:
            self.assertNotIn(SECRET, command)

    def test_refuses_non_loopback_rpc_host(self) -> None:
        with self.assertRaises(WalletError):
            assert_loopback_host("8.8.8.8")
        with self.assertRaises(WalletError):
            TextRpcClient(host="203.0.113.9")
        assert_loopback_host("127.0.0.1")
        assert_loopback_host("localhost")
        assert_loopback_host("::1")

    def test_getinfo_is_testnet(self) -> None:
        info = self.client.getinfo()
        self.assertIn("network=testnet", info)
        self.assertIn("pq_mode=strict", info)
        self.assertIn("max_supply=50000000", info)

    def test_refuses_legacy_insecure_commands_if_transport_tries(self) -> None:
        self.client.create()
        self.rpc.sent.append("sign_message " + SECRET)
        with self.assertRaises(WalletError):
            self.client._assert_no_private_key_on_wire(SECRET)
        self.rpc.sent[-1] = "sendtx leaked"
        with self.assertRaises(WalletError):
            self.client._assert_no_private_key_on_wire("not-the-secret")


class LiboqsWalletTests(unittest.TestCase):
    def test_real_ml_dsa_87_keygen_and_local_sign(self) -> None:
        try:
            from addition_wallet import LiboqsMlDsa87
            backend = LiboqsMlDsa87()
        except (WalletError, OSError) as exc:
            self.skipTest(f"liboqs unavailable: {exc}")
        record = backend.generate()
        self.assertEqual(record.algorithm, "ml-dsa-87")
        self.assertEqual(record.address, derive_address(record.public_key))
        self.assertEqual(len(record.address), 128)
        self.assertNotEqual(record.address, record.public_key)
        self.assertEqual(len(record.public_key), 2592 * 2)
        self.assertEqual(len(record.private_key), 4896 * 2)
        signature = backend.sign(record.private_key, b"abc123def456")
        self.assertTrue(signature)
        self.assertNotIn(record.private_key, signature)
        self.assertEqual(len(signature) % 2, 0)


if __name__ == "__main__":
    unittest.main()
