#!/usr/bin/env python3
"""Tests for local EVM JSON-RPC bridge (honest bootstrap labels)."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path
from unittest import mock

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
        self.assertIn("local", self.b.CLIENT_VERSION)

    def test_send_raw_disabled(self) -> None:
        with self.assertRaises(RuntimeError) as ctx:
            self.b.handle("eth_sendRawTransaction", ["0x00"])
        msg = str(ctx.exception).lower()
        self.assertIn("disabled", msg)
        self.assertNotIn("uniswap", msg)  # error is about send, not fake DEX

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

    def test_rpc_modules_and_protocol(self) -> None:
        mods = self.b.handle("rpc_modules", [])
        self.assertIn("eth", mods)
        self.assertIn("addition", mods)
        self.assertEqual(self.b.handle("net_listening", []), True)
        self.assertEqual(self.b.handle("eth_protocolVersion", []), "0x41")

    def test_disclaimer_mentions_not_eth_mainnet(self) -> None:
        d = self.b.handle("addition_disclaimer", [])
        low = str(d).lower()
        self.assertIn("not ethereum mainnet", low)
        self.assertIn("eth/xmr", low)
        self.assertNotIn("mainnet is live", low)

    def test_network_info_from_getinfo(self) -> None:
        with mock.patch.object(
            self.b,
            "tcp_rpc",
            return_value="network=testnet height=3 peers=1 mempool=0 pow_algorithm=sha3_512",
        ):
            info = self.b.handle("addition_networkInfo", [])
        self.assertEqual(info["chainId"], 424242)
        self.assertFalse(info["ethereumMainnet"])
        self.assertFalse(info["uniswapLive"])
        self.assertFalse(info["ethXmrBridgeLive"])
        self.assertFalse(info["sendRawEnabled"])
        self.assertEqual(info["network"], "testnet")
        self.assertEqual(info["height"], 3)
        self.assertFalse(info["mainnetHasBlocks"])

    def test_mainnet_height_ge_1_flag_only_from_rpc(self) -> None:
        with mock.patch.object(
            self.b,
            "tcp_rpc",
            return_value="network=mainnet height=1 peers=0 mempool=0",
        ):
            info = self.b.handle("addition_networkInfo", [])
        self.assertTrue(info["mainnetHasBlocks"])
        self.assertNotIn("mainnet is live", str(info).lower())

    def test_dispatch_batch(self) -> None:
        out = self.b.dispatch_one({"jsonrpc": "2.0", "id": 1, "method": "eth_chainId", "params": []})
        self.assertEqual(out["result"], "0x67932")
        with mock.patch.object(self.b, "tcp_rpc", return_value="network=testnet height=0 peers=0"):
            batch = [
                self.b.dispatch_one({"jsonrpc": "2.0", "id": 1, "method": "eth_chainId", "params": []}),
                self.b.dispatch_one({"jsonrpc": "2.0", "id": 2, "method": "addition_disclaimer", "params": []}),
            ]
        self.assertEqual(batch[0]["result"], "0x67932")
        self.assertIn("local bootstrap", batch[1]["result"])

    def test_eth_block_shape(self) -> None:
        fields = {
            "height": "2",
            "hash": "abcd" * 16,
            "previous_hash": "0000" * 16,
            "nonce": "7",
            "timestamp": "100",
            "difficulty_target": "99",
            "tx_hashes": "aa,bb",
            "reward_address": "miner1",
            "pow_algorithm": "sha3_512",
        }
        block = self.b.eth_block_from_native(fields, full_txs=False)
        self.assertEqual(block["number"], "0x2")
        self.assertTrue(block["hash"].startswith("0x"))
        self.assertEqual(block["transactions"], ["aa", "bb"])
        self.assertEqual(block["additionPowAlgorithm"], "sha3_512")


if __name__ == "__main__":
    unittest.main()
