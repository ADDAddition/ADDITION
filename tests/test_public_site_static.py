#!/usr/bin/env python3
"""Static copy and RPC checks for the ADDITION testnet site."""

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
    "live L1",
    "privacy ZK",
    "Range Proofs",
    "Roadmap to Production",
    "labjay69",
    "labreche_jeremy@outlook",
    "Addison Electronics",
    "Addison Electronique",
)

SERMONS = (
    "honest testnet",
    "honest website",
    "research notice",
    "never invent",
    "we never invent",
    "fail closed",
    "fail-closed",
)

NAV_REQUIRED = (
    '["/", "Explorer"]',
    '["/status/", "Status"]',
    '["/join/", "Join"]',
)

NAV_FORBIDDEN = (
    '["/swap/"',
    '["/evm/"',
    '["/contracts/"',
    '["/whitepaper/"',
    '["/wallet/"',
    '["/rpc/"',
    '["/docs/"',
    '["/legal/"',
    '["/explorer/"',
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
        index = read("index.html")
        self.assertNotIn("labjay69", index)
        self.assertNotIn("outlook.com", index)

    def test_chrome_nav_is_short(self) -> None:
        chrome = read("chrome.js")
        for item in NAV_REQUIRED:
            self.assertIn(item, chrome)
        for item in NAV_FORBIDDEN:
            self.assertNotIn(item, chrome)
        self.assertIn("rpcQuerySuffix", chrome)
        self.assertIn("testnet · ", chrome)
        self.assertIn("contact@additionblockchain.com", chrome)
        self.assertNotIn("8545", chrome)
        self.assertNotIn("Wallet", chrome)
        self.assertNotIn("STRIP_KEYS", chrome)
        self.assertNotIn("emptyStripCells", chrome)
        self.assertNotIn("lang-en", chrome)

    def test_homepage_is_explorer(self) -> None:
        index = read("index.html")
        chrome = read("chrome.js")
        status = read("status/index.html")
        explorer_js = read("explorer.js")
        self.assertIn('placeholder="block height, block hash, tx hash, address"', index)
        self.assertIn("<th>height</th>", index)
        self.assertIn("<th>hash</th>", index)
        self.assertIn("<th>tx_count</th>", index)
        self.assertIn("<th>time</th>", index)
        self.assertIn("Latest Blocks", index)
        self.assertIn("/explorer.js", index)
        self.assertNotIn("hero", index)
        self.assertNotIn("cards", index)
        self.assertNotIn("8545", index)
        self.assertNotIn("8545", chrome)
        self.assertNotIn("8545", status)
        self.assertNotIn("8545", explorer_js)
        self.assertNotIn("Wallet", index)
        self.assertNotIn("/swap/", index)
        self.assertNotIn("/evm/", index)
        self.assertNotIn("/contracts/", index)
        self.assertNotIn("DEX", index)
        self.assertNotIn("CoinMarketCap", index)
        self.assertNotIn("token sale", index.lower())
        self.assertNotIn("market cap", index.lower())
        self.assertNotIn("hashrate", index.lower())
        self.assertNotIn("mainnet", index.lower())
        self.assertLess(len(index.splitlines()), 50)
        self.assertNotIn("004d9744", index)
        self.assertNotIn("tx_status", explorer_js)
        self.assertNotIn("getblockraw", explorer_js)

    def test_rpc_page_documents_allowlist_and_json(self) -> None:
        rpc = read("rpc/index.html")
        self.assertIn("getblockraw", rpc)
        self.assertIn("/jsonrpc?method=getinfo", rpc)
        self.assertIn("34.27.30.115:28545", rpc)
        self.assertIn(":80", rpc)
        self.assertIn(":38545", rpc)
        self.assertIn("127.0.0.1:8545", rpc)
        self.assertNotIn("labreche_jeremy", rpc)
        self.assertNotIn("outlook.com", rpc)

    def test_join_docs_state_live_bootstrap_and_sync(self) -> None:
        join = read("join/index.html")
        docs = read("docs/index.html")
        started = read("docs/getting-started/index.html")
        rpc = read("rpc/index.html")
        chrome = read("chrome.js")
        worker = read("worker.js")
        blob = join + docs + started + rpc + chrome
        self.assertIn(
            "additiond --network testnet --data-dir $HOME/addition-testnet --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545",
            join,
        )
        self.assertNotIn("--data-dir &lt;dir&gt;", join)
        self.assertNotIn("--data-dir <dir>", join)
        self.assertIn("sync", join)
        self.assertIn("invalid/duplicate", join)
        self.assertIn("/rpc?cmd=getinfo", join)
        self.assertIn("https://rpc.additionblockchain.com/rpc?cmd=getinfo", join)
        self.assertIn("http://34.27.30.115/rpc?cmd=getinfo", join)
        self.assertIn(":80", join)
        self.assertIn("getblockraw", join)
        self.assertIn("ok:BLKDATA", join)
        self.assertIn("HELLO", join)
        self.assertIn("127.0.0.1", join)
        self.assertIn("error: command disabled on public RPC", join)
        self.assertIn("<td>80</td>", join)
        self.assertIn("<td>38545</td>", join)
        self.assertIn("<td>28545</td>", join)
        self.assertIn("Sync uses :80 first", join)
        self.assertIn("optional/filtered", join)
        self.assertIn("Research testnet", join)
        self.assertIn("ADDITION_ADVERTISED_P2P=34.27.30.115:28545", join)
        self.assertIn("do not list <code>self</code>", join)
        self.assertIn("Do not claim public P2P 28545 always works", join)
        self.assertIn("when the operator seed answers", join)
        self.assertIn("when the operator seed answers", rpc)
        self.assertIn("/join/", docs)
        self.assertIn("34.27.30.115:28545", started)
        self.assertIn("/rpc?cmd=getinfo", rpc)
        self.assertIn("getblockraw", rpc)
        self.assertIn('"/join": "/join/index.html"', worker)
        self.assertNotIn("cloudflared", blob.lower())
        self.assertNotIn("0.0.0.0:8545", blob)
        self.assertNotIn("https://rpc.additionblockchain.com/getinfo", blob)
        self.assertNotIn("http://34.27.30.115/getinfo", blob)

    def test_evm_page_is_local_testnet_only(self) -> None:
        evm = read("evm/index.html")
        self.assertIn("127.0.0.1:9545", evm)
        self.assertIn("424242", evm)
        self.assertIn("send disabled", evm.lower())
        self.assertIn("eth_sendRawTransaction", evm)
        self.assertIn("wallet_addEthereumChain", evm)
        self.assertIn("http://127.0.0.1:9545", evm)
        self.assertNotIn("0.0.0.0:9545", evm)
        self.assertNotIn("wallet-connect", evm.lower())
        self.assertIn("cannot list", evm.lower())
        self.assertIn('id="add-mm"', evm)
        self.assertIn("disabled", evm)
        self.assertIn("Button stays disabled until http://127.0.0.1:9545 answers chain 424242.", evm)
        self.assertNotIn("DEX", evm)

    def test_site_brand_assets_exist_and_are_referenced(self) -> None:
        required = (
            "logo-transparent.png",
            "favicon.ico",
            "favicon-32.png",
            "apple-touch-icon.png",
            "og.png",
            "twitter.png",
        )
        for name in required:
            path = PUBLIC / name
            self.assertTrue(path.is_file(), name)
            self.assertGreater(path.stat().st_size, 32, name)
        logo = (PUBLIC / "logo-transparent.png").read_bytes()
        source = (ROOT / "docs" / "assets" / "logo-transparent.png").read_bytes()
        self.assertEqual(logo, source)
        self.assertTrue(logo.startswith(b"\x89PNG"))
        chrome = read("chrome.js")
        self.assertIn("/logo-transparent.png", chrome)
        self.assertIn("/download/", chrome)
        index = read("index.html")
        self.assertIn("/favicon.ico", index)
        self.assertIn("/apple-touch-icon.png", index)
        self.assertIn("/og.png", index)
        self.assertIn("twitter:card", index)
        self.assertLess(len(index.splitlines()), 50)

    def test_download_page_is_testnet_local_only(self) -> None:
        page = read("download/index.html")
        worker = read("worker.js")
        self.assertIn("testnet / local", page.lower())
        self.assertIn("addition-wallet-testnet", page)
        self.assertIn("addition-wallet-cli-testnet", page)
        self.assertNotIn("addition-wallet-testnet.exe", page)
        self.assertNotIn("addition-wallet-cli-testnet.exe", page)
        self.assertNotIn("/download/addition-wallet-testnet.exe", page)
        self.assertIn("127.0.0.1:8545", page)
        self.assertIn("chmod +x", page)
        self.assertIn("--data-dir $HOME/addition-testnet", page)
        self.assertIn("Cashiers / Windows", page)
        self.assertIn("Do not compile", page)
        self.assertIn("RPC offline", page)
        self.assertIn("contact@additionblockchain.com", page)
        self.assertIn('"/download": "/download/index.html"', worker)
        self.assertNotIn("powershell -File", page.lower())
        self.assertNotIn("```powershell", page)
        self.assertNotIn("build_wallet.ps1", page)
        self.assertNotIn("build_wallet.sh", page)
        self.assertNotIn("liboqs", page.lower())
        self.assertNotIn("--data-dir <dir>", page)
        self.assertNotIn("--data-dir &lt;dir&gt;", page)
        self.assertNotIn("wallet-connect", page.lower())
        self.assertNotIn("walletconnect", page.lower())
        self.assertNotIn("token sale", page.lower())
        self.assertNotIn("tokenomics", page.lower())
        self.assertNotIn("market cap", page.lower())
        self.assertNotIn("coinmarketcap", page.lower())
        self.assertIn("not a hosted web wallet", page.lower())
        self.assertNotIn("mainnet is live", page.lower())
        self.assertNotIn("live mainnet", page.lower())
        self.assertNotRegex(page.lower(), r"\bhonest\b")

    def test_readme_linux_compile_path(self) -> None:
        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        started = read("docs/getting-started/index.html")
        self.assertIn("--data-dir $HOME/addition-testnet", readme)
        self.assertIn("--data-dir $HOME/addition-testnet", started)
        self.assertIn("Ubuntu EliteDesk", readme)
        self.assertIn("Cashiers / Windows", readme)
        self.assertIn("https://additionblockchain.com/download/", readme)
        self.assertIn("contact@additionblockchain.com", readme)
        self.assertIn("sudo apt-get", readme)
        self.assertIn("127.0.0.1", readme)
        self.assertNotIn("--data-dir <dir>", readme)
        self.assertNotIn("```powershell", readme)
        self.assertNotIn("powershell -File", readme.lower())
        self.assertNotIn("0.0.0.0:8545", readme)
        self.assertNotIn("Addison", readme)
        self.assertNotRegex(readme.lower(), r"\bhonest\b")
        self.assertNotIn("powershell -File", started.lower())
        self.assertNotIn("```powershell", started)
        self.assertNotIn("--data-dir &lt;dir&gt;", started)

    def test_wallet_stays_loopback_only(self) -> None:
        wallet = read("wallet/index.html")
        self.assertIn("/local-rpc", wallet)
        self.assertIn("127.0.0.1:8545", wallet)
        self.assertIn("createwallet", wallet)
        self.assertIn("disabled", wallet)
        self.assertIn("RPC offline", wallet)
        self.assertIn("mine ", wallet)
        self.assertIn("stake ", wallet)
        self.assertIn("unstake ", wallet)
        self.assertIn("stake_claim", wallet)
        self.assertNotIn("wallet-connect", wallet.lower())

    def test_swap_and_contracts_fail_closed(self) -> None:
        swap = read("swap/index.html")
        contracts = read("contracts/index.html")
        self.assertIn("/local-rpc", swap)
        self.assertIn("127.0.0.1:8545", swap)
        self.assertIn("disabled", swap)
        self.assertIn("RPC offline", swap)
        self.assertIn("swap_pool_create", swap)
        self.assertIn("add_liquidity", swap)
        self.assertIn("swap_exact_in", swap)
        self.assertIn("swap_exact_in_wallet", swap)
        self.assertIn("swap_tvl", swap)
        self.assertIn("/local-rpc", contracts)
        self.assertIn("127.0.0.1:8545", contracts)
        self.assertIn("disabled", contracts)
        self.assertIn("RPC offline", contracts)
        self.assertIn("/tokens/", contracts)
        self.assertNotIn("wallet-connect", swap.lower())

    def test_local_tools_pages_fail_closed(self) -> None:
        local = read("local/index.html")
        tokens = read("tokens/index.html")
        privacy = read("privacy/index.html")
        helper = read("local-rpc.js")
        nav = read("local-nav.js")
        worker = read("worker.js")
        docs = read("docs/index.html")
        join = read("join/index.html")
        for page in (local, tokens, privacy):
            self.assertIn("/local-rpc", page)
            self.assertIn("127.0.0.1:8545", page)
            self.assertIn("disabled", page)
            self.assertIn("RPC offline", page)
            self.assertNotIn("wallet-connect", page.lower())
        self.assertIn("token_create", tokens)
        self.assertIn("token_mint", tokens)
        self.assertIn("token_transfer", tokens)
        self.assertIn("token_transfer_wallet", tokens)
        self.assertIn("token_burn", tokens)
        self.assertIn("privacy_note_prepare", privacy)
        self.assertIn("privacy_mint_open", privacy)
        self.assertIn("privacy_spend_open", privacy)
        self.assertIn("opening_not_zk", privacy)
        self.assertIn("this page does not call it", privacy)
        self.assertIn("ADDITION_PRIVACY_MASTER_KEY", privacy)
        self.assertIn('raw: "RPC offline"', helper)
        self.assertIn("/local-rpc?cmd=", helper)
        self.assertIn('["/local/", "Local"]', nav)
        self.assertIn('["/tokens/", "Tokens"]', nav)
        self.assertIn('["/privacy/", "Privacy"]', nav)
        self.assertIn('"/local": "/local/index.html"', worker)
        self.assertIn("function rpcPath(url)", worker)
        self.assertIn('path === "/jsonrpc"', worker)
        wrangler = (PUBLIC / "wrangler.toml").read_text(encoding="utf-8")
        self.assertIn("run_worker_first", wrangler)
        self.assertIn('"/rpc*"', wrangler)
        self.assertIn("rpc.additionblockchain.com/*", wrangler)
        commands = read("docs/commands/index.html")
        whitepaper = read("whitepaper/index.html")
        self.assertIn("token_transfer_wallet", commands)
        self.assertIn("when the operator seed answers", commands)
        self.assertIn("token_transfer_wallet", whitepaper)
        self.assertIn("ADDITION_PRIVACY_MASTER_KEY", whitepaper)
        self.assertIn('"/tokens": "/tokens/index.html"', worker)
        self.assertIn('"/privacy": "/privacy/index.html"', worker)
        self.assertIn("/local/", docs)
        self.assertIn("/tokens/", docs)
        self.assertIn("/privacy/", docs)
        self.assertIn("/local/", join)
        self.assertIn("127.0.0.1:8546", local)
        self.assertIn("ADDITION_MAINNET_V1", local)
        self.assertIn("not a public network", local)
        self.assertIn("create pair + pool", local)
        self.assertIn("token_create", local)
        self.assertIn("swap_pool_create", local)
        self.assertIn("Not a hosted web wallet, Uniswap, or public token sale", local)
        self.assertIn("min_fee=0", local)
        self.assertIn("opening_not_zk", local)
        self.assertIn("ml-dsa-87", local.lower())
        self.assertIn("Not Monero, not Zcash", local)
        self.assertIn("research_goal_tps", local)
        self.assertIn("Not IBC", local)

    def test_common_js_fail_closed_and_strip_keys(self) -> None:
        common = read("common.js")
        self.assertIn('raw: "RPC offline"', common)
        self.assertIn("looksLikeHtml", common)
        self.assertIn('const keys = ["height", "peers", "network", "pq_mode"]', common)
        self.assertIn("stripFields", common)

    def test_explorer_still_calls_getblock(self) -> None:
        index = read("index.html")
        explorer_js = read("explorer.js")
        common = read("common.js")
        redirect = read("explorer/index.html")
        self.assertIn("/explorer.js", index)
        self.assertIn("loadLatestBlockRows", explorer_js)
        self.assertIn("RPC offline", explorer_js)
        self.assertIn("Not found", explorer_js)
        self.assertIn('return "getblock " + id', common)
        self.assertIn("explorerCommand", common)
        self.assertIn('url=/', redirect)

    def test_no_hardcoded_live_stats(self) -> None:
        index = read("index.html")
        chrome = read("chrome.js")
        css = read("common.css")
        blob = index + chrome + css
        self.assertIsNone(re.search(r"\b42\b", blob))
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
const fields = S.parseFields("network=testnet height=20 peers=1 pq_mode=strict pow_algorithm=sha3_512 last_tps=9.99");
const strip = S.stripFields(fields);
if (strip.height !== "20" || strip.peers !== "1" || strip.network !== "testnet" || strip.pq_mode !== "strict") {
  throw new Error("missing live strip fields");
}
if (Object.prototype.hasOwnProperty.call(strip, "last_tps") || Object.prototype.hasOwnProperty.call(strip, "pow_algorithm")) {
  throw new Error("strip leaked non-strip getinfo fields");
}
if (!S.isHeightQuery("200") || S.isHeightQuery("00ab") || S.blockSearchCommand("200") !== "getblock 200") {
  throw new Error("height search must use getblock");
}
if (S.blockSearchCommand("004d") !== "getblock 004d") {
  throw new Error("hash search must use getblock");
}
const row = S.blockRowFromFields({ height: "200", hash: "abc", tx_count: "1", timestamp: "1786877815" });
if (row.height !== "200" || row.hash !== "abc" || row.tx_count !== "1" || !row.time) {
  throw new Error("block row must copy live getblock fields only");
}
const emptyRow = S.blockRowFromFields({});
if (Object.keys(emptyRow).length !== 0) {
  throw new Error("empty getblock fields must not invent a row");
}
if (S.explorerAllowed("tx_status abc") || S.explorerAllowed("peers")) {
  throw new Error("explorer must not call extra RPC commands");
}
S.rpcCommand("getinfo").then((offline) => {
  if (!offline.offline || offline.raw !== "RPC offline" || Object.keys(offline.fields).length !== 0) {
    throw new Error("network error must fail closed");
  }
  const html = run(async () => ({
    ok: false,
    status: 404,
    text: async () => "<!DOCTYPE html><html><body>missing</body></html>"
  }));
  return html.rpcCommand("getinfo");
}).then((htmlOff) => {
  if (!htmlOff.offline || htmlOff.raw !== "RPC offline") {
    throw new Error("HTML 404 must fail closed");
  }
  return S.loadLatestBlockRows(3);
}).then((emptyLatest) => {
  if (!emptyLatest.offline || emptyLatest.blocks.length !== 0) {
    throw new Error("offline latest blocks must be empty");
  }
  const blocked = run(async () => ({ ok: true, status: 200, text: async () => "ok" }));
  return blocked.explorerCommand("tx_status abc");
}).then((denied) => {
  if (denied.ok || denied.raw !== "Not found") {
    throw new Error("disallowed explorer command must be Not found");
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
            self.assertIn("ADDITION explorer", index)
            self.assertIn("/common.css", index)
            self.assertIn("latest-blocks", index)
            chrome = urllib.request.urlopen(f"http://{host}:{port}/chrome.js").read().decode("utf-8")
            self.assertIn("Explorer", chrome)
            self.assertIn("Status", chrome)
            self.assertIn("Join", chrome)
            logo = urllib.request.urlopen(f"http://{host}:{port}/logo-transparent.png").read()
            self.assertTrue(logo.startswith(b"\x89PNG"))
            download = urllib.request.urlopen(f"http://{host}:{port}/download/index.html").read().decode("utf-8")
            self.assertIn("addition-wallet-testnet", download)
            self.assertIn("testnet", download.lower())
            try:
                urllib.request.urlopen(f"http://{host}:{port}/api/rpc?cmd=getinfo")
                self.fail("offline RPC must not return HTTP 200")
            except urllib.error.HTTPError as exc:
                self.assertEqual(exc.code, 503)
                self.assertEqual(exc.read().decode("utf-8"), "RPC offline")
        finally:
            server.shutdown()
            server.server_close()

    def test_raw_markdown_docs_are_fetchable(self) -> None:
        join_md = read("join.md")
        docs_join_md = read("docs/join.md")
        runbook = read("docs/testnet-rpc-runbook.md")
        wallet = read("docs/wallet.md")
        worker = read("worker.js")
        headers = read("_headers")
        docs_index = read("docs/index.html")
        join_html = read("join/index.html")
        download_html = read("download/index.html")

        self.assertEqual(runbook, (ROOT / "docs" / "TESTNET_PUBLIC_RPC_RUNBOOK.md").read_text(encoding="utf-8"))
        self.assertEqual(wallet, (ROOT / "docs" / "WALLET.md").read_text(encoding="utf-8"))
        self.assertEqual(docs_join_md, join_md)
        self.assertNotIn("<!DOCTYPE html>", join_md)
        self.assertNotIn("<!DOCTYPE html>", docs_join_md)
        self.assertIn(
            "additiond --network testnet --data-dir $HOME/addition-testnet --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545",
            join_md,
        )
        self.assertNotIn("--data-dir <dir>", join_md)
        self.assertIn("sync", join_md)
        self.assertIn("invalid/duplicate", join_md)
        self.assertIn("/rpc?cmd=getinfo", join_md)
        self.assertIn("https://rpc.additionblockchain.com/rpc?cmd=getinfo", join_md)
        self.assertIn("http://34.27.30.115/rpc?cmd=getinfo", join_md)
        self.assertIn("getblockraw", join_md)
        self.assertIn("ok:BLKDATA", join_md)
        self.assertIn("HELLO", join_md)
        self.assertIn("127.0.0.1", join_md)
        self.assertIn("error: command disabled on public RPC", join_md)
        self.assertIn("ADDITION_ADVERTISED_P2P=34.27.30.115:28545", join_md)
        self.assertIn("Research testnet", join_md)
        self.assertIn("contact@additionblockchain.com", join_md)
        self.assertNotIn("0.0.0.0:8545", join_md)
        self.assertNotIn("wallet-connect", join_md.lower())
        self.assertNotIn("token sale", join_md.lower())
        self.assertNotRegex(join_md.lower(), r"\bhonest\b")
        self.assertIn("38545", runbook)
        self.assertIn("127.0.0.1", runbook)
        self.assertIn("Never publish **8545**", runbook)
        self.assertIn("127.0.0.1:8545", wallet)
        self.assertIn("loopback", wallet.lower())
        self.assertIn('url.pathname.endsWith(".md")', worker)
        self.assertIn("text/markdown; charset=utf-8", worker)
        self.assertIn("text/markdown; charset=utf-8", headers)
        self.assertIn("/join.md", docs_index)
        self.assertIn("/docs/join.md", docs_index)
        self.assertIn("/docs/testnet-rpc-runbook.md", docs_index)
        self.assertIn("/docs/wallet.md", docs_index)
        self.assertIn("Local desktop helper", join_html)
        self.assertIn("/download/", join_html)
        self.assertIn("testnet / local", download_html.lower())
        self.assertIn("127.0.0.1:8545", download_html)

        spec = importlib.util.spec_from_file_location("addition_serve", ROOT / "web" / "serve.py")
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        serve = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(serve)
        for path in ("/join.md", "/docs/join.md", "/docs/testnet-rpc-runbook.md", "/docs/wallet.md"):
            target = serve.resolve_static(path)
            self.assertIsNotNone(target, path)
            self.assertEqual(serve.content_type(target), "text/markdown; charset=utf-8", path)
        self.assertEqual(serve.resolve_static("/join/").name, "index.html")
        self.assertEqual(serve.resolve_static("/download/").name, "index.html")

        server = ThreadingHTTPServer(("127.0.0.1", 0), serve.Handler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = server.server_address
            for path, needle in (
                ("/join.md", "bootstrap 34.27.30.115:28545"),
                ("/docs/join.md", "bootstrap 34.27.30.115:28545"),
                ("/docs/testnet-rpc-runbook.md", "public-read"),
                ("/docs/wallet.md", "127.0.0.1:8545"),
            ):
                with urllib.request.urlopen(f"http://{host}:{port}{path}") as resp:
                    self.assertIn("text/markdown", resp.headers.get("Content-Type", ""))
                    body = resp.read().decode("utf-8")
                self.assertIn(needle, body)
                self.assertNotIn("<!DOCTYPE html>", body)
            with urllib.request.urlopen(f"http://{host}:{port}/join/") as resp:
                self.assertIn("text/html", resp.headers.get("Content-Type", ""))
                self.assertIn("Join the ADDITION testnet", resp.read().decode("utf-8"))
            with urllib.request.urlopen(f"http://{host}:{port}/download/") as resp:
                self.assertIn("text/html", resp.headers.get("Content-Type", ""))
                self.assertIn("addition-wallet-testnet", resp.read().decode("utf-8"))
        finally:
            server.shutdown()
            server.server_close()

    def test_root_license_is_mit(self) -> None:
        license_text = (ROOT / "LICENSE").read_text(encoding="utf-8")
        self.assertIn("MIT License", license_text)
        self.assertIn("Permission is hereby granted, free of charge", license_text)
        legal = read("legal/index.html")
        self.assertIn("LICENSE", legal)
        self.assertIn("MIT", legal)
        docs = read("docs/index.html")
        self.assertIn("SHA3 opening notes", docs)
        self.assertNotIn("ZK verifier contract", docs)


if __name__ == "__main__":
    unittest.main()
