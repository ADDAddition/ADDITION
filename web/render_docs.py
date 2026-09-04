#!/usr/bin/env python3
"""Render selected repo markdown docs into static HTML under web/public/."""

from __future__ import annotations

import html
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "web" / "public"

SHELL = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{title}</title>
  <link rel="icon" href="/favicon.ico"><link rel="apple-touch-icon" href="/apple-touch-icon.png">
  <meta property="og:image" content="https://additionblockchain.com/og.png"><meta name="twitter:image" content="https://additionblockchain.com/og.png">
  <meta name="twitter:card" content="summary_large_image">
  <link rel="stylesheet" href="/common.css">
  {extra_head}
</head>
<body data-title="{heading}" data-sub="{sub}">
  <header id="site-header"></header>
  <main class="prose">
    <div class="banner">{banner}</div>
    {body}
    <p class="note">Source file in the repository: <a href="{github}">{source}</a></p>
  </main>
  <footer id="site-footer"></footer>
  <script src="/chrome.js"></script>
</body>
</html>
"""

TOC_STYLE = """<style>
.wp-toc { margin: 1rem 0 1.5rem; padding: 0.85rem 1rem; border: 1px solid var(--line); border-radius: 12px; background: rgba(47, 95, 138, 0.06); }
.wp-toc strong { display: block; margin-bottom: 0.5rem; }
.wp-toc ol { margin: 0; padding-left: 1.2rem; }
.wp-toc li { margin: 0.25rem 0; }
.prose h2[id], .prose h3[id] { scroll-margin-top: 5rem; }
.prose h2[id]:target, .prose h3[id]:target { outline: 2px solid rgba(47, 95, 138, 0.35); outline-offset: 4px; border-radius: 4px; }
</style>
"""


def slugify(text: str) -> str:
    plain = re.sub(r"[*`]", "", text).strip().lower()
    plain = re.sub(r"[^a-z0-9]+", "-", plain)
    return plain.strip("-") or "section"


def inline(text: str) -> str:
    text = html.escape(text)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', text)
    return text


def split_table_cells(row: str) -> list[str]:
    """Split a markdown table row on '|' while keeping pipes inside `code` spans."""
    body = row.strip()
    if body.startswith("|"):
        body = body[1:]
    if body.endswith("|"):
        body = body[:-1]
    cells: list[str] = []
    cur: list[str] = []
    in_code = False
    i = 0
    while i < len(body):
        ch = body[i]
        if ch == "`":
            in_code = not in_code
            cur.append(ch)
        elif ch == "|" and not in_code:
            cells.append("".join(cur).strip())
            cur = []
        elif ch == "\\" and not in_code and i + 1 < len(body) and body[i + 1] == "|":
            # Markdown escaped pipe outside code — keep as literal '|'.
            cur.append("|")
            i += 1
        else:
            cur.append(ch)
        i += 1
    cells.append("".join(cur).strip())
    return cells


def md_to_html(md: str, *, with_anchors: bool = False, skip_md_toc: bool = False) -> str:
    lines = md.replace("\r\n", "\n").split("\n")
    out: list[str] = []
    i = 0
    in_code = False
    code: list[str] = []
    list_items: list[str] = []
    ordered_items: list[str] = []
    table_rows: list[str] = []
    used_ids: dict[str, int] = {}
    skipping_toc = False

    def unique_id(raw: str) -> str:
        base = slugify(raw)
        n = used_ids.get(base, 0)
        used_ids[base] = n + 1
        return base if n == 0 else f"{base}-{n}"

    def flush_list() -> None:
        if list_items:
            out.append("<ul>" + "".join("<li>" + inline(x) + "</li>" for x in list_items) + "</ul>")
            list_items.clear()
        if ordered_items:
            out.append("<ol>" + "".join("<li>" + inline(x) + "</li>" for x in ordered_items) + "</ol>")
            ordered_items.clear()

    def flush_table() -> None:
        if not table_rows:
            return
        rows = []
        for idx, row in enumerate(table_rows):
            cells = split_table_cells(row)
            tag = "th" if idx == 0 else "td"
            if idx == 1 and all(re.fullmatch(r":?-{3,}:?", c.replace(" ", "")) for c in cells):
                continue
            rows.append("<tr>" + "".join(f"<{tag}>{inline(c)}</{tag}>" for c in cells) + "</tr>")
        out.append("<table>" + "".join(rows) + "</table>")
        table_rows.clear()

    while i < len(lines):
        line = lines[i]
        if line.startswith("```"):
            flush_list()
            flush_table()
            skipping_toc = False
            if in_code:
                out.append("<pre>" + html.escape("\n".join(code)) + "</pre>")
                code.clear()
                in_code = False
            else:
                in_code = True
            i += 1
            continue
        if in_code:
            code.append(line)
            i += 1
            continue

        # Drop the markdown TOC block; HTML gets a generated TOC instead.
        if skip_md_toc and re.match(r"^##\s+Table of contents\s*$", line, re.I):
            skipping_toc = True
            i += 1
            continue
        if skipping_toc:
            if line.startswith("## ") or line.startswith("# "):
                skipping_toc = False
            else:
                i += 1
                continue

        if line.startswith("|"):
            flush_list()
            table_rows.append(line)
            i += 1
            continue
        flush_table()
        if re.match(r"^\s*\d+\.\s+", line):
            if list_items:
                out.append("<ul>" + "".join("<li>" + inline(x) + "</li>" for x in list_items) + "</ul>")
                list_items.clear()
            ordered_items.append(re.sub(r"^\s*\d+\.\s+", "", line))
            i += 1
            continue
        if re.match(r"^\s*[-*]\s+", line):
            if ordered_items:
                out.append("<ol>" + "".join("<li>" + inline(x) + "</li>" for x in ordered_items) + "</ol>")
                ordered_items.clear()
            list_items.append(re.sub(r"^\s*[-*]\s+", "", line))
            i += 1
            continue
        flush_list()
        if line.startswith("# "):
            title = line[2:]
            if with_anchors:
                hid = unique_id(title)
                out.append(f'<h2 id="{html.escape(hid)}">' + inline(title) + "</h2>")
            else:
                out.append("<h2>" + inline(title) + "</h2>")
        elif line.startswith("## "):
            title = line[3:]
            if with_anchors:
                hid = unique_id(title)
                out.append(f'<h2 id="{html.escape(hid)}">' + inline(title) + "</h2>")
            else:
                out.append("<h2>" + inline(title) + "</h2>")
        elif line.startswith("### "):
            title = line[4:]
            if with_anchors:
                hid = unique_id(title)
                out.append(f'<h3 id="{html.escape(hid)}">' + inline(title) + "</h3>")
            else:
                out.append("<h3>" + inline(title) + "</h3>")
        elif line.startswith("> "):
            out.append("<p class='note'>" + inline(line[2:]) + "</p>")
        elif line.strip() == "---":
            out.append("<hr>")
        elif line.strip():
            out.append("<p>" + inline(line) + "</p>")
        i += 1
    flush_list()
    flush_table()
    if in_code:
        out.append("<pre>" + html.escape("\n".join(code)) + "</pre>")
    return "\n".join(out)


def build_toc(md: str) -> str:
    items: list[tuple[str, str]] = []
    used: dict[str, int] = {}
    for line in md.replace("\r\n", "\n").split("\n"):
        if line.startswith("```"):
            continue
        m = re.match(r"^##\s+(.+)$", line)
        if not m:
            continue
        title = m.group(1).strip()
        if title.lower() == "table of contents":
            continue
        base = slugify(title)
        n = used.get(base, 0)
        used[base] = n + 1
        hid = base if n == 0 else f"{base}-{n}"
        items.append((hid, title))
    if not items:
        return ""
    lis = "".join(
        f'<li><a href="#{html.escape(hid)}">{inline(title)}</a></li>' for hid, title in items
    )
    return f'<nav class="wp-toc" aria-label="Table of contents"><strong>Contents</strong><ol>{lis}</ol></nav>'


def publish_raw(rel_md: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text((ROOT / rel_md).read_text(encoding="utf-8"), encoding="utf-8")


def write_doc(
    rel_md: str,
    dest: Path,
    title: str,
    heading: str,
    banner: str,
    *,
    with_toc: bool = False,
) -> None:
    md = (ROOT / rel_md).read_text(encoding="utf-8")
    dest.parent.mkdir(parents=True, exist_ok=True)
    body = md_to_html(md, with_anchors=with_toc, skip_md_toc=with_toc)
    if with_toc:
        body = build_toc(md) + "\n" + body
    dest.write_text(
        SHELL.format(
            title=html.escape(title),
            heading=html.escape(heading),
            sub="from the repo",
            banner=banner,
            body=body,
            github="https://github.com/ADDAddition/ADDITION/blob/main/" + rel_md,
            source=rel_md,
            extra_head=TOC_STYLE if with_toc else "",
        ),
        encoding="utf-8",
    )


WHITEPAPER_LIVE_SCRIPT = """
  <script src="/common.js"></script>
  <script src="/chrome.js"></script>
  <script>
    (function () {
      const S = window.AdditionSite;
      const pre = document.getElementById("live-getinfo-snapshot");
      const note = document.getElementById("live-getinfo-note");
      if (!S || !pre) {
        return;
      }
      const monetary = document.getElementById("live-monetary-snapshot");
      S.explorerCommand("getinfo").then(function (result) {
        if (result.offline || !result.ok) {
          pre.textContent = "RPC offline";
          if (monetary) {
            monetary.textContent = "RPC offline";
          }
          if (note) {
            note.textContent = "Fail closed — no sample getinfo substituted.";
          }
          return;
        }
        pre.textContent = result.raw || "";
        if (note) {
          const h = S.fieldOrNull(result.fields || {}, "height");
          const p = S.fieldOrNull(result.fields || {}, "peers");
          note.textContent = "Copied from live getinfo"
            + (h !== null ? (" · height=" + h) : "")
            + (p !== null ? (" · peers=" + p) : "")
            + ".";
        }
      });
      if (monetary) {
        S.explorerCommand("monetary_info").then(function (result) {
          if (result.offline || !result.ok) {
            monetary.textContent = "RPC offline";
            return;
          }
          monetary.textContent = result.raw || "";
        });
      }
    }());
  </script>
"""


def write_whitepaper() -> None:
    """Render live-only whitepaper with getinfo/monetary fail-closed panels."""
    rel_md = "docs/whitepaper.md"
    dest = OUT / "whitepaper" / "index.html"
    md = (ROOT / rel_md).read_text(encoding="utf-8")
    dest.parent.mkdir(parents=True, exist_ok=True)
    body = md_to_html(md, with_anchors=True, skip_md_toc=True)
    body = build_toc(md) + "\n" + body
    body = body.replace(
        "<p>@@LIVE_MONETARY_SNAPSHOT@@</p>",
        '<pre id="live-monetary-snapshot" class="raw">Loading live monetary_info…</pre>',
    )
    body = body.replace(
        "<p>@@LIVE_GETINFO_SNAPSHOT@@</p>",
        '<pre id="live-getinfo-snapshot" class="raw">Loading live getinfo…</pre>\n'
        '<p id="live-getinfo-note" class="note"></p>',
    )
    html_out = SHELL.format(
        title=html.escape("ADDITION technical whitepaper"),
        heading=html.escape("White paper"),
        sub="ADDITION_MAINNET_V1",
        banner=(
            "Complete technical whitepaper from <code>docs/whitepaper.md</code>. "
            "Live <code>getinfo</code>, code, and docs. Brand: <strong>ADDITION</strong>."
        ),
        body=body,
        github="https://github.com/ADDAddition/ADDITION/blob/main/" + rel_md,
        source=rel_md,
        extra_head=TOC_STYLE,
    )
    html_out = html_out.replace(
        '<script src="/chrome.js"></script>\n</body>',
        WHITEPAPER_LIVE_SCRIPT.strip() + "\n</body>",
    )
    dest.write_text(html_out, encoding="utf-8")


def main() -> None:
    write_doc(
        "docs/ARCHITECTURE.md",
        OUT / "docs" / "architecture" / "index.html",
        "ADDITION architecture",
        "Architecture",
        "From <code>docs/ARCHITECTURE.md</code>. Signatures are ML-DSA-87.",
    )
    write_doc(
        "docs/FINAL_COMMANDS.md",
        OUT / "docs" / "commands" / "index.html",
        "ADDITION command reference",
        "Commands",
        "From <code>docs/FINAL_COMMANDS.md</code>. Public read :38545 is the allowlisted subset.",
    )
    write_doc(
        "docs/POUW_PHASE1_SPEC.md",
        OUT / "docs" / "pouw" / "index.html",
        "ADDITION PoUW phase 1 spec",
        "PoUW spec",
        "From <code>docs/POUW_PHASE1_SPEC.md</code>. Spec text; RPC names are a design target.",
    )
    write_doc(
        "docs/MAINNET_RUNBOOK.md",
        OUT / "docs" / "runbook" / "index.html",
        "ADDITION mainnet node runbook",
        "Mainnet node runbook",
        "From <code>docs/MAINNET_RUNBOOK.md</code>. Public product <code>ADDITION_MAINNET_V1</code> — P2P <code>34.27.30.115:28546</code>, RPC <code>34.27.30.115:38546</code> (write allowlist open per CoS). Height from live getinfo may be 0.",
    )
    write_doc(
        "docs/TWO_NODE_TESTNET.md",
        OUT / "docs" / "two-node" / "index.html",
        "ADDITION two-node local testnet",
        "Two-node testnet",
        "From <code>docs/TWO_NODE_TESTNET.md</code>. Two local <code>additiond</code> processes. Live join: <a href=\"/join/\">/join/</a> — bootstrap <code>34.27.30.115:28545</code>, then <code>sync</code>.",
    )
    write_doc(
        "README.md",
        OUT / "docs" / "getting-started" / "index.html",
        "Build and run ADDITION",
        "Build and run",
        "Build <code>additiond</code> (liboqs + OpenSSL). From the repository README.",
    )
    write_doc(
        "tools/zk_backend_contract.md",
        OUT / "docs" / "zk" / "index.html",
        "ADDITION SHA3 opening notes",
        "SHA3 opening notes",
        "From <code>tools/zk_backend_contract.md</code>. Privacy is SHA3-512 opening, not a ZK circuit.",
    )
    write_doc(
        "docs/PRIVACY_REAL_V1.md",
        OUT / "docs" / "privacy-real" / "index.html",
        "ADDITION privacy real v1 (scaffold)",
        "Privacy real v1",
        "From <code>docs/PRIVACY_REAL_V1.md</code>. Live claim stays <code>opening_not_zk</code>; ZK path fail-closed / <code>zk_pending</code>.",
    )
    write_doc(
        "docs/ZK_CIRCUIT_V1.md",
        OUT / "docs" / "zk-circuit" / "index.html",
        "ADDITION ZK circuit v1 (not live)",
        "ZK circuit v1",
        "From <code>docs/ZK_CIRCUIT_V1.md</code>. Real R1CS eval + toy Schnorr prove/verify in tests; <code>zk_circuit_status=not_proven</code>; live privacy remains <code>opening_not_zk</code>. Lab Groth16 Poseidon path: <code>docs/ZK_SNARK_V1.md</code>.",
    )
    write_doc(
        "docs/ZK_SNARK_V1.md",
        OUT / "docs" / "zk-snark" / "index.html",
        "ADDITION ZK SNARK v1 (lab Groth16)",
        "ZK SNARK v1",
        "From <code>docs/ZK_SNARK_V1.md</code>. Real Groth16 prove+verify (Poseidon lab hash) behind <code>ADDITION_ZK_SNARK_V1</code>; live <code>privacy_claim</code> stays <code>opening_not_zk</code>.",
    )
    write_doc(
        "docs/FAST_PATH_V1.md",
        OUT / "docs" / "fast-path" / "index.html",
        "ADDITION fast path v1 (typed stages)",
        "Fast path v1",
        "From <code>docs/FAST_PATH_V1.md</code>. Separate <code>ADDITION_FAST_V1</code> profile — typed stages REAL; leader/execution still scaffold; <code>--fast</code> fail-closed. Live product remains memory_hard mainnet.",
    )
    write_whitepaper()
    publish_raw("docs/TESTNET_PUBLIC_RPC_RUNBOOK.md", OUT / "docs" / "testnet-rpc-runbook.md")
    publish_raw("docs/WALLET.md", OUT / "docs" / "wallet.md")
    publish_raw("web/public/join.md", OUT / "docs" / "join.md")
    print("rendered docs into", OUT / "docs")
    print("rendered whitepaper into", OUT / "whitepaper")


if __name__ == "__main__":
    main()
