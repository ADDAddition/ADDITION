# ADDITION static site

Publish the contents of this directory as the site root for a Pages project (for example additionblockchain.com).

- Pretty URLs are folders with `index.html` (`/explorer/`, `/docs/commands/`, …).
- Explorer and status call `/api/rpc?cmd=…`. On a static host that path is absent unless you attach a real read RPC, so the UI shows **RPC offline** and stays empty.
- Optional query: `?rpc=https://HOST:38545/rpc` to point the browser at a public read HTTP adapter.
- Do not add fake live stats. Contact: labjay69@gmail.com
- Source: https://github.com/ADDAddition/ADDITION
