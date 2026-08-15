#!/usr/bin/env bash
# Stop the two local testnet processes started by start_two_node_testnet.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUN_DIR="${ADDITION_TWO_NODE_DIR:-$ROOT/data/two-node}"

stop_pidfile() {
  local file="$1"
  if [[ ! -f "$file" ]]; then
    return 0
  fi
  local pid
  pid="$(tr -d '[:space:]' <"$file")"
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid" 2>/dev/null || true
    local i
    for i in $(seq 1 20); do
      if ! kill -0 "$pid" 2>/dev/null; then
        break
      fi
      sleep 0.2
    done
    if kill -0 "$pid" 2>/dev/null; then
      kill -KILL "$pid" 2>/dev/null || true
    fi
  fi
  rm -f "$file"
}

stop_pidfile "$RUN_DIR/node-a.pid"
stop_pidfile "$RUN_DIR/node-b.pid"
echo "two-node testnet stopped"
