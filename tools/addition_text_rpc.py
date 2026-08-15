#!/usr/bin/env python3
"""One-line TEXT RPC client for the local ADDITION testnet daemon.

This is not JSON-RPC and not Ethereum JSON-RPC. Each request is one command
line to 127.0.0.1:8545 and one response line.
"""

from __future__ import annotations

import os
import socket
from typing import Callable, List, Optional

DEFAULT_RPC_HOST = "127.0.0.1"
DEFAULT_RPC_PORT = 8545
MAX_RPC_LINE = 32768
CONTACT = "contact@additionblockchain.com"


class TextRpcError(RuntimeError):
    pass


class TextRpcClient:
    """Talk to additiond TEXT RPC. Keys must never be placed on the wire here."""

    def __init__(
        self,
        host: str = DEFAULT_RPC_HOST,
        port: int = DEFAULT_RPC_PORT,
        token: str = "",
        timeout: float = 8.0,
        transport: Optional[Callable[[str], str]] = None,
    ) -> None:
        self.host = host
        self.port = port
        self.token = token
        self.timeout = timeout
        self.transport = transport
        self.sent: List[str] = []

    def call(self, command: str, timeout: Optional[float] = None) -> str:
        if any(ch in command for ch in "\r\n"):
            raise TextRpcError("RPC command must be a single line")
        if not command.strip():
            raise TextRpcError("RPC command is empty")
        wire = f"{self.token} {command}".strip() if self.token else command
        if len(wire.encode("utf-8")) > MAX_RPC_LINE:
            raise TextRpcError("RPC command exceeds 32768-byte TEXT RPC limit")
        self.sent.append(wire)
        if self.transport is not None:
            return self.transport(wire)
        return self._send_socket(wire, timeout if timeout is not None else self.timeout)

    def _send_socket(self, wire: str, timeout: float) -> str:
        try:
            with socket.create_connection((self.host, self.port), timeout=timeout) as sock:
                sock.settimeout(timeout)
                sock.sendall((wire + "\n").encode("utf-8"))
                chunks: List[bytes] = []
                while True:
                    piece = sock.recv(MAX_RPC_LINE)
                    if not piece:
                        break
                    chunks.append(piece)
                    if b"\n" in piece:
                        break
                raw = b"".join(chunks).decode("utf-8", errors="replace")
        except OSError as exc:
            raise TextRpcError(
                f"cannot reach TEXT RPC {self.host}:{self.port} ({exc}). "
                "Start ./build/additiond --network testnet first."
            ) from exc
        return raw.splitlines()[0] if raw else ""


def env_rpc_token() -> str:
    return os.environ.get("ADDITION_RPC_TOKEN", "").strip()
