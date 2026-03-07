# Cloudflare Backend Publish Guide (api.xa1.ai)

This publishes your local portal backend (`web/portal/addition_portal_backend.py`) behind Cloudflare Tunnel, then connects the frontend hosted on Cloudflare Workers.

## 1) Start backend locally

```powershell
Set-Location "C:\Users\admin\Desktop\ADDITION_FINAL\web\portal"
python .\addition_portal_backend.py
```

Check local health:
- `http://127.0.0.1:8080/api/health`

---

## 2) Install cloudflared (Windows)

Use official Cloudflare install method, then verify:

```powershell
cloudflared --version
```

---

## 3) Authenticate and create tunnel

```powershell
cloudflared tunnel login
cloudflared tunnel create addition-api
```

This creates a tunnel ID + credentials JSON in `%USERPROFILE%\.cloudflared\`.

---

## 4) Configure tunnel ingress

Use template:
- `deploy/cloudflare/cloudflared-config.example.yml`

Create:
- `%USERPROFILE%\.cloudflared\config.yml`

Set values:
- `tunnel: <YOUR_TUNNEL_ID>`
- `credentials-file: C:\Users\<YOU>\.cloudflared\<YOUR_TUNNEL_ID>.json`
- hostname should be your API domain (example: `api.xa1.ai`)

---

## 5) Create DNS route in Cloudflare

```powershell
cloudflared tunnel route dns addition-api api.xa1.ai
cloudflared tunnel route dns addition-api api.dogecointoday.com
```

---

## 6) Run tunnel

```powershell
cloudflared tunnel run addition-api
```

If successful, public health endpoint should work:
- `https://api.xa1.ai/api/health`

---

## 7) Connect frontend to backend

Portal frontend now supports dynamic API base:
- Query override: `?api=https://api.xa1.ai`
- Stored override in `localStorage.PORTAL_API_BASE`
- Auto fallback for public domains to `https://api.xa1.ai`

Examples:
- `https://bitcoins.express/?api=https://api.xa1.ai`
- `https://xa1.ai/?api=https://api.xa1.ai`
- `https://dogecointoday.com/?api=https://api.dogecointoday.com`

Note:
- Frontend auto-routes `dogecointoday.com` to `https://api.dogecointoday.com` by default.

---

## 8) Optional hardening

For mainnet operations terminal:

```powershell
$env:ADDITION_RPC_TOKEN = "<STRONG_LOCAL_RPC_TOKEN>"
$env:ADDITION_LAN_RPC_TOKEN = "<STRONG_LAN_RPC_TOKEN>"
$env:ADDITION_NODE_PRIVKEY = "<STABLE_NODE_PRIVKEY_HEX>"
```

And keep `ADDITION_PRIVACY_MASTER_KEY` stable and secret.

---

## 9) Quick diagnostics

1. Frontend loads but metrics fail:
   - open browser devtools network tab
   - verify API target domain and CORS response

2. API health fails publicly:
   - ensure cloudflared tunnel is running
   - check DNS route (`api.xa1.ai`) in Cloudflare
   - test local backend at `127.0.0.1:8080`

3. Daemon not reachable by backend:
   - verify daemon up on `127.0.0.1:8545`
   - test local endpoint `http://127.0.0.1:8080/api/health`
