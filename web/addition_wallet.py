#!/usr/bin/env python3
"""Local ADDITION testnet wallet (TEXT RPC on 127.0.0.1:8545).

Research testnet only. Keys stay in a gitignored file and are never placed
on the RPC line for spend. Signing is ML-DSA-87 via liboqs.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import hashlib
import os
import socket
import stat
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List, Optional

DEFAULT_RPC_HOST = "127.0.0.1"
DEFAULT_RPC_PORT = 8545
DEFAULT_WALLET_PATH = Path("data") / "addition.wallet"
ML_DSA_87 = "ml-dsa-87"
ML_DSA_87_PK_BYTES = 2592
ML_DSA_87_SK_BYTES = 4896
ML_DSA_87_SIG_BYTES = 4627
OQS_SUCCESS = 0
MAX_RPC_LINE = 32768
CONTACT = "contact@additionblockchain.com"

KeygenFn = Callable[[], "WalletRecord"]
SignerFn = Callable[[str, bytes], str]


class WalletError(RuntimeError):
    pass


def derive_address(pubkey_hex: str) -> str:
    return hashlib.sha3_512(("addr|" + pubkey_hex).encode("ascii")).hexdigest()[:40]


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


class LiboqsMlDsa87:
    """Minimal ctypes wrapper around liboqs ML-DSA-87 (FIPS 204)."""

    def __init__(self, library_path: Optional[str] = None) -> None:
        self._lib = self._load(library_path)
        self._lib.OQS_SIG_new.argtypes = [ctypes.c_char_p]
        self._lib.OQS_SIG_new.restype = ctypes.c_void_p
        self._lib.OQS_SIG_free.argtypes = [ctypes.c_void_p]
        self._lib.OQS_SIG_keypair.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        self._lib.OQS_SIG_keypair.restype = ctypes.c_int
        self._lib.OQS_SIG_sign.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_size_t),
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.c_void_p,
        ]
        self._lib.OQS_SIG_sign.restype = ctypes.c_int

    @staticmethod
    def _load(library_path: Optional[str]) -> ctypes.CDLL:
        candidates: List[str] = []
        if library_path:
            candidates.append(library_path)
        env = os.environ.get("ADDITION_LIBOQS", "").strip()
        if env:
            candidates.append(env)
        candidates.extend(
            [
                "/usr/local/lib/liboqs.so",
                "/usr/lib/liboqs.so",
                "liboqs.so",
                "liboqs.dylib",
                "oqs.dll",
            ]
        )
        found = ctypes.util.find_library("oqs")
        if found:
            candidates.append(found)
        last_error = "liboqs not found"
        for path in candidates:
            try:
                return ctypes.CDLL(path)
            except OSError as exc:
                last_error = str(exc)
        raise WalletError(
            "liboqs is required for local ML-DSA-87 keygen/sign. "
            f"Install liboqs and retry ({last_error})."
        )

    def generate(self) -> "WalletRecord":
        sig = self._lib.OQS_SIG_new(b"ML-DSA-87")
        if not sig:
            raise WalletError("OQS_SIG_new failed for ML-DSA-87")
        try:
            public_key = (ctypes.c_uint8 * ML_DSA_87_PK_BYTES)()
            secret_key = (ctypes.c_uint8 * ML_DSA_87_SK_BYTES)()
            if self._lib.OQS_SIG_keypair(sig, public_key, secret_key) != OQS_SUCCESS:
                raise WalletError("OQS_SIG_keypair failed")
            pub_hex = bytes(public_key).hex()
            priv_hex = bytes(secret_key).hex()
            return WalletRecord(
                network="testnet",
                algorithm=ML_DSA_87,
                address=derive_address(pub_hex),
                public_key=pub_hex,
                private_key=priv_hex,
                next_nonce=1,
            )
        finally:
            self._lib.OQS_SIG_free(sig)

    def sign(self, private_key_hex: str, message: bytes) -> str:
        if not _looks_like_hex(private_key_hex) or len(private_key_hex) != ML_DSA_87_SK_BYTES * 2:
            raise WalletError("private key hex does not match ML-DSA-87 secret size")
        sig = self._lib.OQS_SIG_new(b"ML-DSA-87")
        if not sig:
            raise WalletError("OQS_SIG_new failed for ML-DSA-87")
        try:
            secret_key = (ctypes.c_uint8 * ML_DSA_87_SK_BYTES).from_buffer_copy(
                bytes.fromhex(private_key_hex)
            )
            signature = (ctypes.c_uint8 * ML_DSA_87_SIG_BYTES)()
            sig_len = ctypes.c_size_t(ML_DSA_87_SIG_BYTES)
            rc = self._lib.OQS_SIG_sign(
                sig,
                signature,
                ctypes.byref(sig_len),
                message,
                len(message),
                secret_key,
            )
            if rc != OQS_SUCCESS or sig_len.value == 0 or sig_len.value > ML_DSA_87_SIG_BYTES:
                raise WalletError("OQS_SIG_sign failed")
            return bytes(signature[: sig_len.value]).hex()
        finally:
            self._lib.OQS_SIG_free(sig)


@dataclass
class WalletRecord:
    network: str
    algorithm: str
    address: str
    public_key: str
    private_key: str
    next_nonce: int

    def public_view(self) -> str:
        return (
            f"network={self.network} algorithm={self.algorithm} "
            f"address={self.address} next_nonce={self.next_nonce} "
            f"pub={self.public_key}"
        )


class WalletStore:
    def __init__(self, path: Path) -> None:
        self.path = path

    def exists(self) -> bool:
        return self.path.is_file()

    def load(self) -> WalletRecord:
        if not self.exists():
            raise WalletError(f"wallet file not found: {self.path}")
        values: Dict[str, str] = {}
        for raw in self.path.read_text(encoding="ascii").splitlines():
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
        required = ("network", "algorithm", "address", "public_key", "private_key", "next_nonce")
        missing = [key for key in required if not values.get(key)]
        if missing:
            raise WalletError("wallet file missing fields: " + ",".join(missing))
        if values["algorithm"] != ML_DSA_87:
            raise WalletError("unsupported wallet algorithm: " + values["algorithm"])
        if derive_address(values["public_key"]) != values["address"]:
            raise WalletError("wallet address does not match public key")
        return WalletRecord(
            network=values["network"],
            algorithm=values["algorithm"],
            address=values["address"],
            public_key=values["public_key"],
            private_key=values["private_key"],
            next_nonce=int(values["next_nonce"]),
        )

    def save(self, record: WalletRecord) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        body = (
            "# ADDITION research testnet wallet. Keep this file private.\n"
            "# Never commit it. Never paste the private key into RPC.\n"
            f"network={record.network}\n"
            f"algorithm={record.algorithm}\n"
            f"address={record.address}\n"
            f"public_key={record.public_key}\n"
            f"private_key={record.private_key}\n"
            f"next_nonce={record.next_nonce}\n"
        )
        flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC
        fd = os.open(str(self.path), flags, stat.S_IRUSR | stat.S_IWUSR)
        try:
            os.write(fd, body.encode("ascii"))
            os.fchmod(fd, stat.S_IRUSR | stat.S_IWUSR)
        finally:
            os.close(fd)


class TextRpcClient:
    """One-line TEXT RPC client. Not JSON-RPC."""

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
            raise WalletError("RPC command must be a single line")
        wire = f"{self.token} {command}".strip() if self.token else command
        if len(wire) > MAX_RPC_LINE:
            raise WalletError("RPC command exceeds 32768-byte TEXT RPC limit")
        self.sent.append(wire)
        if self.transport is not None:
            return self.transport(wire)
        return self._send_socket(wire, timeout if timeout is not None else self.timeout)

    def _send_socket(self, wire: str, timeout: float) -> str:
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
        return raw.splitlines()[0] if raw else ""


class WalletClient:
    def __init__(
        self,
        rpc: TextRpcClient,
        store: WalletStore,
        keygen: Optional[KeygenFn] = None,
        signer: Optional[SignerFn] = None,
        liboqs_path: Optional[str] = None,
    ) -> None:
        self.rpc = rpc
        self.store = store
        self._keygen = keygen
        self._signer = signer
        self._liboqs_path = liboqs_path
        self._oqs: Optional[LiboqsMlDsa87] = None

    def _oqs_backend(self) -> LiboqsMlDsa87:
        if self._oqs is None:
            self._oqs = LiboqsMlDsa87(self._liboqs_path)
        return self._oqs

    def generate_keys(self) -> WalletRecord:
        if self._keygen is not None:
            return self._keygen()
        return self._oqs_backend().generate()

    def sign_local(self, private_key_hex: str, message: bytes) -> str:
        if self._signer is not None:
            return self._signer(private_key_hex, message)
        return self._oqs_backend().sign(private_key_hex, message)

    def create(self, force: bool = False) -> WalletRecord:
        if self.store.exists() and not force:
            raise WalletError(f"wallet already exists: {self.store.path} (pass --force to overwrite)")
        record = self.generate_keys()
        if record.algorithm != ML_DSA_87:
            raise WalletError("wallet keygen must be ml-dsa-87")
        if derive_address(record.public_key) != record.address:
            raise WalletError("generated address/pubkey mismatch")
        self.store.save(record)
        return record

    def show(self) -> WalletRecord:
        return self.store.load()

    def getinfo(self) -> str:
        return self.rpc.call("getinfo")

    def balance(self, address: Optional[str] = None) -> str:
        record = self.store.load() if address is None else None
        target = address if address is not None else record.address
        return self.rpc.call(f"getbalance {target}")

    def fee_info(self) -> str:
        return self.rpc.call("fee_info")

    def recommended_fee(self) -> int:
        info = parse_kv(self.fee_info())
        raw = info.get("recommended_min_fee", "1")
        try:
            fee = int(raw)
        except ValueError as exc:
            raise WalletError("fee_info returned a non-integer recommended_min_fee") from exc
        return max(fee, 1)

    def mine(self, address: Optional[str] = None) -> str:
        record = self.store.load() if address is None else None
        target = address if address is not None else record.address
        return self.rpc.call(f"mine {target}", timeout=180.0)

    def send(self, to_addr: str, amount: int, fee: Optional[int] = None) -> str:
        if amount <= 0:
            raise WalletError("amount must be > 0")
        if not to_addr or any(ch.isspace() for ch in to_addr):
            raise WalletError("recipient address is invalid")
        record = self.store.load()
        if to_addr == record.address:
            raise WalletError("refusing to send to the same address")
        used_fee = fee if fee is not None else self.recommended_fee()
        if used_fee < 1:
            raise WalletError("fee must be >= 1")

        last_error = "send failed"
        nonce = max(record.next_nonce, 1)
        for _ in range(8):
            built = self.rpc.call(
                f"tx_build {record.address} {record.public_key} {to_addr} {amount} {used_fee} {nonce}"
            )
            if built.startswith("error:"):
                if "nonce" in built:
                    nonce += 1
                    last_error = built
                    continue
                raise WalletError(built)
            sign_hash = parse_kv(built).get("sign_hash", "")
            if not sign_hash:
                raise WalletError("tx_build did not return sign_hash: " + built)
            signature = self.sign_local(record.private_key, sign_hash.encode("utf-8"))
            if signature.startswith("pq="):
                signature = signature[3:]
            if record.private_key in signature:
                raise WalletError("refusing to continue: signature unexpectedly contains the private key")
            submitted = self.rpc.call(
                "sendtx_signed_hash "
                f"{record.address} {record.public_key} {to_addr} {amount} {used_fee} {nonce} {signature}"
            )
            self._assert_no_private_key_on_wire(record.private_key)
            if submitted.startswith("error:"):
                if "nonce" in submitted:
                    nonce += 1
                    last_error = submitted
                    continue
                raise WalletError(submitted)
            record.next_nonce = nonce + 1
            self.store.save(record)
            return submitted
        raise WalletError(last_error)

    def _assert_no_private_key_on_wire(self, private_key_hex: str) -> None:
        for command in self.rpc.sent:
            if private_key_hex and private_key_hex in command:
                raise WalletError("private key leaked onto the TEXT RPC line")
            first = command.split()[0] if command.split() else ""
            if first in {"sendtx", "sendtx_hash", "sign_message"}:
                raise WalletError(f"refusing insecure RPC command: {first}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="ADDITION local testnet wallet. Research prototype only.",
    )
    parser.add_argument(
        "--wallet",
        default=str(DEFAULT_WALLET_PATH),
        help=f"wallet file path (default: {DEFAULT_WALLET_PATH})",
    )
    parser.add_argument("--rpc-host", default=DEFAULT_RPC_HOST)
    parser.add_argument("--rpc-port", type=int, default=DEFAULT_RPC_PORT)
    parser.add_argument(
        "--rpc-token",
        default=os.environ.get("ADDITION_RPC_TOKEN", ""),
        help="optional ADDITION_RPC_TOKEN prefix",
    )
    parser.add_argument("--liboqs", default=os.environ.get("ADDITION_LIBOQS", ""))
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("createwallet", help="create a local ML-DSA-87 address")
    create.add_argument("--force", action="store_true")
    sub.add_parser("show", help="print address and public key (never the private key)")
    sub.add_parser("getinfo", help="query daemon getinfo")
    sub.add_parser("balance", help="getbalance for the local address")
    sub.add_parser("fee", help="fee_info")
    send = sub.add_parser("send", help="tx_build, local ML-DSA-87 sign, sendtx_signed_hash")
    send.add_argument("to")
    send.add_argument("amount", type=int)
    send.add_argument("--fee", type=int, default=None)
    sub.add_parser("mine", help="optional: mine one block to the local address")
    return parser


def _client_from_args(args: argparse.Namespace) -> WalletClient:
    rpc = TextRpcClient(host=args.rpc_host, port=args.rpc_port, token=args.rpc_token)
    store = WalletStore(Path(args.wallet))
    return WalletClient(rpc=rpc, store=store, liboqs_path=args.liboqs or None)


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    client = _client_from_args(args)
    try:
        if args.command == "createwallet":
            record = client.create(force=args.force)
            print(record.public_view())
            print(f"wallet_file={client.store.path}")
            print("priv_printed=0")
            return 0
        if args.command == "show":
            print(client.show().public_view())
            return 0
        if args.command == "getinfo":
            print(client.getinfo())
            return 0
        if args.command == "balance":
            print(client.balance())
            return 0
        if args.command == "fee":
            print(client.fee_info())
            return 0
        if args.command == "send":
            print(client.send(args.to, args.amount, fee=args.fee))
            return 0
        if args.command == "mine":
            print(client.mine())
            return 0
        parser.error(f"unknown command: {args.command}")
        return 2
    except (WalletError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
