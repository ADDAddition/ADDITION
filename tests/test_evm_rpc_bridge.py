#!/usr/bin/env python3
"""Local EVM bootstrap: loopback bind, chain 424242, send disabled."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "web" / "evm" / "evm_rpc_bridge.py"


def load_bridge():
    spec = importlib.util.spec_from_file_location("evm_rpc_bridge", BRIDGE)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


class EvmBridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.b = load_bridge()

    def test_chain_id_and_net_version(self) -> None:
        self.assertEqual(self.b.CHAIN_ID, 424242)
        self.assertEqual(self.b.handle("eth_chainId", []), "0x67932")
        self.assertEqual(self.b.handle("net_version", []), "424242")
        self.assertEqual(self.b.handle("web3_clientVersion", []), self.b.CLIENT_VERSION)

    def test_send_raw_disabled(self) -> None:
        with self.assertRaises(RuntimeError) as ctx:
            self.b.handle("eth_sendRawTransaction", ["0x00"])
        self.assertIn("disabled", str(ctx.exception).lower())

    def test_accounts_empty(self) -> None:
        self.assertEqual(self.b.handle("eth_accounts", []), [])
        self.assertEqual(self.b.handle("eth_requestAccounts", []), [])

    def test_add_chain_params_are_loopback_only(self) -> None:
        params = self.b.add_chain_params()
        self.assertEqual(params["chainId"], "0x67932")
        self.assertEqual(params["rpcUrls"], ["http://127.0.0.1:9545"])
        self.assertIn("send disabled", params["chainName"].lower())
        self.assertNotIn("0.0.0.0", str(params))

    def test_refuse_non_loopback_bind(self) -> None:
        self.assertTrue(self.b.is_loopback_host("127.0.0.1"))
        self.assertFalse(self.b.is_loopback_host("0.0.0.0"))
        self.assertFalse(self.b.is_loopback_host("34.27.30.115"))
        with self.assertRaises(SystemExit) as ctx:
            self.b.require_loopback_bind("0.0.0.0")
        self.assertIn("loopback", str(ctx.exception))

    def test_native_address_strips_0x(self) -> None:
        self.assertEqual(self.b.native_address("0xabc"), "abc")
        self.assertEqual(self.b.native_address("abc"), "abc")


if __name__ == "__main__":
    unittest.main()
