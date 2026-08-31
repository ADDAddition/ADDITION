#!/usr/bin/env python3
"""Smoke the TEXT RPC surface the Flutter desktop wallet uses.

Requires a local additiond on 127.0.0.1:8545 (or ADDITION_LOCAL_RPC_PORT).
Does not print private keys. Exits non-zero on failure.
"""

from __future__ import annotations

import os
import socket
import sys
import time


def rpc(command: str, timeout: float = 60.0) -> str:
    host = os.environ.get("ADDITION_LOCAL_RPC_HOST", "127.0.0.1")
    port = int(os.environ.get("ADDITION_LOCAL_RPC_PORT", "8545"))
    if host not in {"127.0.0.1", "::1", "localhost"}:
        raise SystemExit("refusing non-loopback host")
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall((command.strip() + "\n").encode())
        chunks: list[bytes] = []
        while True:
            data = sock.recv(8192)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break
    return b"".join(chunks).decode(errors="replace").strip()


def field(line: str, key: str) -> str:
    token = key + "="
    for part in line.split():
        if part.startswith(token):
            return part[len(token) :]
    return ""


def main() -> int:
    name = "desktop_wallet_smoke_%d" % int(time.time())
    info = rpc("getinfo")
    print("getinfo:", info)
    if "network=" not in info:
        print("FAIL: getinfo missing network", file=sys.stderr)
        return 1
    if "priv=" in info or "privkey=" in info:
        print("FAIL: getinfo leaked priv", file=sys.stderr)
        return 1

    created = rpc("createwallet " + name)
    print("createwallet:", created.replace(field(created, "pub"), "<pub>") if field(created, "pub") else created)
    if field(created, "priv_printed") not in {"", "0"} and field(created, "priv"):
        print("FAIL: priv printed", file=sys.stderr)
        return 1
    addr = field(created, "address")
    if len(addr) != 128:
        print("FAIL: bad address", file=sys.stderr)
        return 1

    bal = rpc("wallet_balance " + name)
    print("wallet_balance:", bal)
    listed = rpc("wallet_list")
    print("wallet_list wallets=", field(listed, "wallets"))

    # Stake probe (usage vs unknown)
    stake_probe = rpc("staked")
    print("stake probe:", stake_probe)
    if "unknown command" in stake_probe:
        print("stake: not available")
    else:
        print("stake: available on this node")

    # Privacy opening path (may fail without MASTER_KEY — still exercise commands)
    prep = rpc("privacy_note_prepare 5")
    print("privacy_note_prepare:", "trapdoor=<redacted>" if "trapdoor=" in prep else prep)
    if prep.startswith("ok:") or "ADDITION_PRIVACY_MASTER_KEY" in prep or prep.startswith("error:"):
        pass
    else:
        print("FAIL: unexpected privacy prepare", file=sys.stderr)
        return 1

    pstatus = rpc("privacy_status")
    print("privacy_status:", pstatus)
    if "sha3_opening" not in pstatus and "opening_not_zk" not in pstatus and not pstatus.startswith("error:"):
        print("WARN: privacy_status missing honesty labels:", pstatus)

    # Mine (testnet has deadline; may return error on failure — still a real call)
    mined = rpc("mine " + addr, timeout=90.0)
    print("mine:", mined[:200])

    sync_line = rpc("sync")
    print("sync:", sync_line)

    print("OK desktop wallet RPC smoke")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except OSError as exc:
        print("RPC offline:", exc, file=sys.stderr)
        raise SystemExit(2)
