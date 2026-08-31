#!/usr/bin/env python3
"""Prove a packaged wallet binary starts and refuses non-loopback RPC."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


def run(cmd: list[str], env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    merged = os.environ.copy()
    if env:
        merged.update(env)
    return subprocess.run(cmd, capture_output=True, text=True, env=merged, timeout=60)


def smoke_gui(binary: Path) -> None:
    help_proc = run([str(binary), "--help"])
    if help_proc.returncode != 0:
        raise SystemExit("gui --help failed:\n%s\n%s" % (help_proc.stdout, help_proc.stderr))
    blob = (help_proc.stdout + help_proc.stderr).lower()
    if "mainnet" not in blob and "127.0.0.1" not in blob:
        raise SystemExit("gui --help must mention mainnet or 127.0.0.1")

    refused = run(
        [str(binary), "--cli", "getinfo"],
        env={"ADDITION_LOCAL_RPC_HOST": "8.8.8.8", "ADDITION_LOCAL_RPC_PORT": "8546"},
    )
    text = (refused.stdout + refused.stderr).lower()
    if refused.returncode == 0:
        raise SystemExit("gui must refuse non-loopback RPC")
    if "non-loopback" not in text:
        raise SystemExit("gui refuse message missing: %s" % text)

    local = run(
        [str(binary), "--cli", "getinfo"],
        env={"ADDITION_LOCAL_RPC_HOST": "127.0.0.1", "ADDITION_LOCAL_RPC_PORT": "1"},
    )
    local_text = (local.stdout + local.stderr).lower()
    if local.returncode == 0:
        raise SystemExit("gui getinfo on closed port 1 must not succeed")
    if "non-loopback" in local_text:
        raise SystemExit("loopback RPC was rejected as non-loopback")
    if "unreachable" not in local_text and "rpc" not in local_text:
        raise SystemExit("gui should report local RPC failure: %s" % local_text)


def smoke_cli(binary: Path) -> None:
    help_proc = run([str(binary), "--help"])
    if help_proc.returncode != 0:
        raise SystemExit("cli --help failed:\n%s\n%s" % (help_proc.stdout, help_proc.stderr))

    refused = run([str(binary), "--rpc-host", "8.8.8.8", "getinfo"])
    text = (refused.stdout + refused.stderr).lower()
    if refused.returncode == 0:
        raise SystemExit("cli must refuse non-loopback RPC")
    if "non-loopback" not in text:
        raise SystemExit("cli refuse message missing: %s" % text)

    local = run([str(binary), "--rpc-host", "127.0.0.1", "--rpc-port", "1", "getinfo"])
    local_text = (local.stdout + local.stderr).lower()
    if local.returncode == 0:
        raise SystemExit("cli getinfo on closed port 1 must not succeed")
    if "non-loopback" in local_text:
        raise SystemExit("cli loopback RPC was rejected as non-loopback")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print("usage: smoke_wallet_binary.py <gui-binary> [cli-binary]", file=sys.stderr)
        return 2
    gui = Path(argv[1])
    if not gui.is_file():
        print("error: missing binary %s" % gui, file=sys.stderr)
        return 2
    smoke_gui(gui)
    if len(argv) > 2:
        cli = Path(argv[2])
        if not cli.is_file():
            print("error: missing binary %s" % cli, file=sys.stderr)
            return 2
        smoke_cli(cli)
    print("smoke-ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
