# ADDITION static site

Pages root for additionblockchain.com — mobile-first product chrome + research testnet explorer + installable PWA.

Primary nav (desktop + mobile bottom tabs): **Explore · Wallet · Get started · Status**, with a permanent **TESTNET** badge. More menu: Download, Local tools, Public RPC, Docs.

- `/` is the product homepage (what ADDITION is) plus explorer search / latest blocks / latest txs. Dedicated views: `/block/`, `/tx/`, `/address/`. Pretty URLs are folders with `index.html`.
- `/wallet/` is a Trust-like wallet home (balance, receive, send, activity). Create/open wallet in plain language. Mine/stake/tokens live under More. Spends only via `/local-rpc` → `127.0.0.1:8545`. On the public host the page is fail-closed and explains connecting a home node. Never POSTs write commands to public `:80`, `38545`, `38546`, or public `8545`.
- `/join/` covers research testnet (bootstrap `34.27.30.115:28545`, HTTP `:80` / `38545`) and mainnet home-node join (seed `34.27.30.115:28546`, read `38546`). Explorer stays testnet.
- `/status/` is public testnet `getinfo` via `/api/rpc` (Worker `PUBLIC_RPC_HTTP` → `https://rpc.additionblockchain.com/rpc`). Do not point at `38546` or `mainnet-rpc.additionblockchain.com` until operator GO after height &gt; 0.
- PWA: `manifest.webmanifest`, `sw.js`, theme `#f3f6fa` (light), icons for Add to Home Screen.
- Brand: `logo-transparent.png`, `favicon.ico`, `apple-touch-icon.png`, `og.png`.
- `/download/` desktop helper (loopback only). `/local/` operator toolbox.
- Explorer calls `/api/rpc?cmd=…` for public-read only. nginx `:80` stays proxy to testnet read (`127.0.0.1:38545`). No consensus / RPC allowlist changes in this surface.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
<!-- deploy-trigger: light-theme-live -->
