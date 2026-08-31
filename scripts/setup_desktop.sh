#!/usr/bin/env bash
# ADDITION desktop first-run (Linux).
# Installs build deps, builds additiond (requires liboqs + OpenSSL), starts a
# local node with loopback write RPC, optionally launches the Flutter wallet.
#
# Modes (what "mode" means here):
#   testnet  — --network testnet, write 127.0.0.1:8545  (default product)
#   mainnet  — --mainnet,         write 127.0.0.1:8546  (local/operator profile;
#              not a live public network; needs ADDITION_PRIVACY_MASTER_KEY ≥32)
#   regtest  — --regtest,         write 127.0.0.1:8547  (local min-diff; not public)
#
# Usage:
#   ./scripts/setup_desktop.sh                  # deps + build node + start testnet + hint wallet
#   ./scripts/setup_desktop.sh --mode testnet --run-wallet
#   ./scripts/setup_desktop.sh --deps-only
#   ./scripts/setup_desktop.sh --build-only
#   ./scripts/setup_desktop.sh --start-only --mode regtest
#   ./scripts/setup_desktop.sh --stop
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

MODE="testnet"
DO_DEPS=1
DO_BUILD=1
DO_START=1
DO_WALLET=0
DEPS_ONLY=0
BUILD_ONLY=0
START_ONLY=0
STOP=0
# Optional P2P bootstrap (testnet default = operator seed). Empty / --no-bootstrap skips.
BOOTSTRAP="${ADDITION_BOOTSTRAP:-}"
NO_BOOTSTRAP=0
FLUTTER_BIN="${FLUTTER_BIN:-}"
DESKTOP_HOME="${ADDITION_DESKTOP_HOME:-$HOME/addition-desktop}"
LIBOQS_SRC="${LIBOQS_SRC:-$HOME/liboqs}"

die() { echo "error: $*" >&2; exit 1; }
info() { echo "==> $*"; }

usage() {
  cat <<'EOF'
ADDITION desktop first-run (Linux)

  ./scripts/setup_desktop.sh [--mode testnet|mainnet|regtest]
  ./scripts/setup_desktop.sh --deps-only
  ./scripts/setup_desktop.sh --build-only
  ./scripts/setup_desktop.sh --start-only --mode testnet
  ./scripts/setup_desktop.sh --run-wallet --mode testnet
  ./scripts/setup_desktop.sh --stop --mode testnet
  ./scripts/setup_desktop.sh --start-only --bootstrap 34.27.30.115:28545
  ./scripts/setup_desktop.sh --start-only --no-bootstrap

Modes / write RPC (loopback only):
  testnet  127.0.0.1:8545  (default; bootstraps operator P2P unless --no-bootstrap)
  mainnet  127.0.0.1:8546  (local/operator; height follows getinfo; needs ADDITION_PRIVACY_MASTER_KEY ≥32)
  regtest  127.0.0.1:8547

Requires OpenSSL (libssl-dev) and liboqs — no fallback.
EOF
  exit 0
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --mode) MODE="${2:-}"; shift 2 ;;
    --mode=*) MODE="${1#*=}"; shift ;;
    --bootstrap) BOOTSTRAP="${2:-}"; shift 2 ;;
    --bootstrap=*) BOOTSTRAP="${1#*=}"; shift ;;
    --no-bootstrap) NO_BOOTSTRAP=1; BOOTSTRAP=""; shift ;;
    --run-wallet) DO_WALLET=1; shift ;;
    --deps-only) DEPS_ONLY=1; DO_BUILD=0; DO_START=0; shift ;;
    --build-only) BUILD_ONLY=1; DO_DEPS=0; DO_START=0; shift ;;
    --start-only) START_ONLY=1; DO_DEPS=0; DO_BUILD=0; shift ;;
    --stop) STOP=1; DO_DEPS=0; DO_BUILD=0; DO_START=0; shift ;;
    -h|--help) usage ;;
    *) die "unknown arg: $1 (see --help)" ;;
  esac
done

case "$MODE" in
  testnet|mainnet|regtest) ;;
  *) die "mode must be testnet|mainnet|regtest (got: $MODE)" ;;
esac

rpc_port_for_mode() {
  case "$1" in
    testnet) echo 8545 ;;
    mainnet) echo 8546 ;;
    regtest) echo 8547 ;;
  esac
}

p2p_port_for_mode() {
  case "$1" in
    testnet) echo 28547 ;;
    mainnet) echo 28548 ;;
    regtest) echo 28549 ;;
  esac
}

data_dir_for_mode() {
  echo "$DESKTOP_HOME/$1"
}

pid_file_for_mode() {
  echo "$DESKTOP_HOME/$1/additiond.pid"
}

log_file_for_mode() {
  echo "$DESKTOP_HOME/$1/additiond.log"
}

network_args_for_mode() {
  case "$1" in
    testnet) echo --network testnet ;;
    mainnet) echo --mainnet ;;
    regtest) echo --regtest ;;
  esac
}

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || return 1
}

install_deps() {
  info "Installing OS packages (sudo). Needs: cmake g++ ninja libssl-dev git pkg-config libgtk-3-dev python3"
  if ! need_cmd sudo; then
    die "sudo not available; install packages manually (see README)"
  fi
  sudo apt-get update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    cmake g++ ninja-build libssl-dev git pkg-config \
    libgtk-3-dev clang python3 curl unzip xz-utils \
    libstdc++-14-dev || sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
    cmake g++ ninja-build libssl-dev git pkg-config \
    libgtk-3-dev clang python3 curl unzip xz-utils \
    libstdc++-13-dev

  if [[ -z "$FLUTTER_BIN" ]]; then
    if need_cmd flutter; then
      FLUTTER_BIN="$(command -v flutter)"
    elif [[ -x "$HOME/flutter/bin/flutter" ]]; then
      FLUTTER_BIN="$HOME/flutter/bin/flutter"
    else
      info "Flutter not found — cloning stable to \$HOME/flutter"
      git clone --depth 1 -b stable https://github.com/flutter/flutter.git "$HOME/flutter"
      FLUTTER_BIN="$HOME/flutter/bin/flutter"
    fi
  fi
  export PATH="$(dirname "$FLUTTER_BIN"):$PATH"
  "$FLUTTER_BIN" config --no-analytics >/dev/null 2>&1 || true
  "$FLUTTER_BIN" config --enable-linux-desktop >/dev/null 2>&1 || true
  info "Flutter: $($FLUTTER_BIN --version 2>/dev/null | head -1)"
}

ensure_liboqs() {
  if [[ -f /usr/local/lib/liboqs.so ]] || [[ -f /usr/local/lib/liboqs.so.7 ]] || ldconfig -p 2>/dev/null | grep -q liboqs; then
    info "liboqs already present"
    return 0
  fi
  info "Building liboqs into /usr/local (required — no fallback)"
  if [[ ! -d "$LIBOQS_SRC/.git" ]]; then
    git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git "$LIBOQS_SRC"
  fi
  cmake -S "$LIBOQS_SRC" -B "$LIBOQS_SRC/build" -GNinja \
    -DOQS_USE_OPENSSL=ON -DBUILD_SHARED_LIBS=ON
  cmake --build "$LIBOQS_SRC/build"
  sudo cmake --install "$LIBOQS_SRC/build"
  sudo ldconfig
}

build_additiond() {
  ensure_liboqs
  info "Building additiond (OpenSSL + liboqs required)"
  cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$ROOT/build" --target additiond
  [[ -x "$ROOT/build/additiond" ]] || die "build/additiond missing after build"
  info "Built $ROOT/build/additiond"
}

stop_node() {
  local mode="$1"
  local pf
  pf="$(pid_file_for_mode "$mode")"
  if [[ -f "$pf" ]]; then
    local pid
    pid="$(cat "$pf")"
    if kill -0 "$pid" 2>/dev/null; then
      info "Stopping additiond pid=$pid mode=$mode"
      kill "$pid" 2>/dev/null || true
      sleep 1
      kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$pf"
  fi
}

start_node() {
  local mode="$1"
  local bin="$ROOT/build/additiond"
  local port p2p data log pf args
  port="$(rpc_port_for_mode "$mode")"
  p2p="$(p2p_port_for_mode "$mode")"
  data="$(data_dir_for_mode "$mode")"
  log="$(log_file_for_mode "$mode")"
  pf="$(pid_file_for_mode "$mode")"
  mkdir -p "$data"

  [[ -x "$bin" ]] || die "additiond not found at $bin — run without --start-only first"

  if [[ "$mode" == "mainnet" ]]; then
    if [[ -z "${ADDITION_PRIVACY_MASTER_KEY:-}" ]] || [[ "${#ADDITION_PRIVACY_MASTER_KEY}" -lt 32 ]]; then
      die "mainnet requires ADDITION_PRIVACY_MASTER_KEY (≥32 chars). Example: export ADDITION_PRIVACY_MASTER_KEY=\$(python3 -c 'print(\"x\"*32)')"
    fi
  else
    # Privacy opening commands need the key; set a local desktop default if unset.
    if [[ -z "${ADDITION_PRIVACY_MASTER_KEY:-}" ]]; then
      export ADDITION_PRIVACY_MASTER_KEY="addition-desktop-local-key-do-not-use-in-prod"
      info "Set temporary ADDITION_PRIVACY_MASTER_KEY for local desktop (override in env)"
    fi
  fi

  stop_node "$mode"

  # shellcheck disable=SC2046
  args=( $(network_args_for_mode "$mode")
    --data-dir "$data"
    --local-rpc-port "$port"
    --p2p-port "$p2p"
  )

  local bootstrap="$BOOTSTRAP"
  if [[ "$NO_BOOTSTRAP" -eq 0 && -z "$bootstrap" && "$mode" == "testnet" ]]; then
    bootstrap="34.27.30.115:28545"
  fi
  if [[ -n "$bootstrap" ]]; then
    args+=(--bootstrap "$bootstrap")
    info "P2P bootstrap=$bootstrap (write RPC stays 127.0.0.1:$port)"
  fi

  info "Starting additiond mode=$mode write=127.0.0.1:$port data=$data"
  nohup "$bin" "${args[@]}" >"$log" 2>&1 </dev/null &
  echo $! >"$pf"
  sleep 2
  if ! kill -0 "$(cat "$pf")" 2>/dev/null; then
    die "additiond failed to stay up — see $log"
  fi

  if python3 - "$port" <<'PY'
import socket, sys
port = int(sys.argv[1])
try:
    s = socket.create_connection(("127.0.0.1", port), timeout=3)
    s.sendall(b"getinfo\n")
    data = s.recv(4096).decode(errors="replace")
    print(data.strip())
    if "network=" not in data:
        sys.exit(1)
except OSError as e:
    print("RPC offline:", e, file=sys.stderr)
    sys.exit(1)
PY
  then
    info "Write RPC answering on 127.0.0.1:$port"
  else
    die "getinfo failed on 127.0.0.1:$port — see $log"
  fi

  cat <<EOF

Desktop node is up.
  mode:     $mode
  write:    127.0.0.1:$port   (loopback only — public mine/createwallet stay disabled)
  data:     $data
  log:      $log
  stop:     $0 --stop --mode $mode

Flutter wallet:
  cd client/addition_app && flutter pub get && flutter run -d linux
  # Or: $0 --mode $mode --run-wallet
  # In the app (#55 wallet): Status → Connect / getinfo → Create wallet.

Optional loopback tools:
  python3 web/evm/evm_rpc_bridge.py          # JSON-RPC :9545 (send disabled)
  python3 tools/mining_pool.py coordinator   # serializes mine (not NiceHash)

EOF
}

run_wallet() {
  local port
  port="$(rpc_port_for_mode "$MODE")"
  export PATH="${FLUTTER_BIN:+$(dirname "$FLUTTER_BIN"):}$PATH"
  need_cmd flutter || die "flutter not on PATH"
  info "Launching Flutter wallet (mode=$MODE → 127.0.0.1:$port)"
  cd "$ROOT/client/addition_app"
  flutter pub get
  # Pass mode via dart-define so the first screen opens on the right port.
  flutter run -d linux \
    --dart-define=ADDITION_MODE="$MODE" \
    --dart-define=ADDITION_RPC_PORT="$port"
}

# --- main ---
if [[ "$STOP" -eq 1 ]]; then
  stop_node "$MODE"
  info "Stopped mode=$MODE"
  exit 0
fi

if [[ "$DEPS_ONLY" -eq 1 ]]; then
  install_deps
  exit 0
fi

if [[ "$START_ONLY" -eq 1 ]]; then
  start_node "$MODE"
  exit 0
fi

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  build_additiond
  exit 0
fi

if [[ "$DO_DEPS" -eq 1 ]]; then
  install_deps
fi
if [[ "$DO_BUILD" -eq 1 ]]; then
  build_additiond
fi
if [[ "$DO_START" -eq 1 ]]; then
  start_node "$MODE"
fi
if [[ "$DO_WALLET" -eq 1 ]]; then
  run_wallet
fi

exit 0
