# ADDITION static site

Pages root for additionblockchain.com.

Dark console: logo, short nav, EN/FR, live getinfo strip (`height`, `peers`, `network`, `pq_mode`). Down node → **RPC offline**, empty cells.

- Pretty URLs are folders with `index.html` (`/explorer/`, `/wallet/`, `/docs/commands/`, …).
- Explorer and status call `/api/rpc?cmd=…`. When the read RPC answers they show `getinfo` height and the last few `getblock` results. Down node → **RPC offline**, empty cells.
- Optional `?rpc=https://HOST:38545/rpc`.
- Worker var `PUBLIC_RPC_HTTP` is empty by default. Set it to the real `--public-rpc` HTTP URL. Do not commit trycloudflare URLs.
- Two local nodes: `docs/TWO_NODE_TESTNET.md`.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
