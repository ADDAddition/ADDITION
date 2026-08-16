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
        self.assertIn("<td>80</td>", join)
        self.assertIn("<td>38545</td>", join)
        self.assertIn("<td>28545</td>", join)
        self.assertIn("Sync uses :80 first", join)
        self.assertIn("optional/filtered", join)
        self.assertIn("Research testnet", join)
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
