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
  <link rel="stylesheet" href="/common.css">
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


def inline(text: str) -> str:
    text = html.escape(text)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', text)
    return text


def md_to_html(md: str) -> str:
    lines = md.replace("\r\n", "\n").split("\n")
    out: list[str] = []
    i = 0
    in_code = False
    code: list[str] = []
    list_items: list[str] = []
    table_rows: list[str] = []

    def flush_list() -> None:
        if list_items:
            out.append("<ul>" + "".join("<li>" + inline(x) + "</li>" for x in list_items) + "</ul>")
            list_items.clear()

    def flush_table() -> None:
        if not table_rows:
            return
        rows = []
        for idx, row in enumerate(table_rows):
            cells = [c.strip() for c in row.strip("|").split("|")]
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
        if line.startswith("|"):
            flush_list()
            table_rows.append(line)
            i += 1
            continue
        flush_table()
        if re.match(r"^\s*[-*]\s+", line):
            list_items.append(re.sub(r"^\s*[-*]\s+", "", line))
            i += 1
            continue
        flush_list()
        if line.startswith("# "):
            # page already has an H1 from chrome
            out.append("<h2>" + inline(line[2:]) + "</h2>")
        elif line.startswith("## "):
            out.append("<h2>" + inline(line[3:]) + "</h2>")
        elif line.startswith("### "):
            out.append("<h3>" + inline(line[4:]) + "</h3>")
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


def publish_raw(rel_md: str, dest: Path) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text((ROOT / rel_md).read_text(encoding="utf-8"), encoding="utf-8")


def write_doc(rel_md: str, dest: Path, title: str, heading: str, banner: str) -> None:
    md = (ROOT / rel_md).read_text(encoding="utf-8")
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(
        SHELL.format(
            title=html.escape(title),
            heading=html.escape(heading),
            sub="from the repo",
            banner=banner,
            body=md_to_html(md),
            github="https://github.com/ADDAddition/ADDITION/blob/main/" + rel_md,
            source=rel_md,
        ),
        encoding="utf-8",
    )


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
        "ADDITION mainnet node runbook (not live)",
        "Mainnet node runbook",
        "From <code>docs/MAINNET_RUNBOOK.md</code>. Separate chain. Default remains <code>additiond --network testnet</code>. Not a live public network.",
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
        "Build and run ADDITION testnet",
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
    publish_raw("docs/TESTNET_PUBLIC_RPC_RUNBOOK.md", OUT / "docs" / "testnet-rpc-runbook.md")
    publish_raw("docs/WALLET.md", OUT / "docs" / "wallet.md")
    print("rendered docs into", OUT / "docs")


if __name__ == "__main__":
    main()
