#!/usr/bin/env python3
"""Unit tests for the local token CLI (mocked TEXT RPC)."""

from __future__ import annotations

import io
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from typing import List

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from addition_text_rpc import TextRpcClient, TextRpcError
from addition_tokens import TokenCliError, TokenClient, main


class FakeTransport:
    def __init__(self) -> None:
        self.commands: List[str] = []
        self.replies = {
            "getinfo": "network=testnet network_name=addition-testnet height=0",
            "token_create DEMO alice 1000000 1000": "ok",
            "token_mint DEMO alice bob 50": "ok",
            "token_transfer DEMO alice bob 10": "ok",
            "token_balance DEMO alice": "990",
            "token_balance DEMO bob": "60",
            "token_info DEMO": "symbol=DEMO owner=alice max_supply=1000000 total_supply=1050",
            "token_create_ex BURN Burnable alice 100 10 0 1 - 0": "ok",
            "token_burn BURN alice 3": "ok",
            "nft_mint COL item1 alice demo-meta": "ok",
            "nft_transfer COL item1 alice bob": "ok",
            "nft_owner COL item1": "bob",
        }

    def __call__(self, command: str) -> str:
        self.commands.append(command)
        if command in self.replies:
            return self.replies[command]
        return "error: unknown command"


class TokenClientTests(unittest.TestCase):
    def setUp(self) -> None:
        self.transport = FakeTransport()
        self.client = TokenClient(TextRpcClient(transport=self.transport))

    def test_create_mint_transfer_balance(self) -> None:
        self.assertEqual(self.client.create("DEMO", "alice", 1_000_000, 1000), "ok")
        self.assertEqual(self.client.mint("DEMO", "alice", "bob", 50), "ok")
        self.assertEqual(self.client.transfer("DEMO", "alice", "bob", 10), "ok")
        self.assertEqual(self.client.balance("DEMO", "alice"), "990")
        self.assertEqual(self.client.balance("DEMO", "bob"), "60")
        self.assertEqual(
            self.transport.commands,
            [
                "token_create DEMO alice 1000000 1000",
                "token_mint DEMO alice bob 50",
                "token_transfer DEMO alice bob 10",
                "token_balance DEMO alice",
                "token_balance DEMO bob",
            ],
        )

    def test_info_and_nft(self) -> None:
        info = self.client.info("DEMO")
        self.assertIn("symbol=DEMO", info)
        self.assertEqual(self.client.nft_mint("COL", "item1", "alice", "demo-meta"), "ok")
        self.assertEqual(self.client.nft_transfer("COL", "item1", "alice", "bob"), "ok")
        self.assertEqual(self.client.nft_owner("COL", "item1"), "bob")

    def test_burnable_create_ex(self) -> None:
        self.assertEqual(
            self.client.create_ex("BURN", "Burnable", "alice", 100, 10, 0, True, "", 0),
            "ok",
        )
        self.assertEqual(self.client.burn("BURN", "alice", 3), "ok")

    def test_rejects_spaces_and_zero_amount(self) -> None:
        with self.assertRaises(TokenCliError):
            self.client.create("BAD SYM", "alice", 10, 1)
        with self.assertRaises(TokenCliError):
            self.client.mint("DEMO", "alice", "bob", 0)
        with self.assertRaises(TokenCliError):
            self.client.transfer("DEMO", "alice", "bob", -1)

    def test_surfaces_daemon_error(self) -> None:
        with self.assertRaises(TokenCliError) as ctx:
            self.client.balance("MISSING", "alice")
        self.assertIn("unknown command", str(ctx.exception))

    def test_refuses_unlisted_command(self) -> None:
        with self.assertRaises(TokenCliError):
            self.client._call("swap_exact_in A B trader 1 1")


class TokenCliMainTests(unittest.TestCase):
    def test_help_exits_zero(self) -> None:
        buf = io.StringIO()
        with redirect_stdout(buf), self.assertRaises(SystemExit) as ctx:
            main(["--help"])
        self.assertEqual(ctx.exception.code, 0)
        self.assertIn("token_create", buf.getvalue())

    def test_non_loopback_host_is_rejected(self) -> None:
        buf = io.StringIO()
        with redirect_stderr(buf):
            code = main(["--rpc-host", "8.8.8.8", "getinfo"])
        self.assertEqual(code, 1)
        self.assertIn("local TEXT RPC", buf.getvalue())


class TextRpcClientTests(unittest.TestCase):
    def test_rejects_multiline_and_oversize(self) -> None:
        client = TextRpcClient(transport=lambda _: "ok")
        with self.assertRaises(TextRpcError):
            client.call("getinfo\npeers")
        with self.assertRaises(TextRpcError):
            client.call("")


if __name__ == "__main__":
    unittest.main()
