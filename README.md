<p align="center">
  <img src="web/portal/assets/logo-transparent.png" alt="Addition Logo" width="250">
</p>

<h1 align="center">Addition (ADD) / ADD Privacy</h1>

<p align="center">
  <img src="https://img.shields.io/badge/Status-Mainnet--Ready-brightgreen?style=for-the-badge" alt="Status">
  <img src="https://img.shields.io/badge/Security-Quantum--Resistant-blue?style=for-the-badge" alt="Security">
  <img src="https://img.shields.io/badge/Privacy-Enabled-blueviolet?style=for-the-badge" alt="Privacy">
  <img src="https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge" alt="License">
</p>

---

<p align="center">
  <b>Professional quantum-resistant blockchain stack with mining, staking, privacy runtime, smart contracts, and hardened P2P.</b>
</p>

<div align="center">
  <h3>Project Showcase</h3>
  <video src="web/portal/assets/promo.mp4" width="800" controls muted autoplay loop>
    Your browser does not support the video tag.
  </video>
</div>

---
## Core Highlights
- Quantum-resistant signatures (ML-DSA-87, strict mode)
- Hard monetary cap: 50,000,000 ADD
- Mining + staking runtime
- Privacy pool primitives
- Smart contracts, token and NFT runtime
- Signed P2P handshake, anti-replay, relay dedup, rate limits, transport hardening

## Build
```bash
cmake -S . -B build
cmake --build build
```

## Run daemon
```bash
./build/additiond
```

> Windows users: `launch_daemon.bat` opens a console with a black background
> and red "ADDITION" text (`color 0C`), just like the custom shell from before.


> When running the daemon from a terminal (or from `launch_daemon.bat` on
> Windows) you can type `mine` or `mine <address>` at the prompt; each successful
> mining operation is echoed to the console:
> 
> ```
> [rpc] mined block 1 reward=miner1
> ```
> 
> RPC clients such as the wallet now trigger the same behaviour and will also
> receive a response starting with `mined height=`.


## Professional Frontends
- Desktop wallet (enhanced): `web/addition_wallet_pro.py`
- Portal website (advanced): `web/portal/index.html`
- Portal backend (live metrics bridge): `web/portal/addition_portal_backend.py`

### Wallet Pro one-click install / launch (Windows)
- Auto-install Python + deps if missing:
	- `powershell -ExecutionPolicy Bypass -File web/install_wallet_pro.ps1`
- One-click launch (auto-installs Python if needed):
	- `web/launch_wallet_pro.bat`
	- or `powershell -ExecutionPolicy Bypass -File web/launch_wallet_pro.ps1`

### Wallet Pro one-click install / launch (macOS)
- Auto-install Python + deps if missing:
	- `bash web/install_wallet_pro_macos.sh`
- One-click launch:
	- `bash web/launch_wallet_pro_macos.sh`

Wallet Pro now includes built-in:
- Mining controls
- Staking controls (`stake`, `unstake`, `stake_claim`, `staked`)
- Swap controls (`swap_quote`, `swap_exact_in`)
- Signed best-route swap flow

## Mainnet Operations
- Mainnet runbook: `docs/MAINNET_RUNBOOK.md`
- Full white paper: `docs/WHITE_PAPER.md`
- Start script: `scripts/start_mainnet.ps1`
- Automated healthcheck: `scripts/healthcheck.ps1`

## Online Deployment Folders (Clear Structure)

### 1) GitHub + Docker (node + backend online)
Use folder: `deploy/github-docker`

- `deploy/github-docker/Dockerfile`
- `deploy/github-docker/docker-compose.yml`
- `deploy/github-docker/run-local.ps1`
- `.github/workflows/docker-image.yml`

Quick launch (Windows):
```powershell
powershell -ExecutionPolicy Bypass -File deploy/github-docker/run-local.ps1
```

### 2) Cloudflare (portal frontend online)
Use folder: `deploy/cloudflare`

- `deploy/cloudflare/wrangler.toml`
- `deploy/cloudflare/worker.js`
- `deploy/cloudflare/deploy_cloudflare.ps1`

Deploy:
```powershell
npx wrangler login
powershell -ExecutionPolicy Bypass -File deploy/cloudflare/deploy_cloudflare.ps1
```

### Production architecture recommended
- Docker/VPS: `additiond` + portal backend API
- Cloudflare: static portal frontend + CDN + DNS

Run portal backend:
```bash
python web/portal/addition_portal_backend.py
```

## Signed Best-Route Swap (Wallet Pro)

- Open `web/addition_wallet_pro.py` and go to **Ops**.
- In **Signed best-route swap**, enter:
	`token_in token_out trader amount_in min_out deadline_unix max_hops`
- Click **Build Sign Payload**.
- Click **Sign Payload** (uses local wallet private key via `sign_message`).
- Click **Submit Signed Swap**.

Security notes:
- Signed flow verifies PQ signature and deadline on node side.
- Signature field stores hex without `pq=` prefix (handled automatically).

## Multi-Platform Client App (Windows/macOS/Web/Mobile)

Flutter client is scaffolded in `client/addition_app`.

### Prerequisites
- Flutter installed (using Puro in this workspace)
- Portal backend running: `python web/portal/addition_portal_backend.py`

### Run (from repo root)
- Windows desktop:
	- `cd client/addition_app`
	- `flutter run -d windows`
- Web:
	- `cd client/addition_app`
	- `flutter run -d chrome`

### Build artifacts
- Windows:
	- `cd client/addition_app`
	- `flutter build windows`
	- Output: `client/addition_app/build/windows/x64/runner/Release/`
- Web:
	- `cd client/addition_app`
	- `flutter build web`
	- Output: `client/addition_app/build/web/`

### Mobile identity (configured)
- Android applicationId: `com.addition.wallet`
- iOS display name: `Addition Wallet`
- macOS bundle id: `com.addition.wallet`
- Windows product name/binary: `Addition Wallet` / `addition_wallet.exe`

### Flutter client signed swap UI
- In `client/addition_app` UI, you now have:
	- Best route quote
	- Sign payload retrieval
	- Signed best-route execution
- Required fields:
	- Trader address
	- Trader public key
	- Trader signature (hex without `pq=`)

Notes:
- App entry is `client/addition_app/lib/main.dart`.
- API base defaults to `http://127.0.0.1:8080` and can be changed in UI.
Then open `web/portal/index.html` in a browser.

