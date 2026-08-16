# Addition (ADD) experimental profile runbook

> This repository ships a research **testnet**. There is no live public mainnet.
> Default launch is `additiond --network testnet`. Do not set `ADDITION_MAINNET_MODE=1` unless you are explicitly testing the mainnet *profile*.

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
- Keep `ADDITION_ALLOW_INSECURE_TX_COMMANDS` unset (or `0`) on any networked host.
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
- Create a named local wallet (ML-DSA-87, secret written to `data/wallets/<name>.wal`, not printed):
  - `createwallet [name]`
  - `wallet_info <name>` / `wallet_balance <name>` / `getbalance <address>`
- Mine one block to that address (local RPC only; memory-hard):
  - `mine <address>`
- Default send (no privkey on the wire):
  - `wallet_send <name> <to> <amount> [fee]`
- Explicit send (still no privkey if you use `wallet_sign`):
  - `tx_build <from> <pub> <to> <amount> <fee> <nonce>`
  - `wallet_sign <name> <sign_hash_hex_utf8>`
  - `sendtx_signed_hash <from> <pub> <to> <amount> <fee> <nonce> <sig_hex_without_pq_prefix>`
- GUI / page: `python3 web/addition_wallet_gui.py` or `/wallet/` (loopback only)
- Standalone CLI (keys on the caller disk): see [WALLET.md](WALLET.md)
  - `python3 web/addition_wallet.py createwallet`
  - `python3 web/addition_wallet.py send <to_address> <amount>`
- Track status:
  - `tx_status <tx_hash>`
- Instant receive check:
  - `getbalance_instant <to_address>`

## 7) Privacy (SHA3 opening)
- Confirm `getinfo` reports `privacy_mode=sha3_opening`, `privacy_ok=true`, `privacy_verifier=sha3_opening`
- Use the opening path:
  - `privacy_note_prepare <amount>`
  - `privacy_mint_open ...`
  - `privacy_spend_open ...`
- `privacy_mint_zk` / `privacy_spend_zk` are ML-DSA wraps, not a circuit. Garbage proofs error.

Important:
- In mainnet mode (`ADDITION_MAINNET_MODE=1`), daemon startup is blocked if `ADDITION_PRIVACY_MASTER_KEY` is missing or shorter than 32 chars.
- Keep `ADDITION_PRIVACY_MASTER_KEY` stable across restarts, otherwise previously sealed private notes cannot be unsealed.

## 8) Site + wallet (what exists)
- Static site: `python3 web/serve.py` → `http://127.0.0.1:8080`
- Local wallet page: `/wallet/` (loopback `/local-rpc` only)
- Desktop/CLI helper: `python3 web/addition_wallet_gui.py` (`--cli` without a display)
- `web/portal/` is not in this tree. Use `/status/` and `/explorer/` instead.

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
