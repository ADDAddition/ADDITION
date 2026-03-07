# Addition (ADD) Mainnet Runbook

## 1) Pre-flight checklist
- Build must succeed:
  - `cmake -S . -B build`
  - `cmake --build build`
- `additiond.exe` exists in `build/`
- liboqs/OpenSSL available on host
- Firewall rules configured for:
  - TCP 18545 (LAN RPC, optional public)
  - TCP 28545 (P2P transport)
- Data backup path ready (`./data` snapshot)

## 2) Security baseline
- `node_identity.dat` now stores only `PUB|...` (no private key persisted).
- Set secure runtime vars before launch:
  - `ADDITION_MAINNET_MODE=1`
  - `ADDITION_RPC_TOKEN=<strong_token>`
  - `ADDITION_STRICT_ADMIN_MODE=1`
  - `ADDITION_PRIVACY_MASTER_KEY=<strong_secret_min_32_chars>`
- If enabling LAN RPC, also set:
  - `ADDITION_ENABLE_LAN_RPC=1`
  - `ADDITION_LAN_RPC_TOKEN=<strong_token>`
- Keep `ADDITION_ALLOW_INSECURE_TX_COMMANDS` unset (or `0`) in production.
- Privacy verifier is native in-process (ML-DSA-87); external wrappers are disabled.

## 3) First startup
- Launch daemon:
  - `build\additiond.exe`
- Verify startup self-test line contains:
  - `selftest: ok`
- Verify listeners:
  - `local RPC listening on 127.0.0.1:8545`
  - `P2P RPC listening on 0.0.0.0:28545`

## 4) Health checks
- `getinfo`
- `monetary_info`
- `crypto_selftest`
- `peers`

Expected:
- `pq_mode=strict`
- `max_supply=50000000`
- `crypto_selftest` returns `ok:selftest: ok`

## 5) Network bootstrap
- Add known bootstrap peers:
  - `addpeer <ip:port>`
- Trigger sync:
  - `sync`
- Confirm chain progress:
  - `getinfo`

## 6) Wallet and transaction sanity
- Create wallet:
  - `createwallet`
- Mine one block to bootstrap address:
  - `mine <address>`
- Build/sign/send tx (secure flow):
  - `tx_build <from> <pub> <to> <amount> <fee> <nonce>`
  - `sign_message <priv> <sign_hash_hex_utf8>`
  - `sendtx_signed_hash <from> <pub> <to> <amount> <fee> <nonce> <sig_hex_without_pq_prefix>`
- Track status:
  - `tx_status <tx_hash>`
- Instant receive check:
  - `getbalance_instant <to_address>`

## 7) Privacy strict sanity
- Confirm native verifier mode:
  - `privacy_native_verifier pq_mldsa87`
- Submit ZK mint/spend only (avoid non-ZK in strict operations):
  - `privacy_mint_zk ...`
  - `privacy_spend_zk ...`

Important:
- In mainnet mode (`ADDITION_MAINNET_MODE=1`), daemon startup is blocked if `ADDITION_PRIVACY_MASTER_KEY` is missing or shorter than 32 chars.
- Keep `ADDITION_PRIVACY_MASTER_KEY` stable across restarts, otherwise previously sealed private notes cannot be unsealed.

## 8) Portal + metrics
- Start portal backend:
  - `python web/portal/addition_portal_backend.py`
- Open portal:
  - `web/portal/index.html`
- Check `/api/health`, `/api/dashboard`

## 9) Mainnet go/no-go
Go only if all true:
- crypto self-test pass at boot and via command
- peers connected and sync stable
- tx submit + mined path working
- monetary cap telemetry sane
- no repeated P2P ban storms

## 10) Rollback protocol
- Stop daemon cleanly (`quit`)
- Snapshot `./data` to timestamped backup
- Restore last known-good `./data` if anomaly detected
- Relaunch and verify:
  - `getinfo`
  - `monetary_info`
  - `crypto_selftest`

## 11) Operational cadence
- Every hour:
  - `getinfo`, `peers`, `monetary_info`
- Every day:
  - backup `./data`
  - verify portal health endpoint
- Every release:
  - rebuild, self-test, staged restart, post-checks
