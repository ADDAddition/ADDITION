# ADDITION static site

Pages root for additionblockchain.com — mobile-first product chrome + mainnet explorer + installable PWA.

**Public product desk = live only.** Nav surfaces what works on `ADDITION_MAINNET_V1` today. Incomplete factory / mockup desks are not linked from chrome.

Primary nav (desktop + mobile bottom tabs): **Explore · Wallet · Get started · Status**, with a permanent **MAINNET** badge. More menu: Compare, Privacy, Download, Whitepaper, Public RPC, Docs, Local tools, Embed.

- `/` is the product homepage (what ADDITION is) plus explorer search / latest blocks / latest txs, with a compare teaser. Dedicated views: `/block/`, `/tx/`, `/address/`. Pretty URLs are folders with `index.html`.
- `/compare/` (alias `/benchmark/` → `/compare/`) is a live-only table: ADDITION vs Bitcoin, Ethereum, Solana, Monero on shipped axes. No ADDITION TPS race, no invented height/price, no ZK-privacy claim.
- `/wallet/` is a Trust-like wallet (Home / Receive / Send / Activity nav). Prefers public mainnet write on seed `38546` via `/api/rpc` (createwallet / mine / wallet_send / sign / tx_build per CoS). Falls back to `/local-rpc` → loopback (`8546` mainnet / `8545` testnet) when public RPC is offline.
- `/join/` covers public mainnet first (seed `34.27.30.115:28546`, RPC `38546` read+write) and research testnet second.
- `/status/` is public mainnet `getinfo` via `/api/rpc` (Worker `PUBLIC_RPC_HTTP` → `http://34.27.30.115:38546/rpc`). Height from live getinfo only. Live strip shows `network`, `network_id`, `height`, `peers`, `privacy_claim`, supply fields.
- `/privacy/` states the live claim `opening_not_zk` (SHA3-512 opening). No public mint/spend mockup forms — operators use `/local/` next to a node.
- `/api/info` (and `/api/`) returns JSON from live `getinfo` + `monetary_info` (includes `privacy_claim`). `price_usd` is always null — no invented ticker. `/api/capabilities` probes launch RPCs and copies `network_id` from live `getinfo` (fail closed if RPC offline).
- `/embed/` is an iframe widget (themes `modern|compact|bare`) fed by `/api/info`. Says price unavailable when the node has no price RPC. Shows `network_id` only when `/api/info` returns it.
- `/whitepaper/` loads live `getinfo` + `monetary_info` into economics panels (fail closed → `RPC offline`). No canned peers/height/supply snapshot.
- Homepage live strip + latest blocks/txs come only from public read RPCs. Height `0` is displayed as `0`. Empty block lists stay empty (no demo rows).
- PWA: `manifest.webmanifest`, `sw.js`, theme `#0b1220` (dark desk), icons 192/512 for Add to Home Screen.
- Brand: `logo-transparent.png`, `favicon.ico`, `apple-touch-icon.png`, `og.png`. Dark teal-slate desk — near-white body text on dark panels. Product brand is **ADDITION** only.
- `/download/` desktop helper for mainnet / local (`addition-wallet-mainnet`, loopback `8546` only). `/local/` operator toolbox (loopback).
- Homepage hero: one muted autoplay loop `playsinline` `webkit-playsinline` `<video class="hero-banner">` with `preload="metadata"` + `poster="/og.png"` (no controls) at `https://additionblockchain.com/banners/addition-banner-2.mp4`. `chrome.js` kicks `.hero-banner` with `muted=true` + `play().catch(()=>{})`. On ≤840px, `.hero-banner` uses `max-height: 56vw` + `object-fit: contain`. **Not** the old stinger clip.
- Site footer (`chrome.js`): text + links only — **no footer video** (banner-2 is hero-only; no duplicate placement).
- Live strip: shared `AdditionSite.renderLiveStrip` (home / compare / status / join / privacy). Height `0` / peers `0` are shown as real zeros.
- Perf polish: compressed brand PNGs; early font preconnect on primary desks; `:focus-visible` outlines; compare sticky axis column; `_headers` cache for css/js/png.
- Explorer calls `/api/rpc?cmd=…` for public reads. No consensus / economy code changes in this surface. Height, TPS, and price come from live RPC fields only.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
<!-- deploy-trigger: mainnet-public-product -->
