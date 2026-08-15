#!/usr/bin/env python3
"""Live smoke path against a local additiond TEXT RPC (127.0.0.1:8545).

Creates two wallets, mines to the first, sends a PQ-signed tx to the second.
Requires a running `./build/additiond --network testnet` and liboqs.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
if str(WEB) not in sys.path:
    sys.path.insert(0, str(WEB))

from addition_wallet import TextRpcClient, WalletClient, WalletError, WalletStore


def main() -> int:
    rpc_probe = TextRpcClient()
    try:
        info = rpc_probe.call("getinfo")
    except OSError as exc:
        print("error: daemon not reachable on 127.0.0.1:8545:", exc, file=sys.stderr)
        print("start: ./build/additiond --network testnet", file=sys.stderr)
        return 2
    print("getinfo", info)
    if "network=testnet" not in info:
        print("error: expected network=testnet", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        sender_path = Path(tmp) / "sender.wallet"
        dest_path = Path(tmp) / "dest.wallet"
        sender = WalletClient(rpc=TextRpcClient(), store=WalletStore(sender_path))
        dest = WalletClient(rpc=TextRpcClient(), store=WalletStore(dest_path))
        try:
            s = sender.create()
            d = dest.create()
            print("sender", s.address)
            print("dest", d.address)
            print(sender.mine())
            print("sender_balance", sender.balance())
            print("send", sender.send(d.address, 10, fee=1))
            print(sender.mine())
            print("dest_balance", dest.balance())
        except WalletError as exc:
            print("error:", exc, file=sys.stderr)
            return 1
        for command in sender.rpc.sent + dest.rpc.sent:
            if s.private_key in command or d.private_key in command:
                print("error: private key leaked onto RPC", file=sys.stderr)
                return 1
    print("wallet_smoke: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
