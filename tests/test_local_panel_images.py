#!/usr/bin/env python3
"""Logo and live getinfo-shot checks for the local wallet/swap panels."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"

if str(WEB) not in sys.path:
    sys.path.insert(0, str(WEB))

from local_panel_images import (  # noqa: E402
    LIVE_ALT,
    LOGO_PNG,
    LOGO_SVG,
    PLACEHOLDER_ALT,
    PLACEHOLDER_SVG,
    getinfo_shot_svg,
    live_getinfo_svg,
)
from serve import content_type  # noqa: E402

FAKE_DASHBOARD = (
    "124.8 KH/s",
    "1,248,500",
    "Sovereign Mainnet Active",
    "42 Global",
    "TVL $",
    "volume 24h",
)


class LocalPanelImageTests(unittest.TestCase):
    def test_in_repo_assets_are_small_and_labeled(self) -> None:
        self.assertTrue(LOGO_SVG.is_file())
        self.assertTrue(LOGO_PNG.is_file())
        self.assertTrue(PLACEHOLDER_SVG.is_file())
        self.assertLess(LOGO_SVG.stat().st_size, 2048)
        self.assertLess(LOGO_PNG.stat().st_size, 2048)
        self.assertLess(PLACEHOLDER_SVG.stat().st_size, 4096)
        self.assertEqual(LOGO_PNG.read_bytes()[:8], b"\x89PNG\r\n\x1a\n")
        placeholder = PLACEHOLDER_SVG.read_text(encoding="utf-8")
        self.assertIn("PLACEHOLDER", placeholder)
        self.assertIn("RPC offline", placeholder)
        self.assertIn("No getinfo shot", placeholder)
        self.assertIn("not a dashboard", placeholder)
        for claim in FAKE_DASHBOARD:
            self.assertNotIn(claim, placeholder)
            self.assertNotIn(claim, LOGO_SVG.read_text(encoding="utf-8"))

    def test_live_shot_uses_only_the_real_getinfo_line(self) -> None:
        raw = (
            "network=testnet network_name=addition-testnet height=3 peers=1 "
            "pq_mode=strict max_supply=50000000"
        )
        svg = live_getinfo_svg(raw)
        self.assertIn(LIVE_ALT, svg)
        self.assertIn("127.0.0.1:8545", svg)
        self.assertIn("height=3", svg)
        self.assertIn("peers=1", svg)
        self.assertIn("network=testnet", svg)
        self.assertNotIn("height=99", svg)
        self.assertNotIn("TVL", svg)
        self.assertNotIn("KH/s", svg)
        self.assertNotIn("<polyline", svg)
        self.assertNotIn("<polygon", svg)
        offline = getinfo_shot_svg(None, True)
        self.assertIn("PLACEHOLDER", offline)
        self.assertEqual(offline, PLACEHOLDER_SVG.read_text(encoding="utf-8"))
        self.assertIn("PLACEHOLDER", getinfo_shot_svg("RPC offline", False))
        self.assertIn("PLACEHOLDER", getinfo_shot_svg("error: down", False))

    def test_serve_mime_types_for_panel_images(self) -> None:
        self.assertEqual(content_type(LOGO_SVG), "image/svg+xml")
        self.assertEqual(content_type(LOGO_PNG), "image/png")
        self.assertEqual(content_type(PLACEHOLDER_SVG), "image/svg+xml")

    def test_js_helper_builds_live_shot_or_placeholder(self) -> None:
        node = shutil.which("node")
        if not node:
            self.skipTest("node is required for the local-panel helper check")
        script = r"""
const fs = require("fs");
const vm = require("vm");
const path = require("path");
const code = fs.readFileSync(path.join("web", "public", "local-panel.js"), "utf8");
const window = {};
vm.runInNewContext(code, { window });
const P = window.AdditionLocalPanel;
if (!P || !P.liveGetinfoSvg || !P.applyGetinfoShot) {
  throw new Error("AdditionLocalPanel missing");
}
const raw = "network=testnet height=7 peers=0 pq_mode=strict";
const live = P.liveGetinfoSvg(raw);
if (live.indexOf("height=7") < 0 || live.indexOf("127.0.0.1:8545") < 0) {
  throw new Error("live shot missing getinfo text");
}
if (live.indexOf("TVL") >= 0 || live.indexOf("KH/s") >= 0) {
  throw new Error("live shot invented metrics");
}
const img = { src: "", alt: "" };
P.applyGetinfoShot(img, { offline: true, raw: "RPC offline" });
if (img.src !== P.PLACEHOLDER_SRC || img.alt !== P.PLACEHOLDER_ALT) {
  throw new Error("offline must use labeled placeholder");
}
P.applyGetinfoShot(img, { offline: false, raw: raw });
if (img.alt !== P.LIVE_ALT || img.src.indexOf("data:image/svg+xml") !== 0) {
  throw new Error("live shot must be a data URI from the real reply");
}
if (decodeURIComponent(img.src.split(",", 2)[1]).indexOf("height=7") < 0) {
  throw new Error("live data URI missing height=7");
}
console.log("local-panel-ok");
"""
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write(script)
            script_path = handle.name
        try:
            proc = subprocess.run(
                [node, script_path],
                cwd=str(ROOT),
                check=True,
                capture_output=True,
                text=True,
            )
        finally:
            Path(script_path).unlink(missing_ok=True)
        self.assertIn("local-panel-ok", proc.stdout)

    def test_gui_helper_keeps_loopback_and_finds_logo(self) -> None:
        import addition_wallet_gui as gui

        self.assertTrue(LOGO_PNG.is_file())
        self.assertEqual(gui.LOGO_PNG, LOGO_PNG)
        old = os.environ.get("ADDITION_LOCAL_RPC_HOST")
        os.environ["ADDITION_LOCAL_RPC_HOST"] = "8.8.8.8"
        try:
            with self.assertRaises(RuntimeError):
                gui.rpc("getinfo")
        finally:
            if old is None:
                os.environ.pop("ADDITION_LOCAL_RPC_HOST", None)
            else:
                os.environ["ADDITION_LOCAL_RPC_HOST"] = old


if __name__ == "__main__":
    unittest.main()
