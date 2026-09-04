#!/usr/bin/env python3
"""ADDITION site + RPC HTTP proxy (stdlib only)."""

from __future__ import annotations

import json
import os
import socket
import urllib.error
import urllib.parse
import urllib.request
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
    # Seed CoS open write on 38546 (create/mine/send/sign/tx_build).
    "createwallet",
    "wallet_list",
    "wallet_info",
    "wallet_balance",
    "wallet_send",
    "wallet_sign",
    "mine",
    "tx_build",
    "sendtx_signed",
    "sendtx_signed_hash",
    "sign_message",
    "verify_message",
    "getbalance",
    "getbalance_instant",
}


def env_int(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    return int(raw)


def network_label() -> str:
    return os.environ.get("ADDITION_NETWORK", "").strip().lower()


def default_local_rpc_port() -> int:
    if network_label() in {"mainnet", "main"}:
        return 8546
    return 8545


def local_rpc_timeout(command_token: str) -> float:
    if command_token != "mine":
        return 30.0
    if network_label() in {"mainnet", "main"}:
        return 3600.0
    return 300.0


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


def public_http_rpc(host: str, port: int, command: str, timeout: float = 4.0) -> str:
    """Proxy public-read allowlist over HTTP /rpc?cmd= (matches Cloudflare Worker upstream)."""
    base = os.environ.get("ADDITION_PUBLIC_RPC_HTTP", "").strip().rstrip("/")
    encoded = urllib.parse.quote(command.strip(), safe="")
    if base:
        url = base + "?cmd=" + encoded
    else:
        url = "http://%s:%s/rpc?cmd=%s" % (host, port, encoded)
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.read().decode("utf-8", errors="replace").strip()
    except urllib.error.HTTPError as exc:
        # Public nodes return 403 + "error: command disabled…" for non-allowlisted cmds.
        # Keep that body so callers can classify disabled vs offline vs unknown.
        body = exc.read().decode("utf-8", errors="replace").strip()
        if body:
            return body
        raise


def public_rpc(host: str, port: int, command: str, timeout: float = 4.0) -> str:
    # Prefer HTTP (seed 38546 / Worker). Fall back to TEXT TCP for local --public-rpc.
    try:
        return public_http_rpc(host, port, command, timeout)
    except (OSError, urllib.error.URLError, urllib.error.HTTPError, TimeoutError, ValueError):
        return tcp_rpc(host, port, command, timeout)


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
    if path.suffix == ".png":
        return "image/png"
    if path.suffix == ".ico":
        return "image/x-icon"
    if path.suffix == ".svg":
        return "image/svg+xml"
    if path.suffix == ".exe" or path.name.startswith("addition-wallet"):
        return "application/octet-stream"
    return "text/plain; charset=utf-8"


def parse_kv_fields(line: str) -> dict[str, str]:
    fields: dict[str, str] = {}
    text = (line or "").strip()
    if not text or "=" not in text:
        return fields
    for part in text.split():
        eq = part.find("=")
        if eq <= 0:
            continue
        key = part[:eq]
        value = part[eq + 1 :]
        if key:
            fields[key] = value
    return fields


def parse_height_value(fields: dict[str, str]) -> int | None:
    raw = fields.get("height")
    if raw is None:
        return None
    try:
        n = int(raw)
    except ValueError:
        return None
    if n < 0:
        return None
    return n


LAUNCH_PROBE_COMMANDS = (
    ("create_token", "create_token"),
    ("token_create", "token_create"),
    ("token_mint", "token_mint"),
    ("swap_pool_create", "swap_pool_create"),
    ("create_pool", "create_pool"),
    ("presale", "presale"),
    ("airdrop", "airdrop"),
    ("farm", "farm"),
)


def classify_probe(raw: str, offline: bool) -> tuple[bool, str]:
    if offline:
        return False, "offline"
    text = (raw or "").strip()
    if "command disabled on public RPC" in text:
        return False, "disabled_on_public_rpc"
    if "unknown command" in text:
        return False, "unknown_command"
    if text.startswith("error: usage"):
        return True, "usage"
    if text.startswith("error:"):
        return True, "error_response"
    if text:
        return True, "ok"
    return False, "unavailable"


def public_json_info(host: str, port: int, symbol: str | None = None) -> tuple[int, dict[str, Any]]:
    try:
        info_raw = public_rpc(host, port, "getinfo", 4.0)
    except OSError:
        return 503, {
            "ok": False,
            "offline": True,
            "brand": "ADDITION",
            "error": "RPC offline",
            "price_available": False,
            "price_usd": None,
        }
    if info_raw == "RPC offline" or info_raw.startswith("error: public read RPC"):
        return 503, {
            "ok": False,
            "offline": True,
            "brand": "ADDITION",
            "error": "RPC offline",
            "price_available": False,
            "price_usd": None,
        }
    if info_raw.startswith("error:"):
        return 502, {
            "ok": False,
            "offline": False,
            "brand": "ADDITION",
            "error": info_raw,
            "price_available": False,
            "price_usd": None,
        }
    fields = parse_kv_fields(info_raw)
    monetary_ok = False
    try:
        monetary_raw = public_rpc(host, port, "monetary_info", 4.0)
        if monetary_raw and not monetary_raw.startswith("error:"):
            fields.update(parse_kv_fields(monetary_raw))
            monetary_ok = True
    except OSError:
        monetary_ok = False
    body: dict[str, Any] = {
        "ok": True,
        "offline": False,
        "brand": "ADDITION",
        "network": fields.get("network"),
        "network_name": fields.get("network_name"),
        "network_id": fields.get("network_id"),
        "height": parse_height_value(fields),
        "peers": fields.get("peers"),
        "pq_mode": fields.get("pq_mode"),
        "pow_algorithm": fields.get("pow_algorithm"),
        "privacy_claim": fields.get("privacy_claim"),
        "max_supply": fields.get("max_supply"),
        "emitted": fields.get("emitted"),
        "remaining": fields.get("remaining"),
        "next_reward": fields.get("next_reward"),
        "next_halving_height": fields.get("next_halving_height"),
        "price_available": False,
        "price_usd": None,
        "price_note": "No market price RPC on this node",
        "source": {"getinfo": True, "monetary_info": monetary_ok},
        "raw_fields": fields,
        "token": None,
    }
    if symbol:
        try:
            token_raw = public_rpc(host, port, "token_info " + symbol, 4.0)
            offline = token_raw == "RPC offline"
            available, reason = classify_probe(token_raw, offline)
            token_fields = parse_kv_fields(token_raw) if available and not token_raw.startswith("error:") else {}
            body["token"] = {
                "symbol": symbol,
                "available": bool(available and token_fields),
                "reason": reason,
                "fields": token_fields,
                "raw": token_raw,
            }
            if not available:
                body["token"]["note"] = "token_info is not available on the public read path"
        except OSError:
            body["token"] = {"symbol": symbol, "available": False, "reason": "offline"}
    return 200, body


def public_json_capabilities(host: str, port: int) -> tuple[int, dict[str, Any]]:
    probes: dict[str, Any] = {}
    any_available = False
    for probe_id, cmd in LAUNCH_PROBE_COMMANDS:
        try:
            raw = public_rpc(host, port, cmd, 4.0)
            offline = raw == "RPC offline"
        except OSError:
            raw = "RPC offline"
            offline = True
        available, reason = classify_probe(raw, offline)
        probes[probe_id] = {
            "command": cmd,
            "available": available,
            "reason": reason,
            "raw": raw,
        }
        if available:
            any_available = True
    network_id = None
    info_offline = False
    try:
        info_raw = public_rpc(host, port, "getinfo", 4.0)
        if info_raw == "RPC offline" or info_raw.startswith("error: public read RPC"):
            info_offline = True
        elif not info_raw.startswith("error:"):
            network_id = parse_kv_fields(info_raw).get("network_id")
        else:
            info_offline = True
    except OSError:
        info_offline = True
    if info_offline:
        return 503, {
            "ok": False,
            "offline": True,
            "brand": "ADDITION",
            "network_id": None,
            "public_write": False,
            "launch_tabs_enabled": False,
            "note": "RPC offline",
            "probes": probes,
        }
    return 200, {
        "ok": True,
        "offline": False,
        "brand": "ADDITION",
        "network_id": network_id,
        "public_write": False,
        "launch_tabs_enabled": any_available,
        "note": (
            "At least one launch command answered on the public path"
            if any_available
            else "Create Token / Presale / Airdrop / Farm are not available on public mainnet RPC"
        ),
        "probes": probes,
    }


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
        if path in {"/api", "/api/", "/api/info", "/api/info/", "/api/token", "/api/token/", "/api/capabilities", "/api/capabilities/"}:
            host = os.environ.get("ADDITION_PUBLIC_RPC_HOST", "127.0.0.1")
            port = env_int("ADDITION_PUBLIC_RPC_PORT", 38546)
            if path.rstrip("/").endswith("capabilities"):
                status, body = public_json_capabilities(host, port)
            else:
                symbol = (query.get("symbol") or query.get("token") or [""])[0].strip() or None
                status, body = public_json_info(host, port, symbol)
            self._send(status, json.dumps(body), "application/json; charset=utf-8")
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
            port = env_int("ADDITION_LOCAL_RPC_PORT", default_local_rpc_port())
            timeout = local_rpc_timeout(token)
        else:
            if token not in PUBLIC_ALLOWLIST:
                self._send(403, "error: command disabled on public RPC")
                return
            host = os.environ.get("ADDITION_PUBLIC_RPC_HOST", "127.0.0.1")
            port = env_int("ADDITION_PUBLIC_RPC_PORT", 38546)
            try:
                reply = public_rpc(host, port, command, 4.0)
            except OSError:
                self._send(503, "RPC offline")
                return
            self._send(200, reply)
            return
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
        port = env_int("ADDITION_PUBLIC_RPC_PORT", 38546)
        try:
            reply = public_rpc(host, port, command, 4.0)
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
        "ADDITION site on http://%s:%s (/api/info JSON, /api/rpc allowlist, /jsonrpc public-read, /local-rpc loopback-only)"
        % (bind, port)
    )
    httpd.serve_forever()


if __name__ == "__main__":
    main()
