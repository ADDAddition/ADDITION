#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v python3 >/dev/null 2>&1; then
  echo "[wallet] python3 missing, running installer..."
  bash web/install_wallet_pro_macos.sh
fi

echo "[wallet] Launching Addition Wallet Pro"
python3 web/addition_wallet_pro.py