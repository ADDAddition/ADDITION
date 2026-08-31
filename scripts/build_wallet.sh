#!/usr/bin/env bash
# Build Linux mainnet/local wallet binaries and smoke-test them.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ADDITION_WALLET_DIST:-$ROOT/web/public/download}"
WORK="$ROOT/packaging/pyi-build"
mkdir -p "$OUT" "$WORK"

if ! python3 -c "import PyInstaller" >/dev/null 2>&1; then
  python3 -m pip install --user "pyinstaller>=6.0"
fi

build_one() {
  local name="$1"
  local src="$2"
  python3 -m PyInstaller \
    --noconfirm \
    --clean \
    --onefile \
    --console \
    --name "$name" \
    --distpath "$OUT" \
    --workpath "$WORK/$name" \
    --specpath "$WORK/spec" \
    "$src"
}

build_one "addition-wallet-mainnet" "$ROOT/web/addition_wallet_gui.py"
build_one "addition-wallet-cli-mainnet" "$ROOT/web/addition_wallet.py"

chmod +x "$OUT/addition-wallet-mainnet" "$OUT/addition-wallet-cli-mainnet"
python3 "$ROOT/scripts/smoke_wallet_binary.py" \
  "$OUT/addition-wallet-mainnet" \
  "$OUT/addition-wallet-cli-mainnet"
echo "wallet binaries ready in $OUT"
