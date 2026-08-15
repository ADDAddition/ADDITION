#!/usr/bin/env python3
"""Local/testnet ADDITION wallet helper.

Talks only to trusted RPC on 127.0.0.1:8545 (override with ADDITION_LOCAL_RPC_HOST/PORT).
Does not print or transmit private keys. createwallet stores ML-DSA-87 keys in
<data-dir>/wallets/<name>.wal on the node. This is a Bitcoin-like user model
(keys, UTXOs, send/receive, fee) — not BIP compatibility and not a BTC fork.
"""

from __future__ import annotations

import argparse
import os
import socket
import sys


def env_int(name: str, default: int) -> int:
    raw = os.environ.get(name, "").strip()
    if not raw:
        return default
    return int(raw)


def rpc(command: str, timeout: float = 30.0) -> str:
    host = os.environ.get("ADDITION_LOCAL_RPC_HOST", "127.0.0.1")
    port = env_int("ADDITION_LOCAL_RPC_PORT", 8545)
    if host not in {"127.0.0.1", "::1", "localhost"}:
        raise RuntimeError("wallet helper refuses non-loopback RPC hosts")
    payload = command.strip() + "\n"
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(payload.encode("utf-8"))
        chunks: list[bytes] = []
        while True:
            data = sock.recv(8192)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break
    return b"".join(chunks).decode("utf-8", errors="replace").strip()


def field(line: str, key: str) -> str:
    token = key + "="
    for part in line.split():
        if part.startswith(token):
            return part[len(token) :]
    return ""


def cmd_create(name: str) -> str:
    return rpc("createwallet " + name)


def cmd_list() -> str:
    return rpc("wallet_list")


def cmd_info(name: str) -> str:
    return rpc("wallet_info " + name)


def cmd_balance(name: str) -> str:
    return rpc("wallet_balance " + name)


def cmd_send(name: str, to_addr: str, amount: str, fee: str) -> str:
    line = "wallet_send %s %s %s" % (name, to_addr, amount)
    if fee:
        line += " " + fee
    return rpc(line)


def cmd_mine(address: str) -> str:
    return rpc("mine " + address, timeout=600.0)


def cmd_getinfo() -> str:
    return rpc("getinfo")


def cmd_stake(address: str, amount: str) -> str:
    return rpc("stake %s %s" % (address, amount))


def cmd_unstake(address: str, amount: str) -> str:
    return rpc("unstake %s %s" % (address, amount))


def cmd_claim(address: str) -> str:
    return rpc("stake_claim " + address)


def run_cli(args: argparse.Namespace) -> int:
    try:
        if args.cli_cmd == "createwallet":
            print(cmd_create(args.name or "default"))
        elif args.cli_cmd == "list":
            print(cmd_list())
        elif args.cli_cmd == "info":
            print(cmd_info(args.name))
        elif args.cli_cmd == "balance":
            print(cmd_balance(args.name))
        elif args.cli_cmd == "send":
            print(cmd_send(args.name, args.to, args.amount, args.fee or ""))
        elif args.cli_cmd == "mine":
            print(cmd_mine(args.address or field(cmd_info(args.name), "address")))
        elif args.cli_cmd == "getinfo":
            print(cmd_getinfo())
        elif args.cli_cmd == "stake":
            print(cmd_stake(args.address, args.amount))
        elif args.cli_cmd == "unstake":
            print(cmd_unstake(args.address, args.amount))
        elif args.cli_cmd == "claim":
            print(cmd_claim(args.address))
        else:
            print("error: unknown cli command", file=sys.stderr)
            return 2
    except OSError as exc:
        print("error: local RPC unreachable (%s)" % exc, file=sys.stderr)
        return 1
    except RuntimeError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    return 0


def run_gui() -> int:
    try:
        import tkinter as tk
        from tkinter import messagebox, ttk
    except ImportError:
        print("error: tkinter is not available; use --cli", file=sys.stderr)
        return 2

    root = tk.Tk()
    root.title("ADDITION local wallet (testnet)")
    root.geometry("720x640")

    status = tk.StringVar(value="Checking local RPC…")
    name_var = tk.StringVar(value="default")
    to_var = tk.StringVar()
    amount_var = tk.StringVar(value="1")
    fee_var = tk.StringVar(value="1")
    stake_var = tk.StringVar(value="1")
    output = tk.Text(root, height=16, wrap="word")

    def show(text: str) -> None:
        output.delete("1.0", "end")
        output.insert("1.0", text)

    def call(fn, *a):
        try:
            reply = fn(*a)
            show(reply)
            return reply
        except OSError as exc:
            status.set("RPC offline")
            show("RPC offline: %s" % exc)
            return ""
        except RuntimeError as exc:
            show("error: %s" % exc)
            return ""

    def refresh_info() -> None:
        info = call(cmd_getinfo)
        if info and not info.startswith("error:"):
            status.set("Local trusted RPC answered. height=%s network=%s" % (
                field(info, "height") or "?",
                field(info, "network") or "?",
            ))
        elif info:
            status.set(info)

    frm = ttk.Frame(root, padding=10)
    frm.pack(fill="both", expand=True)
    ttk.Label(frm, textvariable=status, wraplength=680).pack(anchor="w")
    ttk.Label(
        frm,
        text="Loopback RPC only. Keys stay in the node wallet file. Not BIP-compatible. Not a live mainnet.",
        wraplength=680,
    ).pack(anchor="w", pady=(0, 8))

    row = ttk.Frame(frm)
    row.pack(fill="x")
    ttk.Label(row, text="wallet name").pack(side="left")
    ttk.Entry(row, textvariable=name_var, width=24).pack(side="left", padx=6)

    btns = ttk.Frame(frm)
    btns.pack(fill="x", pady=6)
    ttk.Button(btns, text="createwallet", command=lambda: call(cmd_create, name_var.get().strip() or "default")).pack(side="left", padx=2)
    ttk.Button(btns, text="wallet_list", command=lambda: call(cmd_list)).pack(side="left", padx=2)
    ttk.Button(btns, text="wallet_info", command=lambda: call(cmd_info, name_var.get().strip())).pack(side="left", padx=2)
    ttk.Button(btns, text="getbalance", command=lambda: call(cmd_balance, name_var.get().strip())).pack(side="left", padx=2)
    ttk.Button(btns, text="getinfo", command=refresh_info).pack(side="left", padx=2)

    send = ttk.LabelFrame(frm, text="wallet_send (signs on the node; no privkey on the wire)")
    send.pack(fill="x", pady=8)
    ttk.Label(send, text="to").grid(row=0, column=0, sticky="w")
    ttk.Entry(send, textvariable=to_var, width=48).grid(row=0, column=1, sticky="ew")
    ttk.Label(send, text="amount").grid(row=1, column=0, sticky="w")
    ttk.Entry(send, textvariable=amount_var, width=12).grid(row=1, column=1, sticky="w")
    ttk.Label(send, text="fee (blank = node floor)").grid(row=2, column=0, sticky="w")
    ttk.Entry(send, textvariable=fee_var, width=12).grid(row=2, column=1, sticky="w")
    ttk.Button(
        send,
        text="Send",
        command=lambda: call(cmd_send, name_var.get().strip(), to_var.get().strip(), amount_var.get().strip(), fee_var.get().strip()),
    ).grid(row=3, column=1, sticky="w", pady=4)

    mine = ttk.LabelFrame(frm, text="mine / stake (local only; mine is memory-hard and can take a long time)")
    mine.pack(fill="x", pady=8)
    ttk.Button(
        mine,
        text="mine to this wallet",
        command=lambda: call(cmd_mine, field(cmd_info(name_var.get().strip()), "address")),
    ).pack(side="left", padx=2)
    ttk.Entry(mine, textvariable=stake_var, width=8).pack(side="left", padx=2)
    ttk.Button(
        mine,
        text="stake",
        command=lambda: call(cmd_stake, field(cmd_info(name_var.get().strip()), "address"), stake_var.get().strip()),
    ).pack(side="left", padx=2)
    ttk.Button(
        mine,
        text="unstake",
        command=lambda: call(cmd_unstake, field(cmd_info(name_var.get().strip()), "address"), stake_var.get().strip()),
    ).pack(side="left", padx=2)
    ttk.Button(
        mine,
        text="claim",
        command=lambda: call(cmd_claim, field(cmd_info(name_var.get().strip()), "address")),
    ).pack(side="left", padx=2)

    output.pack(in_=frm, fill="both", expand=True, pady=8)
    refresh_info()
    root.mainloop()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="ADDITION local/testnet wallet (127.0.0.1 RPC only)")
    parser.add_argument("--cli", dest="cli_cmd", help="createwallet|list|info|balance|send|mine|getinfo|stake|unstake|claim")
    parser.add_argument("--name", default="default")
    parser.add_argument("--to")
    parser.add_argument("--amount")
    parser.add_argument("--fee")
    parser.add_argument("--address")
    args = parser.parse_args()
    if args.cli_cmd:
        if args.cli_cmd in {"info", "balance", "send", "stake", "unstake", "claim"} and not args.name and args.cli_cmd in {"info", "balance", "send"}:
            print("error: --name required", file=sys.stderr)
            return 2
        if args.cli_cmd == "send" and (not args.to or not args.amount):
            print("error: send requires --to and --amount", file=sys.stderr)
            return 2
        return run_cli(args)
    if not os.environ.get("DISPLAY") and sys.platform != "win32" and sys.platform != "darwin":
        print("No DISPLAY; use --cli. Example: python3 web/addition_wallet_gui.py --cli getinfo", file=sys.stderr)
        return 2
    return run_gui()


if __name__ == "__main__":
    raise SystemExit(main())
