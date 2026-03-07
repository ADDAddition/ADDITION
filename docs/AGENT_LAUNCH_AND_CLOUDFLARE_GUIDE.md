# Agent Launch + Cloudflare Hosting Guide

## 1) Launch full network in PowerShell (hardened)

From repository root:

```powershell
Set-Location "C:\Users\admin\Desktop\ADDITION_FINAL"

# Load persistent privacy key from user env
$env:ADDITION_PRIVACY_MASTER_KEY = [Environment]::GetEnvironmentVariable("ADDITION_PRIVACY_MASTER_KEY","User")
if (-not $env:ADDITION_PRIVACY_MASTER_KEY -or $env:ADDITION_PRIVACY_MASTER_KEY.Length -lt 32) {
  throw "ADDITION_PRIVACY_MASTER_KEY missing or shorter than 32 chars"
}

# Recommended security tokens
if (-not $env:ADDITION_RPC_TOKEN) {
  $env:ADDITION_RPC_TOKEN = [Convert]::ToBase64String((1..24 | ForEach-Object { Get-Random -Maximum 256 }))
}
if (-not $env:ADDITION_LAN_RPC_TOKEN) {
  $env:ADDITION_LAN_RPC_TOKEN = [Convert]::ToBase64String((1..24 | ForEach-Object { Get-Random -Maximum 256 }))
}

# Mainnet + network interfaces
$env:ADDITION_MAINNET_MODE = "1"
$env:ADDITION_ENABLE_LAN_RPC = "1"
$env:ADDITION_ENABLE_P2P_RPC = "1"

# Launch
.\scripts\start_mainnet.ps1
```

Expected healthy outputs:
- `selftest: ok`
- `local RPC listening on 127.0.0.1:8545`
- `LAN RPC listening on 0.0.0.0:18545`
- `P2P RPC listening on 0.0.0.0:28545`
- `mainnet mode enabled (ADDITION_MAINNET_MODE=1)`

---

## 2) Persist required env vars (recommended)

```powershell
setx ADDITION_PRIVACY_MASTER_KEY "<YOUR_64_CHAR_SECRET>"
setx ADDITION_RPC_TOKEN "<YOUR_LOCAL_RPC_TOKEN>"
setx ADDITION_LAN_RPC_TOKEN "<YOUR_LAN_RPC_TOKEN>"
```

Open a new terminal after `setx`.

---

## 3) Start portal backend locally

```powershell
Set-Location "C:\Users\admin\Desktop\ADDITION_FINAL\web\portal"
python .\addition_portal_backend.py
```

Default endpoint:
- `http://127.0.0.1:8080`

Health test:
- `http://127.0.0.1:8080/api/health`

---

## 4) Deploy frontend to Cloudflare Workers + Assets

### Prerequisites
- Node.js installed
- Wrangler auth done

```powershell
npx wrangler login
```

### Deploy command

```powershell
Set-Location "C:\Users\admin\Desktop\ADDITION_FINAL"
powershell -ExecutionPolicy Bypass -File .\deploy\cloudflare\deploy_cloudflare.ps1
```

Uses:
- `deploy/cloudflare/wrangler.toml`
- `deploy/cloudflare/worker.js`
- asset directory: `web/portal`

---

## 5) Cloudflare domain routing notes

Configured route examples are in:
- `deploy/cloudflare/wrangler.toml`
- `deploy/cloudflare/wrangler_multiverse.toml`

Frontend can run on domains like:
- `https://xa1.ai`

Backend API should run separately (VPS / container / tunnel), e.g.:
- `https://api.xa1.ai`

---

## 6) Link frontend to backend API

Current frontend JS default:
- `LOCAL_API = 'http://127.0.0.1:8080'`

For production behind Cloudflare, point API calls to your public backend domain.

---

## 7) Operational checklist (GO/NO-GO)

- [ ] Mainnet daemon starts without fatal errors
- [ ] `selftest: ok`
- [ ] RPC local auth token set
- [ ] LAN RPC token set (if LAN enabled)
- [ ] Privacy key loaded and length >= 32
- [ ] Portal backend `/api/health` returns OK
- [ ] Cloudflare deploy completed successfully
- [ ] Public site loads and metrics update in real time
