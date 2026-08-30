# ADDITION static site

Pages root for additionblockchain.com.

White explorer: live chain status strip (`getinfo` + `monetary_info` fields when present), search that routes to block / tx / address views, latest `getblock` rows, and latest `tx_hashes` from those blocks. Header is Explorer | Status | Join. Footer is `testnet · contact@additionblockchain.com`. Down node → **RPC offline** and empty tables.

- `/` is the explorer. Dedicated views: `/block/`, `/tx/`, `/address/`. Pretty URLs are folders with `index.html` (`/join/`, `/status/`, `/wallet/`, `/download/`, `/docs/commands/`, …).
- Brand files are the existing wordmark: `logo-transparent.png`, `favicon.ico`, `apple-touch-icon.png`, `og.png`.
- `/download/` is the testnet / local wallet binary page. No hosted web wallet.
- `/rpc/` is the public-read command explorer for the daemon allowlist only (no invented EVM methods).
- Raw markdown (no HTML scrape): `/join.md`, `/docs/join.md`, `/docs/testnet-rpc-runbook.md`, `/docs/wallet.md`. Served as `text/markdown`. `/docs/join.md` is a copy of `/join.md`.
- Join path: build `additiond` from repo `main`, `--bootstrap 34.27.30.115:28545`, then `sync`. Public HTTP `:80` first; `38545` and `28545` are optional/filtered.
- Explorer calls `/api/rpc?cmd=…` for public-read only: `getinfo`, `monetary_info`, `crypto_selftest`, `tx_status`, `peers`, `getblock`, `getblockhash`, `getblockraw`. No invented height, hash, tx_count, or time. Address balance/history is not on the public allowlist.
- Optional `?rpc=https://HOST:38545/rpc`.
- Worker secret `PUBLIC_RPC_HTTP` is unset by default. Set it with `npx wrangler secret put PUBLIC_RPC_HTTP` to the real `--public-rpc` HTTP URL. Do not commit trycloudflare URLs.
- Two local nodes: `docs/TWO_NODE_TESTNET.md`.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
