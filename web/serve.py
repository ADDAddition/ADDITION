#!/usr/bin/env python3
"""Honest ADDITION testnet site + RPC HTTP proxy (stdlib only)."""

from __future__ import annotations

import os
import socket
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "public"
PUBLIC_ALLOWLIST = {
    "getinfo",
    "monetary_info",
    "crypto_selftest",
    "tx_status",
    "peers",
    "getblock",
    "getblockhash",
}
PAGES = {
    "/": "index.html",
    "/explorer": "explorer.html",
    "/status": "status.html",
    "/contracts": "contracts.html",
    "/swap": "swap.html",
    "/evm": "evm.html",
}


def env_int(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    return int(raw)


def first_token(line: str) -> str:
    parts = line.strip().split()
    return parts[0] if parts else ""


def tcp_rpc(host: str, port: int, command: str, timeout: float = 4.0) -> str:
    payload = command.strip() + "\n"
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(payload.encode("utf-8"))
        chunks: list[bytes] = []
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break
    return b"".join(chunks).decode("utf-8", errors="replace").strip()


def client_is_loopback(handler: BaseHTTPRequestHandler) -> bool:
    host = handler.client_address[0]
    return host in {"127.0.0.1", "::1", "localhost"}


class Handler(BaseHTTPRequestHandler):
    server_version = "addition-site/0.1"

    def log_message(self, fmt: str, *args) -> None:
        print("%s - %s" % (self.address_string(), fmt % args))

    def _send(self, status: int, body: str, content_type: str = "text/plain; charset=utf-8") -> None:
        data = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_OPTIONS(self) -> None:
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if path in {"/rpc", "/local-rpc"}:
            cmd = (query.get("cmd") or [""])[0]
            self._rpc(path, cmd)
            return

        page = PAGES.get(path)
        if page:
            self._file(ROOT / page, "text/html; charset=utf-8")
            return

        rel = path.lstrip("/")
        if not rel or ".." in rel:
            self._send(404, "error: not found")
            return
        target = (ROOT / rel).resolve()
        if not str(target).startswith(str(ROOT.resolve())) or not target.is_file():
            self._send(404, "error: not found")
            return
        ctype = "text/plain; charset=utf-8"
        if target.suffix == ".css":
            ctype = "text/css; charset=utf-8"
        elif target.suffix == ".js":
            ctype = "application/javascript; charset=utf-8"
        elif target.suffix == ".html":
            ctype = "text/html; charset=utf-8"
        self._file(target, ctype)

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path not in {"/rpc", "/local-rpc"}:
            self._send(404, "error: not found")
            return
        length = int(self.headers.get("Content-Length") or "0")
        body = self.rfile.read(max(0, length)).decode("utf-8", errors="replace")
        self._rpc(parsed.path, body)

    def _file(self, path: Path, content_type: str) -> None:
        data = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _rpc(self, path: str, command: str) -> None:
        command = command.strip()
        if not command:
            self._send(400, "error: missing cmd")
            return
        token = first_token(command)
        if path == "/rpc":
            if token not in PUBLIC_ALLOWLIST:
                self._send(403, "error: command disabled on public RPC")
                return
            host = os.environ.get("ADDITION_PUBLIC_RPC_HOST", "127.0.0.1")
            port = env_int("ADDITION_PUBLIC_RPC_PORT", 38545)
        else:
            if not client_is_loopback(self):
                self._send(403, "error: local RPC proxy is loopback-only")
                return
            host = os.environ.get("ADDITION_LOCAL_RPC_HOST", "127.0.0.1")
            port = env_int("ADDITION_LOCAL_RPC_PORT", 8545)
        try:
            reply = tcp_rpc(host, port, command)
        except OSError:
            self._send(503, "RPC offline")
            return
        self._send(200, reply)


def main() -> None:
    bind = os.environ.get("ADDITION_SITE_BIND", "127.0.0.1")
    port = env_int("ADDITION_SITE_PORT", 8080)
    httpd = ThreadingHTTPServer((bind, port), Handler)
    print(
        "ADDITION testnet site on http://%s:%s (public /rpc allowlist, /local-rpc loopback-only)"
        % (bind, port)
    )
    httpd.serve_forever()


if __name__ == "__main__":
    main()
