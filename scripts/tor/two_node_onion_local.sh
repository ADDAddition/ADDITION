#!/usr/bin/env bash
# Local two-node regtest over real Tor onion services (needs `tor` in PATH and internet).
# Keys stay in $RUN (mode 700) and are never printed. Stop: kill the pids in $RUN/*.pid.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; BIN="${ADDITIOND:-$ROOT/build/additiond}"
RUN="${RUN:-$(mktemp -d /tmp/addn.XXXX)}"; chmod 700 "$RUN"; cd "$RUN"
cat > torrc <<T
SocksPort 127.0.0.1:19050
DataDirectory $RUN/tordata
HiddenServiceDir $RUN/hsA
HiddenServicePort 29444 127.0.0.1:29444
HiddenServiceDir $RUN/hsB
HiddenServicePort 29445 127.0.0.1:29445
Log notice file $RUN/tor.log
T
tor -f torrc >/dev/null 2>&1 & echo $! > tor.pid
until grep -q "Bootstrapped 100%" tor.log 2>/dev/null; do sleep 2; done
A=$(cat hsA/hostname); B=$(cat hsB/hostname); export ADDITION_ENABLE_P2P_RPC=1
ADDITION_ADVERTISED_P2P=$A:29444 "$BIN" --regtest --data-dir "$RUN/na" --onion-only --tor-proxy 127.0.0.1:19050 \
  --dandelion --p2p-port 29444 --local-rpc-port 29544 > na.log 2>&1 & echo $! > na.pid
ADDITION_ADVERTISED_P2P=$B:29445 "$BIN" --regtest --data-dir "$RUN/nb" --onion-only --tor-proxy 127.0.0.1:19050 \
  --dandelion --p2p-port 29445 --local-rpc-port 29545 --bootstrap "$A:29444" > nb.log 2>&1 & echo $! > nb.pid
echo "run dir: $RUN  (mine on 127.0.0.1:29544, then 'sync' on 127.0.0.1:29545; onion sync takes ~30-60s)"
