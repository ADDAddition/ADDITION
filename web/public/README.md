# ADDITION static site

Pages root for additionblockchain.com — mobile-first product chrome + mainnet explorer + installable PWA.

Primary nav (desktop + mobile bottom tabs): **Explore · Wallet · Get started · Status**, with a permanent **MAINNET** badge. More menu: Download, Local tools, Public RPC, Docs.

- `/` is the product homepage (what ADDITION is) plus explorer search / latest blocks / latest txs. Dedicated views: `/block/`, `/tx/`, `/address/`. Pretty URLs are folders with `index.html`.
- `/wallet/` is a Trust-like wallet (Home / Receive / Send / Activity nav). Create/open wallet in plain language. Mine/tokens live under More. Spends only via `/local-rpc` → loopback write (`8546` mainnet / `8545` testnet). On the public host the page is offline for writes and explains connecting a home node. Never POSTs `wallet_send` to public `:80`, `38545`, `38546`, or public `8545`/`8546`.
- `/join/` covers public mainnet first (seed `34.27.30.115:28546`, read `38546`) and research testnet second.
- `/status/` is public mainnet `getinfo` via `/api/rpc` (Worker `PUBLIC_RPC_HTTP` → `http://34.27.30.115:38546/rpc`). Height from live getinfo only.
- PWA: `manifest.webmanifest`, `sw.js`, theme `#f3f6fa` (light), icons 192/512 for Add to Home Screen.
- Brand: `logo-transparent.png`, `favicon.ico`, `apple-touch-icon.png`, `og.png`. Light teal-slate theme — no private-zone / gold / dark-privacy graphics.
- `/download/` desktop helper for mainnet / local (`addition-wallet-mainnet`, loopback `8546` only). `/local/` operator toolbox.
- Hero banners: muted autoplay loop `<video>` srcs point at durable `https://additionblockchain.com/banners/addition-banner-1.mp4` and `…/addition-banner-2.mp4`.
- Explorer calls `/api/rpc?cmd=…` for public-read. No consensus / economy code changes in this surface.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
<!-- deploy-trigger: mainnet-public-product -->
