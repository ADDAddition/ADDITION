#!/usr/bin/env python3
"""ADDITION testnet site + RPC HTTP proxy (stdlib only)."""

from __future__ import annotations

import json
import os
import socket
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent / "public"
PUBLIC_ALLOWLIST = {
    "getinfo",
    "monetary_info",
    "crypto_selftest",
    "tx_status",
    "peers",
    "getblock",
    "getblockhash",
    "getblockraw",
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


def resolve_static(path: str) -> Path | None:
    if path in {"", "/"}:
        candidate = ROOT / "index.html"
        return candidate if candidate.is_file() else None
    rel = path.lstrip("/")
    if ".." in rel:
        return None
    for candidate in (ROOT / rel / "index.html", ROOT / rel, ROOT / (rel + ".html")):
        resolved = candidate.resolve()
        if str(resolved).startswith(str(ROOT.resolve())) and resolved.is_file():
            return resolved
    return None


def content_type(path: Path) -> str:
    if path.suffix == ".css":
        return "text/css; charset=utf-8"
    if path.suffix == ".js":
        return "application/javascript; charset=utf-8"
    if path.suffix == ".html":
        return "text/html; charset=utf-8"
    if path.suffix == ".md":
        return "text/markdown; charset=utf-8"
    if path.suffix == ".svg":
        return "image/svg+xml"
    if path.suffix == ".png":
        return "image/png"
    return "text/plain; charset=utf-8"


class Handler(BaseHTTPRequestHandler):
    server_version = "addition-site/0.1"

    def log_message(self, fmt: str, *args) -> None:
        print("%s - %s" % (self.address_string(), fmt % args))

    def _send(self, status: int, body: str, content_type_value: str = "text/plain; charset=utf-8") -> None:
        data = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type_value)
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
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)
        if path in {"/jsonrpc", "/jsonrpc/"}:
            method = (query.get("method") or [""])[0]
            raw_params = (query.get("params") or [""])[0]
            params = [p for p in raw_params.split(",") if p] if raw_params else []
            req_id_raw = (query.get("id") or ["1"])[0]
            try:
                req_id: Any = int(req_id_raw)
            except ValueError:
                req_id = req_id_raw
            self._jsonrpc({"jsonrpc": "2.0", "id": req_id, "method": method, "params": params})
            return
        is_api = path in {"/api/rpc", "/local-rpc"} or (path == "/rpc" and "cmd" in query)
        if is_api:
            cmd = (query.get("cmd") or [""])[0]
            self._rpc(path, cmd)
            return
        target = resolve_static(path)
        if target is None:
            not_found = ROOT / "404.html"
            if not_found.is_file():
                self._file(not_found, "text/html; charset=utf-8", 404)
                return
            self._send(404, "error: not found")
            return
        self._file(target, content_type(target), 200)

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path in {"/jsonrpc", "/jsonrpc/"}:
            length = int(self.headers.get("Content-Length") or "0")
            raw = self.rfile.read(max(0, length)).decode("utf-8", errors="replace")
            try:
                payload = json.loads(raw)
            except json.JSONDecodeError:
                self._send(
                    200,
                    json.dumps({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}}),
                    "application/json",
                )
                return
            self._jsonrpc(payload)
            return
        if parsed.path not in {"/rpc", "/api/rpc", "/local-rpc"}:
            self._send(404, "error: not found")
            return
        length = int(self.headers.get("Content-Length") or "0")
        body = self.rfile.read(max(0, length)).decode("utf-8", errors="replace")
        self._rpc(parsed.path, body)

    def _file(self, path: Path, type_value: str, status: int = 200) -> None:
        data = path.read_bytes()
        self.send_response(status)
        self.send_header("Content-Type", type_value)
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
        if path == "/local-rpc":
            if not client_is_loopback(self):
                self._send(403, "error: local RPC proxy is loopback-only")
                return
            host = os.environ.get("ADDITION_LOCAL_RPC_HOST", "127.0.0.1")
            port = env_int("ADDITION_LOCAL_RPC_PORT", 8545)
            timeout = 300.0 if token == "mine" else 30.0
        else:
            if token not in PUBLIC_ALLOWLIST:
                self._send(403, "error: command disabled on public RPC")
                return
            host = os.environ.get("ADDITION_PUBLIC_RPC_HOST", "127.0.0.1")
            port = env_int("ADDITION_PUBLIC_RPC_PORT", 38545)
        try:
            reply = tcp_rpc(host, port, command, timeout if path == "/local-rpc" else 4.0)
        except OSError:
            self._send(503, "RPC offline")
            return
        self._send(200, reply)

    def _jsonrpc(self, payload: Any) -> None:
        if not isinstance(payload, dict):
            self._send(
                200,
                json.dumps({"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "invalid request"}}),
                "application/json",
            )
            return
        req_id = payload.get("id")
        method = payload.get("method")
        params = payload.get("params") or []
        if not isinstance(method, str) or not method:
            self._send(
                200,
                json.dumps({"jsonrpc": "2.0", "id": req_id, "error": {"code": -32600, "message": "method must be a string"}}),
                "application/json",
            )
            return
        if method not in PUBLIC_ALLOWLIST:
            self._send(
                200,
                json.dumps(
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "error": {"code": -32601, "message": "error: command disabled on public RPC"},
                    }
                ),
                "application/json",
            )
            return
        if not isinstance(params, list):
            self._send(
                200,
                json.dumps({"jsonrpc": "2.0", "id": req_id, "error": {"code": -32602, "message": "params must be a positional array"}}),
                "application/json",
            )
            return
        parts = [method]
        for item in params:
            if item is None or isinstance(item, bool):
                self._send(
                    200,
                    json.dumps({"jsonrpc": "2.0", "id": req_id, "error": {"code": -32602, "message": "invalid params"}}),
                    "application/json",
                )
                return
            if isinstance(item, (int, float)):
                parts.append(str(int(item)))
            else:
                parts.append(str(item))
        command = " ".join(parts)
        host = os.environ.get("ADDITION_PUBLIC_RPC_HOST", "127.0.0.1")
        port = env_int("ADDITION_PUBLIC_RPC_PORT", 38545)
        try:
            reply = tcp_rpc(host, port, command, 4.0)
        except OSError:
            self._send(503, "RPC offline")
            return
        if reply.startswith("error:"):
            self._send(
                200,
                json.dumps({"jsonrpc": "2.0", "id": req_id, "error": {"code": -32000, "message": reply}}),
                "application/json",
            )
            return
        self._send(200, json.dumps({"jsonrpc": "2.0", "id": req_id, "result": reply}), "application/json")


def main() -> None:
    bind = os.environ.get("ADDITION_SITE_BIND", "127.0.0.1")
    port = env_int("ADDITION_SITE_PORT", 8080)
    httpd = ThreadingHTTPServer((bind, port), Handler)
    print(
        "ADDITION testnet site on http://%s:%s (/api/rpc allowlist, /jsonrpc public-read, /local-rpc loopback-only)"
        % (bind, port)
    )
    httpd.serve_forever()


if __name__ == "__main__":
    main()
