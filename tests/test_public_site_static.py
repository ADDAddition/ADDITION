#!/usr/bin/env python3
"""Static copy and RPC checks for the ADDITION testnet site."""

from __future__ import annotations

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
    '["/explorer/", "navExplorer"]',
    '["/status/", "navStatus"]',
    '["/rpc/", "navRpc"]',
    '["/docs/", "navDocs"]',
    '["/wallet/", "navWallet"]',
    '["/legal/", "navLegal"]',
)

NAV_FORBIDDEN = (
    '["/swap/"',
    '["/evm/"',
    '["/contracts/"',
    '["/whitepaper/"',
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
        self.assertIn('STRIP_KEYS = ["height", "peers", "network", "pq_mode"]', chrome)
        self.assertIn("RPC offline", chrome)
        self.assertIn("emptyStripCells", chrome)
        self.assertIn("rpcQuerySuffix", chrome)

    def test_homepage_is_short(self) -> None:
        index = read("index.html")
        self.assertIn("ADDITION testnet", index)
        self.assertIn(":38545", index)
        self.assertIn("127.0.0.1:8545", index)
        self.assertIn("/rpc?cmd=getinfo", index)
        self.assertIn("34.27.30.115:28545", index)
        self.assertIn("sync", index)
        self.assertIn("/join/", index)
        self.assertIn('data-i18n="heroLede"', index)
        self.assertNotIn("/swap/", index)
        self.assertNotIn("/evm/", index)
        self.assertNotIn("/contracts/", index)
        self.assertNotIn("DEX", index)
        self.assertNotIn("CoinMarketCap", index)
        self.assertNotIn("token sale", index.lower())
        self.assertLess(len(index.splitlines()), 50)
        self.assertIn("SHA3-512", index)
        self.assertIn("getinfo", index)
        self.assertIn("getblockraw", index)

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
            "additiond --network testnet --data-dir &lt;dir&gt; --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545",
            join,
        )
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

    def test_wallet_stays_loopback_only(self) -> None:
        wallet = read("wallet/index.html")
        self.assertIn("/local-rpc", wallet)
        self.assertIn("127.0.0.1:8545", wallet)
        self.assertIn("createwallet", wallet)
        self.assertIn("disabled", wallet)
        self.assertIn("RPC offline", wallet)
        self.assertNotIn("wallet-connect", wallet.lower())

    def test_swap_and_contracts_fail_closed(self) -> None:
        swap = read("swap/index.html")
        contracts = read("contracts/index.html")
        self.assertIn("/local-rpc", swap)
        self.assertIn("127.0.0.1:8545", swap)
        self.assertIn("disabled", swap)
        self.assertIn("RPC offline", swap)
        self.assertIn("/local-rpc", contracts)
        self.assertIn("127.0.0.1:8545", contracts)
        self.assertIn("disabled", contracts)
        self.assertIn("RPC offline", contracts)
        self.assertNotIn("wallet-connect", swap.lower())

    def test_common_js_fail_closed_and_strip_keys(self) -> None:
        common = read("common.js")
        self.assertIn('raw: "RPC offline"', common)
        self.assertIn("looksLikeHtml", common)
        self.assertIn('const keys = ["height", "peers", "network", "pq_mode"]', common)
        self.assertIn("stripFields", common)

    def test_explorer_still_calls_getblock(self) -> None:
        explorer = read("explorer/index.html")
        self.assertIn('S.rpcCommand("getblock "', explorer)
        self.assertIn("RPC offline", explorer)
        self.assertIn("loadRecent", explorer)

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
            self.assertIn("ADDITION testnet", index)
            self.assertIn("/common.css", index)
            chrome = urllib.request.urlopen(f"http://{host}:{port}/chrome.js").read().decode("utf-8")
            self.assertIn("navExplorer", chrome)
            try:
                urllib.request.urlopen(f"http://{host}:{port}/api/rpc?cmd=getinfo")
                self.fail("offline RPC must not return HTTP 200")
            except urllib.error.HTTPError as exc:
                self.assertEqual(exc.code, 503)
                self.assertEqual(exc.read().decode("utf-8"), "RPC offline")
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
