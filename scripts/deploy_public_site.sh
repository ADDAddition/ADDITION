#!/usr/bin/env bash
# Deploy web/public to the live catch-all Worker on additionblockchain.com.
# Live routes: additionblockchain.com/* → Worker "addition-explorer"
# Requires: CLOUDFLARE_API_TOKEN (Workers Scripts Edit + Assets) for account
#   c4ceeebc81b5a07252d4d4244e50a916 (Admin@addisonelectronique.com).
# Optional: PUBLIC_RPC_HTTP (default: http://r38546.additionblockchain.com:38546/rpc).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SITE="$ROOT/web/public"
NAME="${CLOUDFLARE_WORKER_NAME:-addition-explorer}"
RPC_DEFAULT="http://r38546.additionblockchain.com:38546/rpc"

if [[ -z "${CLOUDFLARE_API_TOKEN:-}" ]]; then
  echo "error: set CLOUDFLARE_API_TOKEN" >&2
  exit 1
fi

if ! command -v npx >/dev/null 2>&1; then
  echo "error: npx (Node.js) is required" >&2
  exit 1
fi

cd "$SITE"
RPC="${PUBLIC_RPC_HTTP:-$RPC_DEFAULT}"

echo "Deploying $SITE as Worker $NAME (PUBLIC_RPC_HTTP=$RPC)…"
npx --yes wrangler@4 deploy --name "$NAME" --var "PUBLIC_RPC_HTTP:${RPC}"
echo "Done. Catch-all routes on additionblockchain.com should already point at $NAME."
