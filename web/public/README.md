# ADDITION static site

Publish the contents of this directory as the site root for a Pages project (for example additionblockchain.com).

- Pretty URLs are folders with `index.html` (`/explorer/`, `/wallet/`, `/docs/commands/`, …).
- Explorer and status call `/api/rpc?cmd=…`. When the read RPC answers they show `getinfo` height and the last few real `getblock` results. On a static host that path is absent unless you attach a real read RPC, so the UI shows **RPC offline** and stays empty.
- Optional query: `?rpc=https://HOST:38545/rpc` to point the browser at a public read HTTP adapter.
- Worker var `PUBLIC_RPC_HTTP` is empty by default (`wrangler.toml`). Leave it empty for **RPC offline**. Set it to the real `--public-rpc` HTTP URL when an operator has one. Do not commit trycloudflare URLs.
- Two local nodes: see `docs/TWO_NODE_TESTNET.md`.
- Do not add fake live stats. Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
