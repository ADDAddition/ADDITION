#!/usr/bin/env python3
"""The bridge spec must stay a spec, not a live cross-chain product."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = ROOT / "docs" / "BRIDGE.md"
RENDER = ROOT / "web" / "render_docs.py"
RPC_ACCESS = ROOT / "src" / "rpc_access.cpp"
CHROME = ROOT / "web" / "public" / "chrome.js"
INDEX = ROOT / "web" / "public" / "index.html"
PUBLIC = ROOT / "web" / "public"


class BridgeSpecTests(unittest.TestCase):
    def setUp(self) -> None:
        self.spec = SPEC.read_text(encoding="utf-8")
        self.spec_lower = self.spec.lower()

    def test_spec_exists_and_is_labeled(self) -> None:
        self.assertTrue(SPEC.is_file())
        for needle in (
            "specification only",
            "not a live cross-chain product",
            "not built",
            "moves_bitcoin=0",
            "moves_eth=0",
            "moves_sol=0",
            "127.0.0.1",
            "error: command disabled on public RPC",
            "rehearsal",
            "this tree ships the spec only",
            "contact@additionblockchain.com",
        ):
            self.assertIn(needle.lower(), self.spec_lower, f"missing {needle!r}")

    def test_spec_cannot_claim_live_moves(self) -> None:
        for claim in ("moves_bitcoin=1", "moves_eth=1", "moves_sol=1"):
            self.assertNotIn(claim, self.spec)
        self.assertNotRegex(self.spec_lower, r"\bhonest\b")
        self.assertNotRegex(self.spec_lower, r"\bhonesty\b")
        self.assertNotIn("addison", self.spec_lower)
        self.assertNotIn("token sale", self.spec_lower)
        self.assertNotIn("zk-stark", self.spec_lower)
        self.assertNotIn("groth16", self.spec_lower)
        self.assertNotRegex(self.spec, r"\bTPS\b")
        self.assertNotRegex(self.spec, r"research_goal_tps")

    def test_spec_says_existing_lock_mint_is_local_bookkeeping(self) -> None:
        self.assertIn("BridgeEngine", self.spec)
        self.assertIn("does not lock coins on another", self.spec_lower)
        self.assertIn("does not mint add from a foreign deposit", self.spec_lower)
        self.assertIn("in-process", self.spec_lower)

    def test_spec_names_a_later_rehearsal_without_shipping_one(self) -> None:
        self.assertIn("local rehearsal", self.spec_lower)
        self.assertIn("later; not in this tree yet", self.spec_lower)
        self.assertIn("do not remake #12, #33, #38, or #39", self.spec_lower)
        self.assertIn("0x000000FFFFFFFFFF", self.spec)

    def test_spec_is_not_published_on_the_public_site(self) -> None:
        render = RENDER.read_text(encoding="utf-8")
        self.assertNotIn("BRIDGE.md", render)
        self.assertNotIn("docs/BRIDGE", render)
        self.assertFalse((PUBLIC / "bridge").exists())
        self.assertFalse((PUBLIC / "docs" / "bridge").exists())
        chrome = CHROME.read_text(encoding="utf-8")
        index = INDEX.read_text(encoding="utf-8")
        self.assertNotIn("/bridge", chrome)
        self.assertNotIn("/bridge", index)
        self.assertNotIn('["/swap/"', chrome)

    def test_public_read_allowlist_still_omits_bridge_writes(self) -> None:
        src = RPC_ACCESS.read_text(encoding="utf-8")
        start = src.index("bool is_public_read_command")
        end = src.index("bool is_remote_allowed_command")
        public = src[start:end]
        for cmd in (
            "bridge_lock",
            "bridge_mint",
            "bridge_burn",
            "bridge_release",
            "bridge_register",
            "bridge_mint_attested",
            "bridge_release_attested",
        ):
            self.assertNotIn(f'"{cmd}"', public, f"public-read must refuse {cmd}")

    def test_no_new_bridge_rpc_in_this_pr_tree(self) -> None:
        # Spec-only: do not add a lock/mint command that names BTC/ETH/SOL.
        rpc = (ROOT / "src" / "rpc_server.cpp").read_text(encoding="utf-8")
        self.assertNotIn("moves_bitcoin=1", rpc)
        self.assertNotIn("moves_eth=1", rpc)
        self.assertNotIn("moves_sol=1", rpc)
        self.assertIsNone(re.search(r"bridge_rehearsal", rpc))


if __name__ == "__main__":
    unittest.main()
