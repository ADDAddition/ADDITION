#!/usr/bin/env bash
# Deploy web/public to Cloudflare Workers + Assets (addition-testnet-site).
# Requires: CLOUDFLARE_API_TOKEN (Workers Scripts Edit + Assets).
# Optional: CLOUDFLARE_ACCOUNT_ID, PUBLIC_RPC_HTTP (public-read HTTP URL).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SITE="$ROOT/web/public"

if [[ -z "${CLOUDFLARE_API_TOKEN:-}" ]]; then
  echo "error: set CLOUDFLARE_API_TOKEN" >&2
  exit 1
fi

if ! command -v npx >/dev/null 2>&1; then
  echo "error: npx (Node.js) is required" >&2
  exit 1
fi

cd "$SITE"

EXTRA=()
if [[ -n "${PUBLIC_RPC_HTTP:-}" ]]; then
  EXTRA+=(--var "PUBLIC_RPC_HTTP:${PUBLIC_RPC_HTTP}")
fi

echo "Deploying $SITE as addition-testnet-site…"
npx --yes wrangler@4 deploy "${EXTRA[@]}"
echo "Done. Set the Worker route / custom domain to additionblockchain.com if it is not already attached."
