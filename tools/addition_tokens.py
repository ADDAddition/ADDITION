#!/usr/bin/env python3
"""Local ADDITION testnet token CLI (TEXT RPC on 127.0.0.1:8545).

Research testnet only. `token_create` / `token_mint` / `token_transfer` are
unsigned in-process TokenEngine mutations — not a DEX. Prefer
`token_transfer_wallet` or `token_transfer_signed` when the owner is a real
hash-committed address. Private keys are never placed on the RPC line.
"""

from __future__ import annotations

import argparse
import sys
from typing import List, Optional

from addition_text_rpc import (
    CONTACT,
    DEFAULT_RPC_HOST,
    DEFAULT_RPC_PORT,
    TextRpcClient,
    TextRpcError,
    env_rpc_token,
)

# Commands verified in src/rpc_server.cpp. Swap/pool commands exist on the
# daemon as in-process AMM math; this CLI does not expose them as a DEX.
TOKEN_WRITE_COMMANDS = {
    "token_create",
    "token_create_ex",
    "token_mint",
    "token_transfer",
    "token_transfer_signed",
    "token_transfer_wallet",
    "token_burn",
    "nft_mint",
    "nft_transfer",
}
TOKEN_READ_COMMANDS = {
    "token_balance",
    "token_info",
    "token_sign_payload",
    "nft_owner",
    "nft_info",
    "getinfo",
}


class TokenCliError(RuntimeError):
    pass


def _require_token(value: str, name: str) -> str:
    if not value or any(ch.isspace() for ch in value):
        raise TokenCliError(f"{name} must be a single non-empty token (no spaces)")
    return value


def _require_u64(value: int, name: str) -> int:
    if value <= 0:
        raise TokenCliError(f"{name} must be > 0")
    if value > 2**64 - 1:
        raise TokenCliError(f"{name} exceeds uint64")
    return value


def _require_u64_or_zero(value: int, name: str) -> int:
    if value < 0:
        raise TokenCliError(f"{name} must be >= 0")
    if value > 2**64 - 1:
        raise TokenCliError(f"{name} exceeds uint64")
    return value


class TokenClient:
    def __init__(self, rpc: TextRpcClient) -> None:
        self.rpc = rpc

    def _call(self, command: str) -> str:
        first = command.split()[0] if command.split() else ""
        if first not in TOKEN_WRITE_COMMANDS and first not in TOKEN_READ_COMMANDS:
            raise TokenCliError(f"refusing unlisted command: {first}")
        reply = self.rpc.call(command)
        if reply.startswith("error:"):
            raise TokenCliError(reply)
        return reply

    def getinfo(self) -> str:
        return self._call("getinfo")

    def create(self, symbol: str, owner: str, max_supply: int, initial_mint: int) -> str:
        symbol = _require_token(symbol, "symbol")
        owner = _require_token(owner, "owner")
        max_supply = _require_u64(max_supply, "max_supply")
        initial_mint = _require_u64_or_zero(initial_mint, "initial_mint")
        if initial_mint > max_supply:
            raise TokenCliError("initial_mint exceeds max_supply")
        return self._call(f"token_create {symbol} {owner} {max_supply} {initial_mint}")

    def create_ex(
        self,
        symbol: str,
        name: str,
        owner: str,
        max_supply: int,
        initial_mint: int,
        decimals: int,
        burnable: bool,
        dev_wallet: str,
        dev_allocation: int,
    ) -> str:
        symbol = _require_token(symbol, "symbol")
        name = _require_token(name, "name")
        owner = _require_token(owner, "owner")
        max_supply = _require_u64(max_supply, "max_supply")
        initial_mint = _require_u64_or_zero(initial_mint, "initial_mint")
        if decimals < 0 or decimals > 30:
            raise TokenCliError("decimals must be in 0..30")
        dev_allocation = _require_u64_or_zero(dev_allocation, "dev_allocation")
        wallet = "-" if not dev_wallet else _require_token(dev_wallet, "dev_wallet")
        return self._call(
            f"token_create_ex {symbol} {name} {owner} {max_supply} {initial_mint} "
            f"{decimals} {1 if burnable else 0} {wallet} {dev_allocation}"
        )

    def mint(self, symbol: str, caller: str, to: str, amount: int) -> str:
        symbol = _require_token(symbol, "symbol")
        caller = _require_token(caller, "caller")
        to = _require_token(to, "to")
        amount = _require_u64(amount, "amount")
        return self._call(f"token_mint {symbol} {caller} {to} {amount}")

    def transfer(self, symbol: str, from_addr: str, to: str, amount: int) -> str:
        symbol = _require_token(symbol, "symbol")
        from_addr = _require_token(from_addr, "from")
        to = _require_token(to, "to")
        amount = _require_u64(amount, "amount")
        return self._call(f"token_transfer {symbol} {from_addr} {to} {amount}")

    def sign_payload(self, symbol: str, from_addr: str, to: str, amount: int) -> str:
        symbol = _require_token(symbol, "symbol")
        from_addr = _require_token(from_addr, "from")
        to = _require_token(to, "to")
        amount = _require_u64(amount, "amount")
        return self._call(f"token_sign_payload {symbol} {from_addr} {to} {amount}")

    def transfer_signed(
        self,
        symbol: str,
        from_addr: str,
        to: str,
        amount: int,
        pubkey: str,
        sig: str,
    ) -> str:
        symbol = _require_token(symbol, "symbol")
        from_addr = _require_token(from_addr, "from")
        to = _require_token(to, "to")
        amount = _require_u64(amount, "amount")
        pubkey = _require_token(pubkey, "pubkey")
        sig = _require_token(sig, "sig")
        return self._call(
            f"token_transfer_signed {symbol} {from_addr} {to} {amount} {pubkey} {sig}"
        )

    def transfer_wallet(self, wallet: str, symbol: str, to: str, amount: int) -> str:
        wallet = _require_token(wallet, "wallet")
        symbol = _require_token(symbol, "symbol")
        to = _require_token(to, "to")
        amount = _require_u64(amount, "amount")
        return self._call(f"token_transfer_wallet {wallet} {symbol} {to} {amount}")

    def burn(self, symbol: str, from_addr: str, amount: int) -> str:
        symbol = _require_token(symbol, "symbol")
        from_addr = _require_token(from_addr, "from")
        amount = _require_u64(amount, "amount")
        return self._call(f"token_burn {symbol} {from_addr} {amount}")

    def balance(self, symbol: str, owner: str) -> str:
        symbol = _require_token(symbol, "symbol")
        owner = _require_token(owner, "owner")
        return self._call(f"token_balance {symbol} {owner}")

    def info(self, symbol: str) -> str:
        symbol = _require_token(symbol, "symbol")
        return self._call(f"token_info {symbol}")

    def nft_mint(self, collection: str, token_id: str, owner: str, metadata: str) -> str:
        collection = _require_token(collection, "collection")
        token_id = _require_token(token_id, "token_id")
        owner = _require_token(owner, "owner")
        if any(ch in metadata for ch in "\r\n"):
            raise TokenCliError("metadata must be a single line")
        return self._call(f"nft_mint {collection} {token_id} {owner} {metadata}")

    def nft_transfer(self, collection: str, token_id: str, from_addr: str, to: str) -> str:
        collection = _require_token(collection, "collection")
        token_id = _require_token(token_id, "token_id")
        from_addr = _require_token(from_addr, "from")
        to = _require_token(to, "to")
        return self._call(f"nft_transfer {collection} {token_id} {from_addr} {to}")

    def nft_owner(self, collection: str, token_id: str) -> str:
        collection = _require_token(collection, "collection")
        token_id = _require_token(token_id, "token_id")
        return self._call(f"nft_owner {collection} {token_id}")

    def nft_info(self, collection: str, token_id: str) -> str:
        collection = _require_token(collection, "collection")
        token_id = _require_token(token_id, "token_id")
        return self._call(f"nft_info {collection} {token_id}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "ADDITION local testnet token CLI. Research prototype only. "
            f"Contact: {CONTACT}"
        ),
    )
    parser.add_argument("--rpc-host", default=DEFAULT_RPC_HOST)
    parser.add_argument("--rpc-port", type=int, default=DEFAULT_RPC_PORT)
    parser.add_argument(
        "--rpc-token",
        default=env_rpc_token(),
        help="optional ADDITION_RPC_TOKEN prefix",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("getinfo", help="query daemon getinfo")

    create = sub.add_parser("create", help="token_create <symbol> <owner> <max_supply> <initial_mint>")
    create.add_argument("symbol")
    create.add_argument("owner")
    create.add_argument("max_supply", type=int)
    create.add_argument("initial_mint", type=int)

    create_ex = sub.add_parser(
        "create-ex",
        help="token_create_ex (name has no spaces; use - for empty dev wallet)",
    )
    create_ex.add_argument("symbol")
    create_ex.add_argument("name")
    create_ex.add_argument("owner")
    create_ex.add_argument("max_supply", type=int)
    create_ex.add_argument("initial_mint", type=int)
    create_ex.add_argument("decimals", type=int)
    create_ex.add_argument("burnable", type=int, choices=(0, 1))
    create_ex.add_argument("dev_wallet")
    create_ex.add_argument("dev_allocation", type=int)

    mint = sub.add_parser("mint", help="token_mint <symbol> <caller> <to> <amount>")
    mint.add_argument("symbol")
    mint.add_argument("caller")
    mint.add_argument("to")
    mint.add_argument("amount", type=int)

    transfer = sub.add_parser("transfer", help="token_transfer <symbol> <from> <to> <amount> (unsigned research)")
    transfer.add_argument("symbol")
    transfer.add_argument("from_addr")
    transfer.add_argument("to")
    transfer.add_argument("amount", type=int)

    sign_payload = sub.add_parser(
        "sign-payload",
        help="token_sign_payload <symbol> <from> <to> <amount>",
    )
    sign_payload.add_argument("symbol")
    sign_payload.add_argument("from_addr")
    sign_payload.add_argument("to")
    sign_payload.add_argument("amount", type=int)

    transfer_signed = sub.add_parser(
        "transfer-signed",
        help="token_transfer_signed <symbol> <from> <to> <amount> <pubkey> <sig>",
    )
    transfer_signed.add_argument("symbol")
    transfer_signed.add_argument("from_addr")
    transfer_signed.add_argument("to")
    transfer_signed.add_argument("amount", type=int)
    transfer_signed.add_argument("pubkey")
    transfer_signed.add_argument("sig")

    transfer_wallet = sub.add_parser(
        "transfer-wallet",
        help="token_transfer_wallet <wallet> <symbol> <to> <amount>",
    )
    transfer_wallet.add_argument("wallet")
    transfer_wallet.add_argument("symbol")
    transfer_wallet.add_argument("to")
    transfer_wallet.add_argument("amount", type=int)

    burn = sub.add_parser("burn", help="token_burn <symbol> <from> <amount> (token must be burnable)")
    burn.add_argument("symbol")
    burn.add_argument("from_addr")
    burn.add_argument("amount", type=int)

    balance = sub.add_parser("balance", help="token_balance <symbol> <owner>")
    balance.add_argument("symbol")
    balance.add_argument("owner")

    info = sub.add_parser("info", help="token_info <symbol>")
    info.add_argument("symbol")

    nft_mint = sub.add_parser("nft-mint", help="nft_mint <collection> <token_id> <owner> <metadata>")
    nft_mint.add_argument("collection")
    nft_mint.add_argument("token_id")
    nft_mint.add_argument("owner")
    nft_mint.add_argument("metadata", nargs="?", default="")

    nft_transfer = sub.add_parser("nft-transfer", help="nft_transfer <collection> <token_id> <from> <to>")
    nft_transfer.add_argument("collection")
    nft_transfer.add_argument("token_id")
    nft_transfer.add_argument("from_addr")
    nft_transfer.add_argument("to")

    nft_owner = sub.add_parser("nft-owner", help="nft_owner <collection> <token_id>")
    nft_owner.add_argument("collection")
    nft_owner.add_argument("token_id")
    return parser


def _client_from_args(args: argparse.Namespace) -> TokenClient:
    if args.rpc_host not in {"127.0.0.1", "localhost", "::1"}:
        raise TokenCliError(
            "this CLI talks to local TEXT RPC only (127.0.0.1). "
            "No public token endpoint is published."
        )
    rpc = TextRpcClient(host=args.rpc_host, port=args.rpc_port, token=args.rpc_token)
    return TokenClient(rpc=rpc)


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        client = _client_from_args(args)
        if args.command == "getinfo":
            print(client.getinfo())
            return 0
        if args.command == "create":
            print(client.create(args.symbol, args.owner, args.max_supply, args.initial_mint))
            return 0
        if args.command == "create-ex":
            print(
                client.create_ex(
                    args.symbol,
                    args.name,
                    args.owner,
                    args.max_supply,
                    args.initial_mint,
                    args.decimals,
                    bool(args.burnable),
                    "" if args.dev_wallet == "-" else args.dev_wallet,
                    args.dev_allocation,
                )
            )
            return 0
        if args.command == "mint":
            print(client.mint(args.symbol, args.caller, args.to, args.amount))
            return 0
        if args.command == "transfer":
            print(client.transfer(args.symbol, args.from_addr, args.to, args.amount))
            return 0
        if args.command == "sign-payload":
            print(client.sign_payload(args.symbol, args.from_addr, args.to, args.amount))
            return 0
        if args.command == "transfer-signed":
            print(
                client.transfer_signed(
                    args.symbol,
                    args.from_addr,
                    args.to,
                    args.amount,
                    args.pubkey,
                    args.sig,
                )
            )
            return 0
        if args.command == "transfer-wallet":
            print(client.transfer_wallet(args.wallet, args.symbol, args.to, args.amount))
            return 0
        if args.command == "burn":
            print(client.burn(args.symbol, args.from_addr, args.amount))
            return 0
        if args.command == "balance":
            print(client.balance(args.symbol, args.owner))
            return 0
        if args.command == "info":
            print(client.info(args.symbol))
            return 0
        if args.command == "nft-mint":
            print(client.nft_mint(args.collection, args.token_id, args.owner, args.metadata))
            return 0
        if args.command == "nft-transfer":
            print(client.nft_transfer(args.collection, args.token_id, args.from_addr, args.to))
            return 0
        if args.command == "nft-owner":
            print(client.nft_owner(args.collection, args.token_id))
            return 0
        parser.error(f"unknown command: {args.command}")
        return 2
    except (TokenCliError, TextRpcError, OSError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
