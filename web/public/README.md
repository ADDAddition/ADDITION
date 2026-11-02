# ADDITION static site

Pages root for additionblockchain.com — mobile-first product chrome + mainnet explorer + installable PWA.

**Pre-mainnet.** ADDITION mainnet has not launched. The site only describes what the code does today and marks the network as pre-mainnet/testnet.

Primary nav (desktop + mobile bottom tabs): **Explore · Wallet · Get started · Status**, with a **PRE-MAINNET** badge. More menu: Specs & compare, Privacy, Whitepaper, Public RPC, Build & download, Docs.

- `/` is the product homepage (what ADDITION is) plus explorer search / latest blocks / latest txs, with a compare teaser. Dedicated views: `/block/`, `/tx/`, `/address/`. Pretty URLs are folders with `index.html`.
- `/compare/` (alias `/benchmark/` → `/compare/`) is a live-only table: ADDITION vs Bitcoin, Ethereum, Solana, Monero on shipped axes. No ADDITION TPS race, no invented height/price, no ZK-privacy claim.
- `/wallet/` talks only to the visitor's own node via `/local-rpc` → loopback (`8545` testnet / `8546` mainnet profile). The public RPC is read-only.
- `/join/` covers running a research-testnet node over Tor and the mainnet seed-file mechanism (no seeds published yet).
- `/status/` shows launch readiness plus live read-only `getinfo` via `/api/rpc` (Worker secret `PUBLIC_RPC_HTTP`). Fields come from the node only.
- `/privacy/` states the live claim `opening_not_zk` (SHA3-512 opening) and separates it from the Rust shielded pool.
- `/api/info` (and `/api/`) returns JSON from live `getinfo` + `monetary_info` (includes `privacy_claim`). `price_usd` is always null — no invented ticker. `/api/capabilities` reports `public_write: false`, probes RPCs and copies `network_id` from live `getinfo` (fail closed if RPC offline).
- `/whitepaper/` loads live `getinfo` + `monetary_info` into economics panels (fail closed → `RPC offline`). No canned peers/height/supply snapshot.
- Homepage live strip + latest blocks/txs come only from public read RPCs. Height `0` is displayed as `0`. Empty block lists stay empty (no demo rows).
- PWA: `manifest.webmanifest`, `sw.js`, theme `#0b1220` (dark desk), icons 192/512 for Add to Home Screen.
- Brand: `logo-transparent.png`, `favicon.ico`, `apple-touch-icon.png`, `og.png`. Dark teal-slate desk — near-white body text on dark panels. Product brand is **ADDITION** only.
- `/download/` explains how to build from source. No prebuilt or signed binaries are published yet.
- Homepage hero: one muted autoplay loop `playsinline` `webkit-playsinline` `<video class="hero-banner">` with `preload="metadata"` + `poster="/og.png"` (no controls) at `https://additionblockchain.com/banners/addition-banner-2.mp4`. `chrome.js` kicks `.hero-banner` with `muted=true` + `play().catch(()=>{})`. On ≤840px, `.hero-banner` uses `max-height: 56vw` + `object-fit: contain`. **Not** the old stinger clip.
- Site footer (`chrome.js`): text + links only — **no footer video** (banner-2 is hero-only; no duplicate placement).
- Live strip: shared `AdditionSite.renderLiveStrip` (home / compare / status / join / privacy). Height `0` / peers `0` are shown as real zeros.
- Perf polish: compressed brand PNGs; early font preconnect on primary desks; `:focus-visible` outlines; compare sticky axis column; `_headers` cache for css/js/png.
- Explorer calls `/api/rpc?cmd=…` for public reads. No consensus / economy code changes in this surface. Height, TPS, and price come from live RPC fields only.
- Contact: contact@additionblockchain.com
- Source: https://github.com/ADDAddition/ADDITION
<!-- deploy-trigger: mainnet-public-product -->
