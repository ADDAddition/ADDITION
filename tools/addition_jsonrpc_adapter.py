#!/usr/bin/env python3
"""Local/testnet JSON-RPC adapter over ADDITION TEXT RPC.

This is not Ethereum JSON-RPC. It only forwards commands that the running
TEXT RPC (127.0.0.1:8545) already answers. Bind is loopback-only. Spend
commands that take a private key are refused. Token writes are forwarded
because they already exist on local TEXT RPC and do not put keys on the wire.

Research testnet only. Contact: contact@additionblockchain.com
"""

from __future__ import annotations

import argparse
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from ipaddress import ip_address
from typing import Any, Dict, List, Optional, Sequence, Tuple
from urllib.parse import urlparse

from addition_text_rpc import (
    CONTACT,
    DEFAULT_RPC_HOST,
    DEFAULT_RPC_PORT,
    TextRpcClient,
    TextRpcError,
    env_rpc_token,
)

DEFAULT_ADAPTER_HOST = "127.0.0.1"
DEFAULT_ADAPTER_PORT = 8645
DEFAULT_PUBLIC_RPC_PORT = 38545

# Public-read allowlist. Same set as additiond public RPC / is_public_read_command.
PUBLIC_READ_METHODS = {
    "getinfo",
    "monetary_info",
    "crypto_selftest",
    "tx_status",
    "peers",
    "getblock",
    "getblockhash",
    "getblockraw",
}

# Exact TEXT RPC command names. No invented methods. No eth_* aliases.
READ_METHODS = {
    "getinfo",
    "getbalance",
    "getbalance_instant",
    "tx_status",
    "token_balance",
    "token_info",
    "nft_owner",
    "monetary_info",
    "fee_info",
    "protocol_status",
    "crypto_selftest",
}

# Exist on trusted local TEXT RPC. Unsigned in-process mutations. No keys.
WRITE_METHODS = {
    "token_create",
    "token_create_ex",
    "token_mint",
    "token_transfer",
    "token_burn",
    "nft_mint",
    "nft_transfer",
}

REFUSED_METHODS = {
    "sendtx",
    "sendtx_hash",
    "sendtx_signed",
    "sendtx_signed_hash",
    "sign_message",
    "tx_build",
    "createwallet",
    "identity_rotate_propose",
}


class AdapterError(RuntimeError):
    def __init__(self, message: str, code: int = -32600) -> None:
        super().__init__(message)
        self.code = code


def is_loopback_host(host: str) -> bool:
    if host in {"localhost", "127.0.0.1", "::1"}:
        return True
    try:
        return ip_address(host).is_loopback
    except ValueError:
        return False


def format_text_command(method: str, params: Sequence[Any]) -> str:
    if not method or any(ch.isspace() for ch in method) or any(ch in method for ch in "\r\n"):
        raise AdapterError("method must be a single TEXT RPC command name", -32600)
    parts: List[str] = [method]
    for index, raw in enumerate(params):
        if raw is None:
            raise AdapterError(f"params[{index}] is null", -32602)
        if isinstance(raw, bool):
            raise AdapterError(f"params[{index}] must not be a boolean", -32602)
        if isinstance(raw, (int, float)):
            if isinstance(raw, float) and not raw.is_integer():
                raise AdapterError(f"params[{index}] must be an integer", -32602)
            value = str(int(raw))
        elif isinstance(raw, str):
            value = raw
        else:
            raise AdapterError(f"params[{index}] must be a string or integer", -32602)
        if any(ch.isspace() for ch in value) or any(ch in value for ch in "\r\n"):
            raise AdapterError(
                f"params[{index}] must be a single TEXT RPC token (no spaces)",
                -32602,
            )
        parts.append(value)
    return " ".join(parts)


def classify_method(method: str, public_read: bool = False) -> str:
    if method in REFUSED_METHODS or method.startswith("eth_") or method.startswith("web3_"):
        return "refused"
    if public_read:
        if method in PUBLIC_READ_METHODS:
            return "read"
        return "refused"
    if method in READ_METHODS:
        return "read"
    if method in WRITE_METHODS:
        return "write"
    return "unknown"


def dispatch(
    rpc: TextRpcClient,
    method: str,
    params: Sequence[Any],
    allow_writes: bool,
    public_read: bool = False,
) -> str:
    kind = classify_method(method, public_read=public_read)
    if kind == "refused":
        raise AdapterError(
            "refused: this adapter does not forward spend/key "
            "commands, writes on --public-read, or Ethereum JSON-RPC methods",
            -32601,
        )
    if kind == "unknown":
        raise AdapterError(
            f"unknown TEXT RPC method {method!r}; this adapter is not Ethereum JSON-RPC",
            -32601,
        )
    if kind == "write" and (not allow_writes or public_read):
        raise AdapterError("write methods disabled (--read-only / --public-read)", -32601)
    command = format_text_command(method, params)
    reply = rpc.call(command)
    if reply.startswith("error:"):
        raise AdapterError(reply, -32000)
    return reply


def jsonrpc_response(req_id: Any, result: Any = None, error: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    body: Dict[str, Any] = {"jsonrpc": "2.0", "id": req_id}
    if error is not None:
        body["error"] = error
    else:
        body["result"] = result
    return body


def handle_payload(rpc: TextRpcClient, payload: Any, allow_writes: bool, public_read: bool = False) -> Any:
    if isinstance(payload, list):
        if not payload:
            raise AdapterError("empty batch", -32600)
        return [handle_one(rpc, item, allow_writes, public_read) for item in payload]
    if isinstance(payload, dict):
        return handle_one(rpc, payload, allow_writes, public_read)
    raise AdapterError("JSON-RPC body must be an object or array", -32700)


def handle_one(rpc: TextRpcClient, item: Any, allow_writes: bool, public_read: bool = False) -> Dict[str, Any]:
    if not isinstance(item, dict):
        return jsonrpc_response(None, error={"code": -32600, "message": "invalid request"})
    req_id = item.get("id")
    if item.get("jsonrpc") != "2.0":
        return jsonrpc_response(req_id, error={"code": -32600, "message": "jsonrpc must be 2.0"})
    method = item.get("method")
    if not isinstance(method, str):
        return jsonrpc_response(req_id, error={"code": -32600, "message": "method must be a string"})
    params = item.get("params", [])
    if params is None:
        params = []
    if not isinstance(params, list):
        return jsonrpc_response(
            req_id,
            error={"code": -32602, "message": "params must be a positional array"},
        )
    try:
        result = dispatch(rpc, method, params, allow_writes, public_read)
        return jsonrpc_response(req_id, result=result)
    except AdapterError as exc:
        return jsonrpc_response(req_id, error={"code": exc.code, "message": str(exc)})
    except TextRpcError as exc:
        return jsonrpc_response(req_id, error={"code": -32000, "message": str(exc)})


class AdapterServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(
        self,
        server_address: Tuple[str, int],
        rpc: TextRpcClient,
        allow_writes: bool,
        public_read: bool = False,
    ) -> None:
        self.rpc = rpc
        self.allow_writes = allow_writes
        self.public_read = public_read
        super().__init__(server_address, AdapterHandler)


class AdapterHandler(BaseHTTPRequestHandler):
    server: AdapterServer

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send(self, status: int, body: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802 — BaseHTTPRequestHandler API
        path = urlparse(self.path).path
        if path not in {"/", "/rpc", "/health"}:
            self._send(404, b"not found\n", "text/plain; charset=utf-8")
            return
        text = (
            "ADDITION local/testnet TEXT-RPC adapter. Not Ethereum JSON-RPC. "
            f"POST JSON-RPC 2.0 to /rpc. Contact: {CONTACT}\n"
        )
        self._send(200, text.encode("utf-8"), "text/plain; charset=utf-8")

    def do_POST(self) -> None:  # noqa: N802 — BaseHTTPRequestHandler API
        path = urlparse(self.path).path
        if path not in {"/", "/rpc"}:
            self._send(404, b"not found\n", "text/plain; charset=utf-8")
            return
        length_raw = self.headers.get("Content-Length", "")
        try:
            length = int(length_raw)
        except ValueError:
            self._send(400, b'{"error":"missing Content-Length"}\n', "application/json")
            return
        if length < 0 or length > 65536:
            self._send(413, b'{"error":"payload too large"}\n', "application/json")
            return
        raw = self.rfile.read(length)
        try:
            payload = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            body = json.dumps(
                jsonrpc_response(None, error={"code": -32700, "message": "parse error"})
            ).encode("utf-8")
            self._send(200, body, "application/json")
            return
        response = handle_payload(
            self.server.rpc,
            payload,
            self.server.allow_writes,
            self.server.public_read,
        )
        self._send(200, json.dumps(response).encode("utf-8"), "application/json")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Local/testnet JSON-RPC adapter over ADDITION TEXT RPC. "
            f"Not Ethereum JSON-RPC. Contact: {CONTACT}"
        ),
    )
    parser.add_argument("--bind", default=DEFAULT_ADAPTER_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_ADAPTER_PORT)
    parser.add_argument("--rpc-host", default=DEFAULT_RPC_HOST)
    parser.add_argument("--rpc-port", type=int, default=DEFAULT_RPC_PORT)
    parser.add_argument("--rpc-token", default=env_rpc_token())
    parser.add_argument(
        "--read-only",
        action="store_true",
        help="forward only read TEXT RPC commands",
    )
    parser.add_argument(
        "--public-read",
        action="store_true",
        help=(
            "public-read JSON API: same allowlist as public RPC "
            "(getinfo, monetary_info, crypto_selftest, tx_status, peers, "
            "getblock, getblockhash, getblockraw). No writes."
        ),
    )
    return parser


def main(argv: Optional[List[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if args.public_read:
        if args.rpc_port == DEFAULT_RPC_PORT:
            args.rpc_port = DEFAULT_PUBLIC_RPC_PORT
    if not args.public_read and (not is_loopback_host(args.bind) or not is_loopback_host(args.rpc_host)):
        print(
            "error: adapter and TEXT RPC host must be loopback "
            "(127.0.0.1 / localhost / ::1) unless --public-read.",
            file=sys.stderr,
        )
        return 2
    rpc = TextRpcClient(host=args.rpc_host, port=args.rpc_port, token=args.rpc_token)
    server = AdapterServer(
        (args.bind, args.port),
        rpc,
        allow_writes=not args.read_only and not args.public_read,
        public_read=args.public_read,
    )
    if args.public_read:
        mode = "public-read allowlist, no writes"
    else:
        mode = "read-only" if args.read_only else "read + local token writes"
    print(
        f"ADDITION local/testnet JSON-RPC adapter on http://{args.bind}:{args.port}/rpc "
        f"({mode}). Upstream TEXT RPC {args.rpc_host}:{args.rpc_port}. "
        "Not Ethereum JSON-RPC."
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping adapter")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
