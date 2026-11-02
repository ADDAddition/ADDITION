#!/usr/bin/env bash
# Run ON A SEED MACHINE (as root) to create its Tor v3 onion service and print
# the public seed line to add to seeds-mainnet.txt. Prints only the public
# hostname; the private key stays in /var/lib/tor and is never shown.
#
#   sudo scripts/tor/seed-node/setup_seed_hidden_service.sh
#
# Requires: tor (apt install tor). Does not start additiond and changes no
# firewall rule. Port 28546 is only bound on 127.0.0.1 behind Tor.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
HS_DIR=/var/lib/tor/addition_mainnet_seed
PORT=28546
[[ $EUID -eq 0 ]] || { echo "run as root (sudo)" >&2; exit 1; }
command -v tor >/dev/null || { echo "tor not installed: apt install tor" >&2; exit 1; }

TORRC=/etc/tor/torrc
if ! grep -q "HiddenServiceDir $HS_DIR" "$TORRC" 2>/dev/null; then
  cp -a "$TORRC" "$TORRC.bak.$(date +%s)" 2>/dev/null || true
  {
    echo ""
    echo "# --- ADDITION mainnet seed (added by setup_seed_hidden_service.sh) ---"
    grep -v '^\s*#' "$HERE/torrc.seed" | grep -v '^\s*$' | grep -v '^SocksPort\|^DataDirectory'
  } >> "$TORRC"
fi
tor --verify-config -f "$TORRC" >/dev/null
systemctl restart tor 2>/dev/null || service tor restart

for _ in $(seq 1 60); do [[ -s "$HS_DIR/hostname" ]] && break; sleep 1; done
[[ -s "$HS_DIR/hostname" ]] || { echo "tor did not create $HS_DIR/hostname" >&2; exit 1; }
chmod 700 "$HS_DIR"
HOST="$(tr -d '[:space:]' < "$HS_DIR/hostname")"
echo "seed line for seeds-mainnet.txt:"
echo "${HOST}:${PORT}"
echo
echo "start the node (onion-only):"
echo "  ADDITION_ADVERTISED_P2P=${HOST}:${PORT} additiond --mainnet --onion-only \\"
echo "    --tor-proxy 127.0.0.1:9050 --p2p-port ${PORT} --seeds-file /etc/addition/seeds-mainnet.txt"
echo
echo "BACK UP $HS_DIR offline (encrypted). Losing it changes the onion address."
