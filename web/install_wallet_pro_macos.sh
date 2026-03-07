#!/usr/bin/env bash
set -euo pipefail

echo "[wallet] Addition Wallet Pro macOS installer"

if ! command -v python3 >/dev/null 2>&1; then
  echo "[wallet] python3 not found."
  if command -v brew >/dev/null 2>&1; then
    echo "[wallet] Installing Python via Homebrew..."
    brew install python
  else
    echo "[wallet] Homebrew not found. Install Homebrew first: https://brew.sh"
    exit 1
  fi
fi

echo "[wallet] Python version:"
python3 --version

echo "[wallet] Upgrade pip + install dependencies"
python3 -m pip install --upgrade pip
python3 -m pip install --upgrade requests

echo "[wallet] Done. Launch with: python3 web/addition_wallet_pro.py"