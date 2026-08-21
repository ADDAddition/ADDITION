# ADDITION static site

Pages root for additionblockchain.com.

White explorer: search + latest `getblock` rows from live public read RPC. Header is Explorer | Status | Join. Footer is `testnet · contact@additionblockchain.com`. Down node → **RPC offline** and an empty table.

- `/` is the explorer. Pretty URLs are folders with `index.html` (`/join/`, `/status/`, `/wallet/`, `/download/`, `/docs/commands/`, …).
- Brand files are the existing wordmark: `logo-transparent.png`, `favicon.ico`, `apple-touch-icon.png`, `og.png`.
- `/download/` is the testnet / local wallet binary page. No hosted web wallet.
- Raw markdown (no HTML scrape): `/join.md`, `/docs/join.md`, `/docs/testnet-rpc-runbook.md`, `/docs/wallet.md`. Served as `text/markdown`. `/docs/join.md` is a copy of `/join.md`.
- Join path: build `additiond` from repo `main`, `--bootstrap 34.27.30.115:28545`, then `sync`. Public HTTP `:80` first; `38545` and `28545` are optional/filtered.
- Explorer calls `/api/rpc?cmd=…` for `getinfo` / `getblock` / `getblockhash` only. No invented height, hash, tx_count, or time.
- Optional `?rpc=https://HOST:38545/rpc`.
- Worker secret `PUBLIC_RPC_HTTP` is unset by default. Set it with `npx wrangler secret put PUBLIC_RPC_HTTP` to the real `--public-rpc` HTTP URL. Do not commit trycloudflare URLs.
- Two local nodes: `docs/TWO_NODE_TESTNET.md`.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
