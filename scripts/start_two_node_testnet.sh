#!/usr/bin/env bash
# Start two local testnet processes. Not a public mainnet.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ADDITIOND:-$ROOT/build/additiond}"
RUN_DIR="${ADDITION_TWO_NODE_DIR:-$ROOT/data/two-node}"
PUB_BIND="${ADDITION_PUBLIC_RPC_BIND:-127.0.0.1}"

if [[ ! -x "$BIN" && ! -f "$BIN" ]]; then
  echo "error: additiond not found at $BIN" >&2
  echo "build: cmake -S . -B build && cmake --build build --target additiond" >&2
  exit 2
fi

mkdir -p "$RUN_DIR/node-a" "$RUN_DIR/node-b" "$RUN_DIR/logs"
cd "$ROOT"

if [[ -f "$RUN_DIR/node-a.pid" ]] || [[ -f "$RUN_DIR/node-b.pid" ]]; then
  echo "error: pid files already exist in $RUN_DIR (run scripts/stop_two_node_testnet.sh first)" >&2
  exit 1
fi

export ADDITION_ENABLE_P2P_RPC=1

# Node A: public read RPC + P2P 28545 + write 8545 loopback.
"$BIN" --network testnet \
  --data-dir "$RUN_DIR/node-a" \
  --public-rpc \
  --public-rpc-port 38545 \
  --public-rpc-bind "$PUB_BIND" \
  --local-rpc-port 8545 \
  --p2p-port 28545 \
  >"$RUN_DIR/logs/node-a.log" 2>&1 </dev/null &
echo $! >"$RUN_DIR/node-a.pid"

# Node B: second data dir, write 8546, P2P 28546, bootstrap to A.
"$BIN" --network testnet \
  --data-dir "$RUN_DIR/node-b" \
  --local-rpc-port 8546 \
  --p2p-port 28546 \
  --bootstrap 127.0.0.1:28545 \
  >"$RUN_DIR/logs/node-b.log" 2>&1 </dev/null &
echo $! >"$RUN_DIR/node-b.pid"

rpc_line() {
  local port="$1"
  local cmd="$2"
  python3 - "$port" "$cmd" <<'PY'
import socket
import sys

port = int(sys.argv[1])
cmd = sys.argv[2]
try:
    with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
        sock.sendall((cmd + "\n").encode("utf-8"))
        chunks = []
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break
    sys.stdout.write(b"".join(chunks).decode("utf-8", "replace").strip())
except OSError:
    sys.exit(2)
PY
}

wait_rpc() {
  local port="$1"
  local i
  for i in $(seq 1 90); do
    if rpc_line "$port" "getinfo" 2>/dev/null | grep -q 'network=testnet'; then
      return 0
    fi
    sleep 0.3
  done
  echo "error: timeout waiting for 127.0.0.1:$port" >&2
  echo "logs: $RUN_DIR/logs" >&2
  return 1
}

wait_rpc 8545
wait_rpc 38545
wait_rpc 8546

ADD_OUT="$(rpc_line 8546 "addpeer 127.0.0.1:28545" || true)"
if [[ "$ADD_OUT" == "ok" ]]; then
  echo "node B addpeer: ok"
elif [[ "$ADD_OUT" == "error: invalid/duplicate peer" ]]; then
  echo "node B addpeer: already present (bootstrap)"
else
  echo "node B addpeer: $ADD_OUT"
fi

cat <<EOF
two-node testnet running (local processes, not a public mainnet)

  Node A write RPC   127.0.0.1:8545
  Node A public RPC  ${PUB_BIND}:38545   (reads only)
  Node A P2P         0.0.0.0:28545     (ADDITION_ENABLE_P2P_RPC=1)
  Node B write RPC   127.0.0.1:8546
  Node B P2P         0.0.0.0:28546
  data               $RUN_DIR

Public checks:
  printf 'getinfo\\n' | nc 127.0.0.1 38545
  curl -s 'http://127.0.0.1:38545/rpc?cmd=getinfo'
  printf 'mine\\n' | nc 127.0.0.1 38545   # error: command disabled on public RPC

Website worker/site: leave PUBLIC_RPC_HTTP unset unless you have a real URL.
When this node is reachable, set the Worker secret PUBLIC_RPC_HTTP to the HTTP
adapter you actually run, for example:
  PUBLIC_RPC_HTTP=http://127.0.0.1:38545/rpc
Do not commit trycloudflare or other ephemeral tunnel URLs.

Stop: scripts/stop_two_node_testnet.sh
EOF
