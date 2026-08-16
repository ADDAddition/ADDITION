#!/usr/bin/env python3
"""Fail-closed tests for the ADDITION iOS wallet client rules."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IOS = ROOT / "ios"
if str(IOS) not in sys.path:
    sys.path.insert(0, str(IOS))

from addition_ios_client import (  # noqa: E402
    HASH_COMMITTED_ADDRESS_HEX_LEN,
    AdditionClientError,
    RPCOfflineError,
    TextRpcClient,
    WalletClient,
    WriteEndpoint,
    activity_from_send_reply,
    assert_command_allowed,
    assert_write_endpoint,
    build_createwallet,
    build_getbalance,
    build_wallet_send,
    classify_write_host,
    derive_address,
    is_known_public_read_endpoint,
    parse_confirmed_balance,
    parse_getinfo,
    parse_whole_amount,
    public_read_http,
    validate_address,
    validate_wallet_name,
)


PUB = "bb" * 32
ADDR = derive_address(PUB)
TO_ADDR = derive_address("cc" * 32)


class FakeNode:
    def __init__(self) -> None:
        self.online = True
        self.balance_line = "name=demo address=%s confirmed=50 staked=0" % ADDR
        self.info_line = (
            "network=testnet network_name=addition-testnet height=3 peers=1 "
            "pq_mode=strict max_supply=50000000"
        )
        self.calls = []
        self.wallets = {
            "demo": {
                "address": ADDR,
                "pub": PUB,
                "algo": "ml-dsa-87",
            }
        }

    def __call__(self, wire: str) -> str:
        self.calls.append(wire)
        if not self.online:
            raise OSError("connection refused")
        parts = wire.split()
        cmd = parts[0] if parts else ""
        if cmd == "getinfo":
            return self.info_line
        if cmd == "fee_info":
            return "base_min_fee=1 recommended_min_fee=1"
        if cmd == "createwallet":
            name = parts[1] if len(parts) > 1 else "default"
            if name in self.wallets:
                return "error: wallet already exists"
            self.wallets[name] = {"address": ADDR, "pub": PUB, "algo": "ml-dsa-87"}
            return (
                f"address={ADDR} address_chars=128 pub={PUB} algo=ml-dsa-87 "
                f"name={name} path=data/wallets/{name}.wal priv_printed=0"
            )
        if cmd == "wallet_info":
            name = parts[1]
            wallet = self.wallets.get(name)
            if wallet is None:
                return "error: wallet not found"
            return (
                f"name={name} address={wallet['address']} pub={wallet['pub']} "
                f"algo={wallet['algo']} path=data/wallets/{name}.wal "
                f"next_nonce=1 confirmed=50 staked=0"
            )
        if cmd == "wallet_list":
            return "wallets=%d name=demo address=%s algo=ml-dsa-87" % (
                len(self.wallets),
                ADDR,
            )
        if cmd == "wallet_balance":
            return self.balance_line
        if cmd == "getbalance":
            return "50"
        if cmd == "wallet_send":
            return f"ok:gossiped hash={'ab' * 32} from={ADDR} to={TO_ADDR} amount=10 fee=1"
        if cmd in {"sendtx", "sendtx_hash", "sign_message"}:
            raise AssertionError(f"insecure command used: {cmd}")
        return "error: unknown command"


class AddressAndAmountTests(unittest.TestCase):
    def test_address_derivation_matches_node_formula(self) -> None:
        addr = derive_address(PUB)
        self.assertEqual(len(addr), HASH_COMMITTED_ADDRESS_HEX_LEN)
        self.assertEqual(
            addr,
            "0d7423925f416bb9a12138d5dc4307735df023980d7d5c5b2858b00f57301f07"
            "932d68659e94d26f89f1a3b707bc219a78668722ed44de7edfca2e18727e4a2a",
        )
        self.assertEqual(addr, derive_address(PUB, "ml-dsa-87"))
        self.assertNotEqual(addr, derive_address(PUB, "slh-dsa-shake-256s"))
        self.assertNotEqual(addr, PUB)
        self.assertNotEqual(addr, derive_address("00" * 32))

    def test_address_rejects_foreign_chain_shapes(self) -> None:
        with self.assertRaises(AdditionClientError):
            validate_address("0x" + "ab" * 20)
        with self.assertRaises(AdditionClientError):
            validate_address("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq")
        with self.assertRaises(AdditionClientError):
            validate_address("1BoatSLRHtKNngkdXEeobR76b53LETtpyT")
        with self.assertRaises(AdditionClientError):
            validate_address("So11111111111111111111111111111111111111112")
        self.assertEqual(validate_address(ADDR), ADDR)

    def test_whole_unit_amounts_only(self) -> None:
        self.assertEqual(parse_whole_amount("10"), 10)
        self.assertEqual(parse_whole_amount("1"), 1)
        for raw in ("10.5", "0.00000001", "1e2", "1/2", "0", "-3", "", "ten"):
            with self.assertRaises(AdditionClientError):
                parse_whole_amount(raw)

    def test_wallet_name_matches_node(self) -> None:
        self.assertEqual(validate_wallet_name("default"), "default")
        self.assertEqual(validate_wallet_name("Alice-1"), "Alice-1")
        for raw in ("", "-bad", "has space", "a" * 65, "weird!"):
            with self.assertRaises(AdditionClientError):
                validate_wallet_name(raw)


class WriteRPCPolicyTests(unittest.TestCase):
    def test_allows_loopback_and_lan_only(self) -> None:
        self.assertEqual(classify_write_host("127.0.0.1"), "loopback")
        self.assertEqual(classify_write_host("localhost"), "loopback")
        self.assertEqual(classify_write_host("::1"), "loopback")
        self.assertEqual(classify_write_host("192.168.1.20"), "lan")
        self.assertEqual(classify_write_host("10.0.0.8"), "lan")
        self.assertEqual(classify_write_host("172.16.4.4"), "lan")
        self.assertEqual(classify_write_host("macbook.local"), "lan")
        assert_write_endpoint("127.0.0.1:8545")
        assert_write_endpoint("http://192.168.1.20:18545")

    def test_refuses_public_write_endpoints(self) -> None:
        refused = [
            "rpc.additionblockchain.com",
            "additionblockchain.com",
            "www.additionblockchain.com",
            "34.27.30.115",
            "8.8.8.8",
            "1.1.1.1",
            "https://rpc.additionblockchain.com/rpc",
            "http://34.27.30.115:38545/rpc",
        ]
        for host in refused:
            with self.subTest(host=host):
                with self.assertRaises(AdditionClientError):
                    WriteEndpoint.parse(host)

    def test_endpoint_parse_defaults_to_loopback_8545(self) -> None:
        ep = WriteEndpoint.parse("127.0.0.1")
        self.assertEqual(ep.host, "127.0.0.1")
        self.assertEqual(ep.port, 8545)
        self.assertEqual(ep.scheme, "text")

    def test_public_read_allowlist_is_not_a_wallet_backend(self) -> None:
        self.assertTrue(is_known_public_read_endpoint("https://rpc.additionblockchain.com/rpc"))
        self.assertTrue(is_known_public_read_endpoint("http://34.27.30.115/rpc"))
        with self.assertRaises(AdditionClientError):
            assert_command_allowed("createwallet default", write=False)
        with self.assertRaises(AdditionClientError):
            assert_command_allowed("wallet_send demo %s 1" % TO_ADDR, write=False)
        with self.assertRaises(AdditionClientError):
            assert_command_allowed("getbalance %s" % ADDR, write=False)
        assert_command_allowed("getinfo", write=False)
        assert_command_allowed("getblock 1", write=False)
        assert_command_allowed("getblockraw 1", write=False)


class FailClosedRPCTests(unittest.TestCase):
    def setUp(self) -> None:
        self.node = FakeNode()
        self.rpc = TextRpcClient(WriteEndpoint.parse("127.0.0.1:8545"), transport=self.node)
        self.client = WalletClient(self.rpc)

    def test_balance_uses_real_rpc_reply(self) -> None:
        self.assertEqual(self.client.balance("demo"), 50)
        self.assertTrue(self.rpc.sent[-1].startswith("wallet_balance "))

    def test_offline_rpc_does_not_invent_balance_or_height(self) -> None:
        def down(_wire: str) -> str:
            raise OSError("connection refused")

        offline = TextRpcClient(WriteEndpoint.parse("127.0.0.1:8545"), transport=down)
        client = WalletClient(offline)
        with self.assertRaises(RPCOfflineError) as ctx:
            client.balance("demo")
        self.assertIn("RPC offline", str(ctx.exception))
        with self.assertRaises(RPCOfflineError):
            client.getinfo()
        with self.assertRaises(RPCOfflineError):
            parse_confirmed_balance("")
        with self.assertRaises(RPCOfflineError):
            parse_confirmed_balance("RPC offline")
        with self.assertRaises(AdditionClientError):
            parse_confirmed_balance("error: command disabled on public RPC")
        with self.assertRaises(AdditionClientError):
            parse_getinfo("ok but no fields")

    def test_malformed_balance_is_an_error_not_zero(self) -> None:
        for line in ("confirmed=abc", "balance=50", "ok", "50.0", "null"):
            with self.assertRaises(AdditionClientError):
                parse_confirmed_balance(line)

    def test_create_load_send_match_node_commands(self) -> None:
        created = self.client.createwallet("demo2")
        self.assertEqual(created["address"], ADDR)
        self.assertEqual(created["algo"], "ml-dsa-87")
        self.assertEqual(created["priv_printed"], "0")
        info = self.client.wallet_info("demo")
        self.assertEqual(info["address"], ADDR)
        sent = self.client.send("demo", TO_ADDR, 10, fee=1)
        self.assertIn("ok:gossiped", sent)
        self.assertEqual(self.rpc.sent[0], "createwallet demo2")
        self.assertTrue(self.rpc.sent[1].startswith("wallet_info "))
        self.assertEqual(self.rpc.sent[-1], f"wallet_send demo {TO_ADDR} 10 1")

    def test_refuses_insecure_and_foreign_commands(self) -> None:
        for cmd in ("sendtx leaked", "sendtx_hash leaked", "sign_message leaked"):
            with self.assertRaises(AdditionClientError):
                assert_command_allowed(cmd, write=True)
        for cmd in ("eth_blockNumber", "getbalancebtc", "solana_send"):
            with self.assertRaises(AdditionClientError):
                assert_command_allowed(cmd, write=True)

    def test_build_helpers_match_daemon_usage(self) -> None:
        self.assertEqual(build_createwallet("default"), "createwallet default")
        self.assertEqual(build_getbalance(ADDR), f"getbalance {ADDR}")
        self.assertEqual(
            build_wallet_send("demo", TO_ADDR, 7),
            f"wallet_send demo {TO_ADDR} 7",
        )

    def test_activity_comes_from_real_send_reply_only(self) -> None:
        item = activity_from_send_reply(
            f"ok:gossiped hash={'ab' * 32} from={ADDR} to={TO_ADDR} amount=10 fee=1"
        )
        self.assertEqual(item["kind"], "send")
        self.assertEqual(item["amount"], "-10 ADD")
        self.assertEqual(item["detail"], TO_ADDR)
        for bad in ("", "RPC offline", "error: insufficient unlocked balance", "ok"):
            with self.assertRaises(AdditionClientError):
                activity_from_send_reply(bad)

    def test_public_read_cannot_be_used_as_wallet_backend(self) -> None:
        with self.assertRaises(AdditionClientError):
            public_read_http("createwallet default", "https://rpc.additionblockchain.com/rpc")
        with self.assertRaises(AdditionClientError):
            public_read_http("getinfo", "http://example.com/rpc")


class SourceGuardTests(unittest.TestCase):
    def _copy_paths(self) -> list[Path]:
        out = []
        for path in IOS.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in {".swift", ".md", ".plist"}:
                continue
            rel = str(path.relative_to(IOS))
            if "/Client/" in rel or rel.endswith("addition_ios_client.py"):
                continue
            if "Tests/" in rel or rel.endswith("Tests.swift"):
                continue
            out.append(path)
        return out

    def test_ios_slice_stays_addition_only(self) -> None:
        client = (IOS / "addition_ios_client.py").read_text(encoding="utf-8").lower()
        self.assertIn("createwallet", client)
        self.assertIn("wallet_send", client)
        self.assertIn("wallet_balance", client)
        self.assertIn("127.0.0.1", client)
        self.assertIn("rpc offline", client)
        copy = "\n".join(p.read_text(encoding="utf-8") for p in self._copy_paths()).lower()
        self.assertIn("contact@additionblockchain.com", copy)
        self.assertNotIn("trust wallet", copy)
        self.assertNotIn("metamask", copy)
        self.assertNotIn("walletconnect", copy)
        self.assertNotIn("addison", copy)
        self.assertNotRegex(copy, r"\bhonest\b")
        self.assertNotIn("xa1.ai", copy)
        self.assertNotIn("token sale", copy)
        self.assertNotIn("app store", copy)
        self.assertNotIn("0.0.0.0:8545", copy)

    def test_previews_stay_fail_closed(self) -> None:
        for name in ("home", "receive", "send", "activity"):
            text = (IOS / "previews" / f"{name}.html").read_text(encoding="utf-8")
            self.assertNotRegex(text, r"\b\d+\s*ADD\b")
            self.assertNotIn("height=", text)
            self.assertIn("Logo.png", text)
            self.assertIn("Mark.png", text)

    def test_brand_images_are_website_files(self) -> None:
        assets = IOS / "AdditionWallet" / "AdditionWallet" / "Assets.xcassets"
        pairs = [
            (ROOT / "web" / "public" / "logo-transparent.png", assets / "Logo.imageset" / "Logo.png"),
            (ROOT / "web" / "public" / "apple-touch-icon.png", assets / "Mark.imageset" / "Mark.png"),
            (ROOT / "web" / "public" / "og.png", assets / "OpenGraph.imageset" / "OpenGraph.png"),
            (ROOT / "web" / "public" / "favicon-32.png", assets / "Favicon.imageset" / "Favicon.png"),
        ]
        for src, dst in pairs:
            self.assertTrue(src.is_file(), src)
            self.assertTrue(dst.is_file(), dst)
            self.assertEqual(src.read_bytes(), dst.read_bytes(), dst.name)
        icon = assets / "AppIcon.appiconset" / "AppIcon.png"
        self.assertTrue(icon.is_file())
        self.assertGreater(icon.stat().st_size, 1000)

    def test_no_committed_ipa(self) -> None:
        ipas = list(IOS.rglob("*.ipa"))
        self.assertEqual(ipas, [])

    def test_xcode_project_exists(self) -> None:
        project = IOS / "AdditionWallet" / "AdditionWallet.xcodeproj" / "project.pbxproj"
        readme = IOS / "README.md"
        self.assertTrue(project.is_file())
        self.assertTrue(readme.is_file())
        readme_text = readme.read_text(encoding="utf-8")
        self.assertIn("Xcode", readme_text)
        self.assertIn("127.0.0.1", readme_text)
        self.assertNotIn("sudo apt-get", readme_text)
        self.assertNotIn("cashiers", readme_text.lower())
        self.assertNotRegex(readme_text.lower(), r"\bhonest\b")

    def test_does_not_touch_public_site_or_windows_slice(self) -> None:
        # This slice must not add iOS links to cashiers surfaces.
        download = (ROOT / "web" / "public" / "download" / "index.html").read_text(encoding="utf-8")
        join = (ROOT / "web" / "public" / "join.md").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        for text in (download, join, readme):
            self.assertNotIn("App Store", text)
            self.assertNotIn("TestFlight", text)
            self.assertNotIn("AdditionWallet.xcodeproj", text)


if __name__ == "__main__":
    unittest.main()
