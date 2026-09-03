#!/usr/bin/env python3
"""Static copy and RPC checks for the ADDITION public mainnet site."""

from __future__ import annotations

import importlib.util
import re
import shutil
import subprocess
import tempfile
import unittest
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Thread

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = ROOT / "web" / "public"

FAKE_CLAIMS = (
    "Sovereign Mainnet Active",
    "42 Global",
    "124.8 KH/s",
    "1,248,500",
    "ZK-STARK",
    "native ZK path",
    "ZK verifier contract",
    "privacy ZK",
    "Range Proofs",
    "Roadmap to Production",
    "labjay69",
    "labreche_jeremy@outlook",
    "Addison Electronics",
    "Addison Electronique",
    "Swap-to-BTC",
    "Solidity IDE",
)

SERMONS = (
    "honest testnet",
    "honest website",
    "research notice",
    "never invent",
    "we never invent",
)


def read(rel: str) -> str:
    return (PUBLIC / rel).read_text(encoding="utf-8")


def iter_site_text() -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for path in PUBLIC.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix not in {".html", ".js", ".css", ".md"}:
            continue
        out.append((str(path.relative_to(PUBLIC)), path.read_text(encoding="utf-8")))
    return out


class PublicSiteStaticTests(unittest.TestCase):
    def test_forbidden_marketing_claims_absent(self) -> None:
        for rel, text in iter_site_text():
            for claim in FAKE_CLAIMS:
                self.assertNotIn(claim, text, f"{rel} still contains {claim!r}")

    def test_no_sermon_copy(self) -> None:
        for rel, text in iter_site_text():
            lower = text.lower()
            for phrase in SERMONS:
                self.assertNotIn(phrase, lower, f"{rel} still contains {phrase!r}")
            self.assertNotRegex(lower, r"\bhonest\b", f"{rel} still contains 'honest'")
            self.assertNotRegex(lower, r"\bhonesty\b", f"{rel} still contains 'honesty'")

    def test_contact_is_public_mailbox_only(self) -> None:
        chrome = read("chrome.js")
        self.assertIn("contact@additionblockchain.com", chrome)
        self.assertNotIn("gmail.com", chrome)
        self.assertNotIn("outlook.com", chrome)

    def test_chrome_is_mainnet_product_nav(self) -> None:
        chrome = read("chrome.js")
        self.assertIn('["/", "Explore"', chrome)
        self.assertIn('["/wallet/", "Wallet"', chrome)
        self.assertIn('["/join/", "Get started"', chrome)
        self.assertIn('["/status/", "Status"', chrome)
        self.assertIn('["/download/", "Download"]', chrome)
        self.assertIn('["/launch/", "Launch"]', chrome)
        self.assertIn('["/embed/", "Embed"]', chrome)
        self.assertIn('["/rpc/", "Public RPC"]', chrome)
        self.assertIn(">MAINNET</span>", chrome)
        self.assertIn("public product is <strong>MAINNET</strong>", chrome)
        self.assertIn("contact@additionblockchain.com", chrome)
        self.assertNotIn(">TESTNET</span>", chrome)
        self.assertNotIn("0.0.0.0:8545", chrome)
        self.assertIn('path === "/wallet"', chrome)

    def test_chrome_footer_has_banner_2_video(self) -> None:
        chrome = read("chrome.js")
        videos = re.findall(r"<video\b[^>]*>", chrome, flags=re.IGNORECASE)
        self.assertEqual(len(videos), 1, "site footer must have exactly one <video>")
        footer_video = videos[0]
        self.assertIn("addition-banner-2.mp4", footer_video)
        self.assertIn("footer-banner", footer_video)
        self.assertRegex(footer_video, re.compile(r"\bmuted\b", re.IGNORECASE))
        self.assertNotRegex(footer_video, re.compile(r"\bautoplay\b", re.IGNORECASE))
        self.assertRegex(footer_video, re.compile(r"\bloop\b", re.IGNORECASE))
        self.assertRegex(footer_video, re.compile(r"\bplaysinline\b", re.IGNORECASE))
        self.assertNotRegex(footer_video, re.compile(r"\bcontrols\b", re.IGNORECASE))
        self.assertNotIn("addition-stinger.mp4", chrome)
        self.assertNotIn("addition-banner-1.mp4", chrome)
        self.assertIn("kickHeroStinger", chrome)
        self.assertIn('querySelector(".hero-stinger")', chrome)
        self.assertIn("video.muted = true", chrome)
        self.assertIn("video.play()", chrome)
        css = read("common.css")
        self.assertIn(".footer-banner", css)
        self.assertIn("object-fit: contain", css)
        self.assertIn("@media (max-width: 840px)", css)
        # Live phone hotfix (CoS curl): exact ≤840px stinger block; 56vw×2; no max-height:none.
        self.assertIn(
            """@media (max-width: 840px) {
  .hero-stinger,
  video.hero-stinger,
  .hero-stinger video {
    display: block;
    width: 100%;
    max-width: 100%;
    height: auto;
    max-height: 56vw;
    object-fit: contain;
    object-position: center;
  }
  .hero-stinger {
    overflow: hidden;
    margin: 0.5rem 0 1rem;
  }
}""",
            css,
        )
        self.assertEqual(css.count("max-height: 56vw"), 2)
        self.assertNotIn("max-height: none", css)

    def test_homepage_is_mainnet_product(self) -> None:
        index = read("index.html")
        self.assertIn('net-badge">MAINNET</span>', index)
        self.assertIn("ADDITION_MAINNET_V1", index)
        self.assertIn("34.27.30.115:38546", index)
        self.assertIn("/api/rpc", index)
        self.assertIn("latest-blocks", index)
        self.assertIn("/wallet/", index)
        self.assertIn("/explorer.js", index)
        self.assertIn("<h1>ADDITION</h1>", index)
        videos = re.findall(r"<video\b[^>]*>", index, flags=re.IGNORECASE)
        self.assertEqual(len(videos), 1, "homepage must have exactly one hero <video>")
        hero = videos[0]
        self.assertIn("addition-stinger.mp4", hero)
        self.assertRegex(hero, re.compile(r"\bmuted\b", re.IGNORECASE))
        self.assertRegex(hero, re.compile(r"\bautoplay\b", re.IGNORECASE))
        self.assertRegex(hero, re.compile(r"\bloop\b", re.IGNORECASE))
        self.assertRegex(hero, re.compile(r"\bplaysinline\b", re.IGNORECASE))
        self.assertRegex(hero, re.compile(r"\bwebkit-playsinline\b", re.IGNORECASE))
        self.assertRegex(hero, re.compile(r'\bpreload=["\']auto["\']', re.IGNORECASE))
        self.assertNotRegex(hero, re.compile(r"\bcontrols\b", re.IGNORECASE))
        self.assertNotIn("hero-banners", index)
        self.assertNotIn("addition-banner-1.mp4", index)
        self.assertNotIn("addition-banner-2.mp4", index)
        self.assertNotIn('net-badge">TESTNET</span>', index)
        self.assertNotIn("ADDITION_TESTNET_V1", index)
        self.assertNotIn("0.0.0.0:8545", index)
        self.assertNotIn("SmartChain", index)
        self.assertNotIn("DEX", index)
        self.assertNotIn("token sale", index.lower())

    def test_download_is_mainnet_helper(self) -> None:
        download = read("download/index.html")
        self.assertIn('data-sub="mainnet / local"', download)
        self.assertIn("addition-wallet-mainnet", download)
        self.assertIn("addition-wallet-cli-mainnet", download)
        self.assertIn("additiond --mainnet", download)
        self.assertIn("127.0.0.1:8546", download)
        self.assertIn("/local-rpc", download)
        self.assertIn("wallet_send", download)
        self.assertIn("does not publish a <code>.exe</code>", download)
        self.assertNotIn("addition-wallet-testnet", download)
        self.assertNotIn('data-sub="testnet / local"', download)
        self.assertNotIn("0.0.0.0:8545", download)
        self.assertIn("Not a hosted custodial wallet", download)

    def test_status_and_manifest_are_mainnet(self) -> None:
        status = read("status/index.html")
        manifest = read("manifest.webmanifest")
        self.assertIn('net-badge">MAINNET</span>', status)
        self.assertIn("ADDITION_MAINNET_V1", status)
        self.assertIn("34.27.30.115:38546", status)
        self.assertNotIn("never talks to mainnet", status.lower())
        self.assertIn("ADDITION_MAINNET_V1", manifest)
        self.assertIn("icon-192.png", manifest)
        self.assertIn("icon-512.png", manifest)
        self.assertTrue((PUBLIC / "icon-192.png").is_file())
        self.assertTrue((PUBLIC / "icon-512.png").is_file())

    def test_join_docs_mainnet_first(self) -> None:
        join = read("join/index.html")
        join_md = read("join.md")
        docs_join = read("docs/join.md")
        self.assertEqual(join_md, docs_join)
        self.assertIn("ADDITION_MAINNET_V1", join)
        self.assertIn("34.27.30.115:28546", join)
        self.assertIn("34.27.30.115:38546", join)
        self.assertIn("id=\"mainnet\"", join)
        self.assertIn("id=\"testnet\"", join)
        self.assertLess(join.find('id="mainnet"'), join.find('id="testnet"'))
        self.assertIn("wallet_send", join)
        self.assertIn("/local-rpc", join)
        self.assertIn("never publish", join.lower())
        self.assertNotIn("Public product today. Explorer", join)
        self.assertNotIn("website explorer stays on testnet", join.lower())
        self.assertIn("34.27.30.115:28546", join_md)
        self.assertIn("34.27.30.115:38546", join_md)
        self.assertIn("ADDITION_MAINNET_V1", join_md)

    def test_rpc_page_documents_mainnet_seed(self) -> None:
        rpc = read("rpc/index.html")
        self.assertIn("34.27.30.115:38546", rpc)
        self.assertIn("34.27.30.115:28546", rpc)
        self.assertIn("/api/rpc", rpc)
        self.assertIn("wallet_send", rpc)
        self.assertIn("127.0.0.1:8546", rpc)
        self.assertIn("Command explorer", rpc)
        self.assertNotIn("eth_call", rpc)
        self.assertIn("Do not open unauthenticated write RPC", rpc)
        self.assertIn("0.0.0.0:8545", rpc)  # explicit forbid warning only

    def test_wallet_is_trust_like_loopback_only(self) -> None:
        wallet = read("wallet/index.html")
        wallet_js = read("wallet.js")
        helper = read("local-rpc.js")
        self.assertIn("wallet-tabs", wallet)
        self.assertIn('data-go="home"', wallet)
        self.assertIn('data-go="receive"', wallet)
        self.assertIn('data-go="send"', wallet)
        self.assertIn('data-go="activity"', wallet)
        self.assertIn("/local-rpc", wallet)
        self.assertIn("127.0.0.1:8546", wallet)
        self.assertIn("createwallet", wallet_js)
        self.assertIn("wallet_send", wallet_js)
        self.assertIn("/local-rpc?cmd=", helper)
        self.assertNotIn("0.0.0.0:8545", wallet)
        self.assertNotIn("stake_reward", wallet)
        self.assertNotIn("getblocktemplate", wallet)
        self.assertNotIn("submitblock", wallet)
        # Spend path must not target public RPC URLs.
        self.assertNotIn("38546/rpc", wallet_js)
        self.assertNotIn("fetch(\"/api/rpc", wallet_js)

    def test_local_tools_and_worker_fail_closed(self) -> None:
        local = read("local/index.html")
        helper = read("local-rpc.js")
        worker = read("worker.js")
        wrangler = (PUBLIC / "wrangler.toml").read_text(encoding="utf-8")
        self.assertIn("/local-rpc", local)
        self.assertIn("127.0.0.1:8546", local)
        self.assertIn("wallet_send", local)
        self.assertIn('raw: "RPC offline"', helper)
        self.assertIn("env.PUBLIC_RPC_URL || env.PUBLIC_RPC_HTTP", worker)
        self.assertIn('path === "/local-rpc"', worker)
        self.assertIn("34.27.30.115:38546/rpc", wrangler)
        self.assertIsNone(re.search(r"(?m)^PUBLIC_RPC_HTTP\s*=", wrangler))
        workflow = (ROOT / ".github" / "workflows" / "site-cloudflare.yml").read_text(encoding="utf-8")
        self.assertIn("secrets.PUBLIC_RPC_HTTP", workflow)

    def test_readme_public_product_is_mainnet(self) -> None:
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        site_readme = read("README.md")
        self.assertIn("public product: mainnet", readme)
        self.assertIn("public_product-mainnet", readme)
        self.assertIn("ADDITION_MAINNET_V1", readme)
        self.assertIn("34.27.30.115:38546", readme)
        self.assertIn("100% to finding miner", readme)
        self.assertIn("addition-wallet-mainnet", readme)
        self.assertIn("127.0.0.1:8546", readme)
        self.assertNotIn("addition-wallet-testnet", readme)
        self.assertNotIn("public product: testnet", readme)
        self.assertIn("MAINNET", site_readme)
        self.assertIn("38546", site_readme)
        self.assertNotRegex(readme.lower(), r"\bhonest\b")

    def test_chrome_brand_stays_addition(self) -> None:
        chrome = read("chrome.js")
        self.assertIn('class="brand-name">ADDITION</span>', chrome)
        self.assertNotIn("SmartChain", chrome)

    def test_serve_defaults_to_mainnet_public_port(self) -> None:
        spec = importlib.util.spec_from_file_location("addition_serve", ROOT / "web" / "serve.py")
        self.assertIsNotNone(spec)
        assert spec is not None and spec.loader is not None
        serve = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(serve)
        self.assertIn("38546", (ROOT / "web" / "serve.py").read_text(encoding="utf-8"))
        self.assertNotIn("0.0.0.0:8545", (ROOT / "web" / "serve.py").read_text(encoding="utf-8"))

    def test_site_brand_assets_exist(self) -> None:
        for name in (
            "logo-transparent.png",
            "favicon.ico",
            "apple-touch-icon.png",
            "og.png",
            "icon-192.png",
            "icon-512.png",
        ):
            path = PUBLIC / name
            self.assertTrue(path.is_file(), name)
            self.assertGreater(path.stat().st_size, 32, name)

    def test_common_js_strip_and_allowlist(self) -> None:
        common = read("common.js")
        self.assertIn('raw: "RPC offline"', common)
        self.assertIn("STRIP_KEYS", common)
        self.assertIn("tx_status", common)
        self.assertIn("getblockraw", common)
        self.assertIn("explorerCommand", common)

    def test_no_hardcoded_fake_stats(self) -> None:
        index = read("index.html")
        chrome = read("chrome.js")
        status = read("status/index.html")
        blob = index + chrome + status
        self.assertNotIn("KH/s", blob)
        self.assertNotIn("1,248,500", blob)
        self.assertNotIn("124.8", blob)

    def test_strip_fields_and_rpc_fail_closed(self) -> None:
        node = shutil.which("node")
        if not node:
            self.skipTest("node is required for the RPC helper check")
        script = r"""
const fs = require("fs");
const vm = require("vm");
const path = require("path");
const code = fs.readFileSync(path.join("web", "public", "common.js"), "utf8");
const window = { location: { search: "" } };
function run(fetchImpl) {
  const sandbox = { window, URLSearchParams, fetch: fetchImpl };
  vm.runInNewContext(code, sandbox);
  return window.AdditionSite;
}
const S = run(async () => { throw new Error("offline"); });
const fields = S.parseFields("network=mainnet height=0 peers=1 pq_mode=strict pow_algorithm=memory_hard max_supply=50000000 next_reward=50 last_tps=9.99");
const strip = S.stripFields(fields);
if (strip.height !== "0" || strip.network !== "mainnet" || strip.pq_mode !== "strict") {
  throw new Error("missing live strip fields");
}
if (Object.prototype.hasOwnProperty.call(strip, "last_tps")) {
  throw new Error("strip leaked non-strip getinfo fields");
}
if (S.explorerAllowed("mine") || S.explorerAllowed("wallet_send")) {
  throw new Error("explorer must reject spend/mine");
}
S.rpcCommand("getinfo").then((offline) => {
  if (!offline.offline || offline.raw !== "RPC offline") {
    throw new Error("network error must fail closed");
  }
  console.log("helpers-ok");
}).catch((err) => {
  console.error(err);
  process.exit(1);
});
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
        self.assertIn("helpers-ok", proc.stdout)

    def test_static_pages_serve_and_rpc_offline(self) -> None:
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, fmt: str, *args) -> None:  # noqa: ARG002
                return

            def do_GET(self) -> None:
                if self.path.startswith("/api/rpc"):
                    body = b"RPC offline"
                    self.send_response(503)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                rel = "index.html" if self.path in {"/", "/index.html"} else self.path.lstrip("/")
                target = (PUBLIC / rel).resolve()
                if not str(target).startswith(str(PUBLIC.resolve())) or not target.is_file():
                    self.send_error(404)
                    return
                data = target.read_bytes()
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            index = urllib.request.urlopen(f"http://{host}:{port}/").read().decode("utf-8")
            self.assertIn("ADDITION_MAINNET_V1", index)
            self.assertIn("MAINNET", index)
            chrome = urllib.request.urlopen(f"http://{host}:{port}/chrome.js").read().decode("utf-8")
            self.assertIn("MAINNET", chrome)
            self.assertIn("Wallet", chrome)
            try:
                urllib.request.urlopen(f"http://{host}:{port}/api/rpc?cmd=getinfo")
                self.fail("offline RPC must not return HTTP 200")
            except urllib.error.HTTPError as exc:
                self.assertEqual(exc.code, 503)
                self.assertEqual(exc.read().decode("utf-8"), "RPC offline")
        finally:
            server.shutdown()
            server.server_close()

    def test_root_license_is_mit(self) -> None:
        license_text = (ROOT / "LICENSE").read_text(encoding="utf-8")
        self.assertIn("MIT License", license_text)
        legal = read("legal/index.html")
        self.assertIn("ADDITION_MAINNET_V1", legal)
        self.assertIn("MIT", legal)

    def test_embed_and_launch_are_mainnet_honest(self) -> None:
        embed = read("embed/index.html")
        launch = read("launch/index.html")
        worker = read("worker.js")
        self.assertIn("ADDITION", embed)
        self.assertIn("/api/info", embed)
        self.assertIn("Price unavailable", embed)
        self.assertNotIn("USDT", embed)
        self.assertNotIn("ghost", embed.lower())
        self.assertNotIn("SmartChain", embed)
        self.assertNotIn("0.0.0.0:8545", embed)
        self.assertIn("Create Token", launch)
        self.assertIn("Presale", launch)
        self.assertIn("Airdrop", launch)
        self.assertIn("Farm", launch)
        self.assertIn("/api/capabilities", launch)
        self.assertIn("does not expose", launch)
        self.assertIn("Panel stays empty.", launch)
        self.assertIn("disabled", launch)
        self.assertNotIn("SmartChain", launch)
        self.assertNotIn("0.0.0.0:8545", launch)
        self.assertIn('"/embed"', worker)
        self.assertIn('"/launch"', worker)
        self.assertIn("/api/info", worker)
        self.assertIn("/api/capabilities", worker)
        self.assertIn("price_available: false", worker)
        self.assertIn("price_usd: null", worker)
        self.assertIn("/api*", (PUBLIC / "wrangler.toml").read_text(encoding="utf-8"))

    def test_public_json_api_from_live_fields(self) -> None:
        serve_src = (ROOT / "web" / "serve.py").read_text(encoding="utf-8")
        self.assertIn("def public_json_info", serve_src)
        self.assertIn("def public_json_capabilities", serve_src)
        self.assertIn('"price_available": False', serve_src)
        self.assertIn('"price_usd": None', serve_src)
        self.assertIn("ADDITION_MAINNET_V1", serve_src)
        self.assertNotIn("0.0.0.0:8545", serve_src)


if __name__ == "__main__":
    unittest.main()
