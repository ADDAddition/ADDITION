#!/usr/bin/env python3
"""ADDITION iOS wallet client rules (Python oracle for Linux tests).

Swift in ios/AdditionWallet/AdditionWallet/Client/ implements the same
commands, amounts, addresses, and write-RPC policy. Keep them aligned.

Research testnet / local node only. Keys stay on the user-controlled
additiond via createwallet / wallet_send. This module does not invent
balances, heights, or a public write API.
"""

from __future__ import annotations

import hashlib
import ipaddress
import socket
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional
from urllib.parse import urlparse

ML_DSA_87 = "ml-dsa-87"
HASH_COMMITTED_ADDRESS_HEX_LEN = 128
DEFAULT_WRITE_HOST = "127.0.0.1"
DEFAULT_WRITE_PORT = 8545
DEFAULT_WRITE_SCHEME = "text"
MAX_RPC_LINE = 32768
CONTACT = "contact@additionblockchain.com"
PUBLIC_READ_COMMANDS = frozenset(
    {"getinfo", "getblock", "getblockraw"}
)
WRITE_COMMANDS = frozenset(
    {
        "createwallet",
        "wallet_list",
        "wallet_info",
        "wallet_balance",
        "wallet_send",
        "getbalance",
        "fee_info",
        "getinfo",
    }
)
INSECURE_COMMANDS = frozenset({"sendtx", "sendtx_hash", "sign_message"})
FOREIGN_CHAIN_TOKENS = frozenset(
    {
        "bitcoin",
        "btc",
        "ethereum",
        "eth",
        "solana",
        "sol",
        "metamask",
        "walletconnect",
    }
)
LOOPBACK_HOSTS = frozenset({"127.0.0.1", "::1", "localhost", "localhost.localdomain"})
REFUSED_PUBLIC_WRITE_HOSTS = frozenset(
    {
        "rpc.additionblockchain.com",
        "additionblockchain.com",
        "www.additionblockchain.com",
        "34.27.30.115",
    }
)
KNOWN_PUBLIC_READ_URLS = (
    "https://rpc.additionblockchain.com/rpc",
    "http://34.27.30.115/rpc",
    "http://34.27.30.115:38545/rpc",
)


class AdditionClientError(RuntimeError):
    pass


class RPCOfflineError(AdditionClientError):
    def __init__(self, detail: str = "RPC offline") -> None:
        super().__init__(detail)


def parse_kv(line: str) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for token in line.split():
        if "=" in token:
            key, value = token.split("=", 1)
            out[key] = value
    return out


def _looks_like_hex(value: str) -> bool:
    if not value or len(value) % 2 != 0:
        return False
    try:
        int(value, 16)
    except ValueError:
        return False
    return True


def derive_address(pubkey_hex: str, scheme_id: str = ML_DSA_87) -> str:
    """SHA3-512(scheme_id || 0x00 || pubkey_bytes), 128 hex."""
    if not scheme_id:
        raise AdditionClientError("missing scheme_id")
    if not _looks_like_hex(pubkey_hex):
        raise AdditionClientError("invalid pubkey hex")
    preimage = scheme_id.encode("ascii") + b"\x00" + bytes.fromhex(pubkey_hex)
    return hashlib.sha3_512(preimage).hexdigest()


def validate_wallet_name(name: str) -> str:
    if not name or len(name) > 64:
        raise AdditionClientError("invalid wallet name (use 1-64 letters, digits, _ or -)")
    if not name[0].isalnum():
        raise AdditionClientError("invalid wallet name (use 1-64 letters, digits, _ or -)")
    if any(not (ch.isalnum() or ch in {"_", "-"}) for ch in name):
        raise AdditionClientError("invalid wallet name (use 1-64 letters, digits, _ or -)")
    return name


def validate_address(address: str) -> str:
    raw = address.strip()
    if raw.lower().startswith("0x"):
        raise AdditionClientError("not an ADDITION address")
    if len(raw) != HASH_COMMITTED_ADDRESS_HEX_LEN:
        raise AdditionClientError("ADDITION address must be 128 hex characters")
    if not _looks_like_hex(raw):
        raise AdditionClientError("ADDITION address must be 128 hex characters")
    return raw.lower()


def parse_whole_amount(raw: str) -> int:
    text = raw.strip()
    if not text:
        raise AdditionClientError("amount must be a whole ADD unit")
    if any(ch in text for ch in ".,eE/"):
        raise AdditionClientError("whole-unit amounts only; no decimal subunit")
    if text[0] == "-":
        raise AdditionClientError("amount must be > 0")
    if not text.isdigit():
        raise AdditionClientError("amount must be a whole ADD unit")
    value = int(text)
    if value <= 0:
        raise AdditionClientError("amount must be > 0")
    return value


def parse_optional_fee(raw: Optional[str]) -> Optional[int]:
    if raw is None:
        return None
    text = raw.strip()
    if not text:
        return None
    fee = parse_whole_amount(text)
    if fee < 1:
        raise AdditionClientError("fee must be >= 1")
    return fee


def first_command_token(command: str) -> str:
    parts = command.strip().split()
    return parts[0] if parts else ""


def assert_command_allowed(command: str, *, write: bool) -> str:
    if any(ch in command for ch in "\r\n"):
        raise AdditionClientError("RPC command must be a single line")
    token = first_command_token(command)
    if not token:
        raise AdditionClientError("empty RPC command")
    lowered = token.lower()
    if lowered in INSECURE_COMMANDS:
        raise AdditionClientError(f"refusing insecure RPC command: {token}")
    if lowered in FOREIGN_CHAIN_TOKENS or any(
        part in FOREIGN_CHAIN_TOKENS for part in lowered.split("_")
    ) or any(token in lowered for token in FOREIGN_CHAIN_TOKENS):
        raise AdditionClientError("ADDITION RPC only")
    if write:
        if lowered not in WRITE_COMMANDS:
            raise AdditionClientError(f"command not allowed on write RPC: {token}")
    else:
        if lowered not in PUBLIC_READ_COMMANDS:
            raise AdditionClientError(f"command disabled on public RPC: {token}")
    if len(command) > MAX_RPC_LINE:
        raise AdditionClientError("RPC command exceeds 32768-byte TEXT RPC limit")
    return token


def _host_from_endpoint(endpoint: str) -> str:
    text = endpoint.strip()
    if not text:
        raise AdditionClientError("missing RPC endpoint")
    if "://" not in text:
        if text.count(":") == 1 and not text.startswith("["):
            return text.split(":", 1)[0].strip().lower()
        if text.startswith("["):
            end = text.find("]")
            if end > 1:
                return text[1:end].lower()
        return text.lower()
    parsed = urlparse(text)
    host = (parsed.hostname or "").lower()
    if not host:
        raise AdditionClientError("missing RPC host")
    return host


def _is_loopback_host(host: str) -> bool:
    if host in LOOPBACK_HOSTS:
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


def _is_lan_host(host: str) -> bool:
    if host.endswith(".local"):
        return True
    try:
        ip = ipaddress.ip_address(host)
    except ValueError:
        return False
    return bool(ip.is_private or ip.is_link_local)


def _is_refused_public_write_host(host: str) -> bool:
    if host in REFUSED_PUBLIC_WRITE_HOSTS:
        return True
    if host.endswith(".additionblockchain.com"):
        return True
    return False


def classify_write_host(host: str) -> str:
    normalized = host.strip().lower()
    if not normalized:
        raise AdditionClientError("missing RPC host")
    if _is_refused_public_write_host(normalized):
        return "refused_public"
    if _is_loopback_host(normalized):
        return "loopback"
    if _is_lan_host(normalized):
        return "lan"
    return "refused_public"


def assert_write_endpoint(endpoint: str) -> str:
    host = _host_from_endpoint(endpoint)
    kind = classify_write_host(host)
    if kind == "refused_public":
        raise AdditionClientError(
            "write RPC refused: not a loopback or LAN node you control"
        )
    return host


def is_known_public_read_endpoint(endpoint: str) -> bool:
    text = endpoint.strip().rstrip("/")
    if text in KNOWN_PUBLIC_READ_URLS:
        return True
    host = _host_from_endpoint(endpoint)
    return host in {
        "rpc.additionblockchain.com",
        "34.27.30.115",
    }


def parse_confirmed_balance(line: str) -> int:
    text = line.strip()
    if not text:
        raise RPCOfflineError("RPC offline")
    if text.startswith("error:"):
        raise AdditionClientError(text)
    if text == "RPC offline":
        raise RPCOfflineError("RPC offline")
    values = parse_kv(text)
    if "confirmed" in values:
        raw = values["confirmed"]
        if not raw.isdigit():
            raise AdditionClientError("RPC error: confirmed balance is not a whole unit")
        return int(raw)
    if text.isdigit():
        return int(text)
    raise AdditionClientError("RPC error: balance reply is not a whole-unit amount")


def parse_getinfo(line: str) -> Dict[str, str]:
    text = line.strip()
    if not text:
        raise RPCOfflineError("RPC offline")
    if text.startswith("error:"):
        raise AdditionClientError(text)
    if text == "RPC offline":
        raise RPCOfflineError("RPC offline")
    values = parse_kv(text)
    if "network" not in values:
        raise AdditionClientError("RPC error: getinfo missing network")
    if "height" in values:
        if not values["height"].isdigit():
            raise AdditionClientError("RPC error: getinfo height is not an integer")
    return values


def build_createwallet(name: str = "default") -> str:
    return f"createwallet {validate_wallet_name(name)}"


def build_wallet_info(name: str) -> str:
    return f"wallet_info {validate_wallet_name(name)}"


def build_wallet_list() -> str:
    return "wallet_list"


def build_wallet_balance(name: str) -> str:
    return f"wallet_balance {validate_wallet_name(name)}"


def build_getbalance(address: str) -> str:
    return f"getbalance {validate_address(address)}"


def build_wallet_send(
    name: str, to_addr: str, amount: int, fee: Optional[int] = None
) -> str:
    validate_wallet_name(name)
    validate_address(to_addr)
    if amount <= 0:
        raise AdditionClientError("amount must be > 0")
    if fee is not None and fee < 1:
        raise AdditionClientError("fee must be >= 1")
    line = f"wallet_send {name} {to_addr} {amount}"
    if fee is not None:
        line += f" {fee}"
    return line


def build_getinfo() -> str:
    return "getinfo"


def build_fee_info() -> str:
    return "fee_info"


def activity_from_send_reply(reply: str) -> Dict[str, str]:
    if reply.startswith("error:") or reply == "RPC offline" or not reply.strip():
        raise AdditionClientError("no activity from a failed send")
    values = parse_kv(reply)
    if "ok:gossiped" not in reply and "hash=" not in reply:
        raise AdditionClientError("wallet_send did not confirm")
    amount = values.get("amount")
    dest = values.get("to", "")
    return {
        "kind": "send",
        "title": "Sent ADD",
        "detail": dest,
        "amount": f"-{amount} ADD" if amount else "",
        "hash": values.get("hash", ""),
    }


@dataclass
class WriteEndpoint:
    host: str = DEFAULT_WRITE_HOST
    port: int = DEFAULT_WRITE_PORT
    token: str = ""
    scheme: str = DEFAULT_WRITE_SCHEME

    @classmethod
    def parse(cls, raw: str, token: str = "") -> "WriteEndpoint":
        text = raw.strip()
        if not text:
            text = f"{DEFAULT_WRITE_HOST}:{DEFAULT_WRITE_PORT}"
        scheme = DEFAULT_WRITE_SCHEME
        host = DEFAULT_WRITE_HOST
        port = DEFAULT_WRITE_PORT
        if "://" in text:
            parsed = urlparse(text)
            if parsed.scheme in {"http", "https"}:
                scheme = parsed.scheme
            elif parsed.scheme == "text":
                scheme = "text"
            else:
                raise AdditionClientError("unsupported RPC URL scheme")
            host = (parsed.hostname or "").lower()
            if parsed.port is not None:
                port = parsed.port
            elif scheme in {"http", "https"}:
                port = 443 if scheme == "https" else 80
        elif text.startswith("["):
            end = text.find("]")
            if end < 2:
                raise AdditionClientError("invalid IPv6 RPC host")
            host = text[1:end].lower()
            rest = text[end + 1 :]
            if rest.startswith(":"):
                port = int(rest[1:])
        elif text.count(":") == 1:
            host_part, port_part = text.split(":", 1)
            host = host_part.lower()
            port = int(port_part)
        else:
            host = text.lower()
        if not host:
            raise AdditionClientError("missing RPC host")
        assert_write_endpoint(host)
        if port <= 0 or port > 65535:
            raise AdditionClientError("invalid RPC port")
        return cls(host=host, port=port, token=token, scheme=scheme)

    def display(self) -> str:
        if self.scheme in {"http", "https"}:
            return f"{self.scheme}://{self.host}:{self.port}"
        return f"{self.host}:{self.port}"


class TextRpcClient:
    def __init__(
        self,
        endpoint: WriteEndpoint,
        timeout: float = 8.0,
        transport: Optional[Callable[[str], str]] = None,
    ) -> None:
        assert_write_endpoint(endpoint.host)
        self.endpoint = endpoint
        self.timeout = timeout
        self.transport = transport
        self.sent: List[str] = []

    def call(self, command: str, *, write: bool = True) -> str:
        assert_command_allowed(command, write=write)
        wire = f"{self.endpoint.token} {command}".strip() if self.endpoint.token else command
        if len(wire) > MAX_RPC_LINE:
            raise AdditionClientError("RPC command exceeds 32768-byte TEXT RPC limit")
        self.sent.append(wire)
        if self.transport is not None:
            try:
                reply = self.transport(wire)
            except OSError as exc:
                raise RPCOfflineError("RPC offline") from exc
        else:
            reply = self._send_socket(wire)
        if reply is None:
            raise RPCOfflineError("RPC offline")
        text = reply.strip()
        if not text:
            raise RPCOfflineError("RPC offline")
        if text == "RPC offline":
            raise RPCOfflineError("RPC offline")
        return text

    def _send_socket(self, wire: str) -> str:
        try:
            with socket.create_connection(
                (self.endpoint.host, self.endpoint.port), timeout=self.timeout
            ) as sock:
                sock.settimeout(self.timeout)
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
            raise RPCOfflineError("RPC offline") from exc
        return raw.splitlines()[0] if raw else ""


def public_read_http(command: str, url: str, timeout: float = 8.0) -> str:
    assert_command_allowed(command, write=False)
    if not is_known_public_read_endpoint(url):
        raise AdditionClientError("unknown public read endpoint")
    target = url.rstrip("/") + "?cmd=" + urllib.parse.quote(command, safe="")
    try:
        with urllib.request.urlopen(target, timeout=timeout) as resp:
            body = resp.read().decode("utf-8", errors="replace").strip()
    except (OSError, urllib.error.URLError) as exc:
        raise RPCOfflineError("RPC offline") from exc
    if not body:
        raise RPCOfflineError("RPC offline")
    if body == "RPC offline":
        raise RPCOfflineError("RPC offline")
    return body


class WalletClient:
    def __init__(self, rpc: TextRpcClient) -> None:
        self.rpc = rpc

    def createwallet(self, name: str = "default") -> Dict[str, str]:
        line = self.rpc.call(build_createwallet(name))
        if line.startswith("error:"):
            raise AdditionClientError(line)
        values = parse_kv(line)
        if "address" not in values:
            raise AdditionClientError("RPC error: createwallet missing address")
        validate_address(values["address"])
        return values

    def wallet_info(self, name: str) -> Dict[str, str]:
        line = self.rpc.call(build_wallet_info(name))
        if line.startswith("error:"):
            raise AdditionClientError(line)
        values = parse_kv(line)
        if "address" not in values:
            raise AdditionClientError("RPC error: wallet_info missing address")
        validate_address(values["address"])
        return values

    def wallet_list(self) -> str:
        line = self.rpc.call(build_wallet_list())
        if line.startswith("error:"):
            raise AdditionClientError(line)
        return line

    def balance(self, name: str) -> int:
        line = self.rpc.call(build_wallet_balance(name))
        return parse_confirmed_balance(line)

    def getbalance(self, address: str) -> int:
        line = self.rpc.call(build_getbalance(address))
        return parse_confirmed_balance(line)

    def send(self, name: str, to_addr: str, amount: int, fee: Optional[int] = None) -> str:
        info = self.wallet_info(name)
        if to_addr == info.get("address"):
            raise AdditionClientError("refusing to send to the same address")
        line = self.rpc.call(build_wallet_send(name, to_addr, amount, fee))
        if line.startswith("error:"):
            raise AdditionClientError(line)
        if "ok:gossiped" not in line and "hash=" not in line:
            raise AdditionClientError("RPC error: wallet_send did not confirm")
        return line

    def getinfo(self) -> Dict[str, str]:
        return parse_getinfo(self.rpc.call(build_getinfo()))

    def fee_info(self) -> Dict[str, str]:
        line = self.rpc.call(build_fee_info())
        if line.startswith("error:"):
            raise AdditionClientError(line)
        return parse_kv(line)
