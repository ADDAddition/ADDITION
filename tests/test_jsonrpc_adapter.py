#!/usr/bin/env python3
"""Unit tests for the local/testnet JSON-RPC adapter."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from addition_jsonrpc_adapter import (
    AdapterError,
    classify_method,
    dispatch,
    format_text_command,
    handle_payload,
    is_loopback_host,
)
from addition_text_rpc import TextRpcClient


class FakeTransport:
    def __init__(self) -> None:
        self.commands = []
        self.replies = {
            "getinfo": "network=testnet network_name=addition-testnet",
            "token_balance DEMO alice": "1000",
            "token_create DEMO alice 1000 10": "ok",
            "tx_status deadbeef": "status=unknown",
        }

    def __call__(self, command: str) -> str:
        self.commands.append(command)
        return self.replies.get(command, "error: unknown command")


class AdapterUnitTests(unittest.TestCase):
    def setUp(self) -> None:
        self.transport = FakeTransport()
        self.rpc = TextRpcClient(transport=self.transport)

    def test_loopback_only(self) -> None:
        self.assertTrue(is_loopback_host("127.0.0.1"))
        self.assertTrue(is_loopback_host("localhost"))
        self.assertTrue(is_loopback_host("::1"))
        self.assertFalse(is_loopback_host("8.8.8.8"))
        self.assertFalse(is_loopback_host("example.com"))

    def test_classify_real_and_refused_methods(self) -> None:
        self.assertEqual(classify_method("getinfo"), "read")
        self.assertEqual(classify_method("token_balance"), "read")
        self.assertEqual(classify_method("token_create"), "write")
        self.assertEqual(classify_method("eth_blockNumber"), "refused")
        self.assertEqual(classify_method("web3_clientVersion"), "refused")
        self.assertEqual(classify_method("sendtx"), "refused")
        self.assertEqual(classify_method("swap_quote"), "unknown")
        self.assertEqual(classify_method("invented_foo"), "unknown")
        self.assertEqual(classify_method("getinfo", public_read=True), "read")
        self.assertEqual(classify_method("getblockraw", public_read=True), "read")
        self.assertEqual(classify_method("token_create", public_read=True), "refused")
        self.assertEqual(classify_method("mine", public_read=True), "refused")

    def test_format_joins_params(self) -> None:
        self.assertEqual(
            format_text_command("token_balance", ["DEMO", "alice"]),
            "token_balance DEMO alice",
        )
        self.assertEqual(
            format_text_command("token_create", ["DEMO", "alice", 1000, 10]),
            "token_create DEMO alice 1000 10",
        )
        with self.assertRaises(AdapterError):
            format_text_command("token_balance", ["BAD SYM", "alice"])

    def test_dispatch_read_and_write(self) -> None:
        self.assertIn("network=testnet", dispatch(self.rpc, "getinfo", [], True))
        self.assertEqual(dispatch(self.rpc, "token_balance", ["DEMO", "alice"], True), "1000")
        self.assertEqual(
            dispatch(self.rpc, "token_create", ["DEMO", "alice", 1000, 10], True),
            "ok",
        )
        with self.assertRaises(AdapterError):
            dispatch(self.rpc, "token_create", ["DEMO", "alice", 1000, 10], False)
        with self.assertRaises(AdapterError):
            dispatch(self.rpc, "eth_chainId", [], True)
        with self.assertRaises(AdapterError):
            dispatch(self.rpc, "swap_quote", ["A", "B", 1], True)

    def test_jsonrpc_payload(self) -> None:
        reply = handle_payload(
            self.rpc,
            {"jsonrpc": "2.0", "id": 7, "method": "getinfo", "params": []},
            True,
        )
        self.assertEqual(reply["id"], 7)
        self.assertIn("network=testnet", reply["result"])

        refused = handle_payload(
            self.rpc,
            {"jsonrpc": "2.0", "id": 8, "method": "eth_blockNumber", "params": []},
            True,
        )
        self.assertEqual(refused["error"]["code"], -32601)
        self.assertIn("Ethereum", refused["error"]["message"])

        batch = handle_payload(
            self.rpc,
            [
                {"jsonrpc": "2.0", "id": 1, "method": "token_balance", "params": ["DEMO", "alice"]},
                {"jsonrpc": "2.0", "id": 2, "method": "tx_status", "params": ["deadbeef"]},
            ],
            True,
        )
        self.assertEqual(batch[0]["result"], "1000")
        self.assertEqual(batch[1]["result"], "status=unknown")


if __name__ == "__main__":
    unittest.main()
