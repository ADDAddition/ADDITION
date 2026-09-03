# ADDITION static site

Pages root for additionblockchain.com — mobile-first product chrome + mainnet explorer + installable PWA.

Primary nav (desktop + mobile bottom tabs): **Explore · Wallet · Get started · Status**, with a permanent **MAINNET** badge. More menu: Download, Local tools, Launch, Embed, Public RPC, Docs.

- `/` is the product homepage (what ADDITION is) plus explorer search / latest blocks / latest txs. Dedicated views: `/block/`, `/tx/`, `/address/`. Pretty URLs are folders with `index.html`.
- `/wallet/` is a Trust-like wallet (Home / Receive / Send / Activity nav). Create/open wallet in plain language. Mine/tokens live under More. Spends only via `/local-rpc` → loopback write (`8546` mainnet / `8545` testnet). On the public host the page is offline for writes and explains connecting a home node. Never POSTs `wallet_send` to public `:80`, `38545`, `38546`, or public `8545`/`8546`.
- `/join/` covers public mainnet first (seed `34.27.30.115:28546`, read `38546`) and research testnet second.
- `/status/` is public mainnet `getinfo` via `/api/rpc` (Worker `PUBLIC_RPC_HTTP` → `http://34.27.30.115:38546/rpc`). Height from live getinfo only.
- `/api/info` (and `/api/`) returns JSON from live `getinfo` + `monetary_info`. `price_usd` is always null — no invented ticker. `/api/capabilities` probes launch RPCs on the public path.
- `/embed/` is an iframe widget (themes `modern|compact|bare`) fed by `/api/info`. Says price unavailable when the node has no price RPC.
- `/launch/` has Create Token / Presale / Airdrop / Farm tabs. Tabs stay feature-off unless `/api/capabilities` finds those commands on public mainnet RPC. No fake factory UI.
- PWA: `manifest.webmanifest`, `sw.js`, theme `#f3f6fa` (light), icons 192/512 for Add to Home Screen.
- Brand: `logo-transparent.png`, `favicon.ico`, `apple-touch-icon.png`, `og.png`. Light teal-slate theme — no private-zone / gold / dark-privacy graphics.
- `/download/` desktop helper for mainnet / local (`addition-wallet-mainnet`, loopback `8546` only). `/local/` operator toolbox.
- Homepage hero: one muted autoplay loop `playsinline` `webkit-playsinline` `<video>` with `preload="auto"` (no controls) at `https://additionblockchain.com/banners/addition-stinger.mp4` — not dual banner-1/banner-2 tiles. `chrome.js` kicks `.hero-stinger` with `muted=true` + `play().catch(()=>{})` so Safari/Chrome actually start. On ≤840px, `.hero-stinger` / `video.hero-stinger` use live `max-height: 56vw` + `object-fit: contain` (no `max-height: none`).
- Site footer (`chrome.js`): one muted loop `playsinline` `<video>` (no controls, **no autoplay**) at `https://additionblockchain.com/banners/addition-banner-2.mp4` — full-width `object-fit: contain`. Only the hero stinger autoplays. Banner-1 stays reserved for Addition Core About.
- Explorer calls `/api/rpc?cmd=…` for public-read. No consensus / economy code changes in this surface.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
<!-- deploy-trigger: mainnet-public-product -->
