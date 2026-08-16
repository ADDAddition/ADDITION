"""Local wallet/swap panel images: in-repo logo and getinfo shot helpers."""

from __future__ import annotations

from pathlib import Path

ASSETS = Path(__file__).resolve().parent / "public" / "assets"
LOGO_SVG = ASSETS / "addition-logo.svg"
LOGO_PNG = ASSETS / "addition-logo.png"
PLACEHOLDER_SVG = ASSETS / "getinfo-placeholder.svg"
PLACEHOLDER_ALT = "PLACEHOLDER: RPC offline. No getinfo shot."
LIVE_ALT = "live getinfo from 127.0.0.1:8545"


def wrap_text(text: str, width: int = 72) -> list[str]:
    words = text.strip().split()
    lines: list[str] = []
    current = ""
    for word in words:
        next_line = f"{current} {word}".strip()
        if current and len(next_line) > width:
            lines.append(current)
            current = word
        else:
            current = next_line
    if current:
        lines.append(current)
    return lines[:14]


def _xml_escape(value: str) -> str:
    return (
        value.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def live_getinfo_svg(raw: str) -> str:
    lines = wrap_text(raw)
    height = 44 + len(lines) * 16
    width = 560
    body = "".join(
        (
            f'<text x="16" y="{40 + idx * 16}" fill="#dce3ea" '
            'font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace" '
            f'font-size="11">{_xml_escape(line)}</text>'
        )
        for idx, line in enumerate(lines)
    )
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-label="{_xml_escape(LIVE_ALT)}">'
        f"<title>{_xml_escape(LIVE_ALT)}</title>"
        f'<rect width="{width}" height="{height}" fill="#0d0f12"/>'
        f'<rect x="1" y="1" width="{width - 2}" height="{height - 2}" fill="none" stroke="#1c232c"/>'
        '<text x="16" y="20" fill="#3d9a6a" '
        'font-family="ui-monospace, SFMono-Regular, Menlo, Consolas, monospace" '
        'font-size="12" font-weight="700">getinfo 127.0.0.1:8545</text>'
        f"{body}</svg>"
    )


def getinfo_shot_svg(raw: str | None, offline: bool) -> str:
    if offline or not raw or raw == "RPC offline" or raw.startswith("error:"):
        return PLACEHOLDER_SVG.read_text(encoding="utf-8")
    return live_getinfo_svg(raw)
