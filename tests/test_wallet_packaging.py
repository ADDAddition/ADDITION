#!/usr/bin/env python3
"""Packaging and loopback checks for the testnet/local wallet binaries."""

from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
if str(WEB) not in sys.path:
    sys.path.insert(0, str(WEB))

from addition_wallet import TextRpcClient, WalletError  # noqa: E402
import addition_wallet_gui as wallet_gui  # noqa: E402


class WalletPackagingTests(unittest.TestCase):
    def test_cli_refuses_non_loopback_host(self) -> None:
        with self.assertRaises(WalletError) as ctx:
            TextRpcClient(host="8.8.8.8")
        self.assertIn("non-loopback", str(ctx.exception))
        TextRpcClient(host="127.0.0.1")
        TextRpcClient(host="localhost")

    def test_gui_rpc_refuses_non_loopback_host(self) -> None:
        old = os.environ.get("ADDITION_LOCAL_RPC_HOST")
        os.environ["ADDITION_LOCAL_RPC_HOST"] = "1.2.3.4"
        try:
            with self.assertRaises(RuntimeError) as ctx:
                wallet_gui.rpc("getinfo")
            self.assertIn("non-loopback", str(ctx.exception))
        finally:
            if old is None:
                os.environ.pop("ADDITION_LOCAL_RPC_HOST", None)
            else:
                os.environ["ADDITION_LOCAL_RPC_HOST"] = old

    def test_build_scripts_exist(self) -> None:
        linux = ROOT / "scripts" / "build_wallet.sh"
        windows = ROOT / "scripts" / "build_wallet.ps1"
        smoke = ROOT / "scripts" / "smoke_wallet_binary.py"
        readme = ROOT / "packaging" / "README.md"
        self.assertTrue(linux.is_file())
        self.assertTrue(windows.is_file())
        self.assertTrue(smoke.is_file())
        self.assertTrue(readme.is_file())
        text = readme.read_text(encoding="utf-8")
        self.assertIn("./scripts/build_wallet.sh", text)
        self.assertIn("testnet", text.lower())
        self.assertIn("127.0.0.1", text)
        self.assertIn("addition-wallet-testnet.exe", text)
        self.assertIn(".\\addition-wallet-testnet.exe --cli getinfo", text)
        self.assertIn(r"$HOME\addition-testnet", text)
        self.assertIn("web/public/download/", text)
        self.assertNotIn("--data-dir <dir>", text)
        self.assertNotIn("wallet-connect", text.lower())
        self.assertNotRegex(text.lower(), r"\bhonest\b")
        wallet_src = (WEB / "addition_wallet.py").read_text(encoding="utf-8")
        self.assertNotIn("fchmod", wallet_src)

    def test_packaged_binary_smoke_when_present(self) -> None:
        gui = ROOT / "web" / "public" / "download" / "addition-wallet-testnet"
        cli = ROOT / "web" / "public" / "download" / "addition-wallet-cli-testnet"
        if not gui.is_file():
            self.skipTest("run ./scripts/build_wallet.sh to produce the Linux binary")
        cmd = [sys.executable, str(ROOT / "scripts" / "smoke_wallet_binary.py"), str(gui)]
        if cli.is_file():
            cmd.append(str(cli))
        proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertIn("smoke-ok", proc.stdout)


if __name__ == "__main__":
    unittest.main()
