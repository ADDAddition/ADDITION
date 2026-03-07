# Addition (ADD) White Paper

Version: 1.0 (Mainnet Operations Edition)  
Date: 2026-03-03

---

## Abstract

Addition (ADD) is a post-quantum-oriented Layer-1 blockchain stack designed for practical operations: deterministic node behavior, strict cryptographic startup checks, capped monetary policy, mining + staking runtime, token/NFT primitives, swap routing, privacy extension hooks, and decentralized peer synchronization.

This white paper describes the implemented architecture in the current codebase, operational assumptions, command surface, and launch model for self-hosted and internet deployment.

---

## 1. Design Goals

1. **Security-first baseline**
   - Strict post-quantum signature mode in node runtime.
   - Startup cryptographic self-test required.
   - Signed message and signed swap execution flows.

2. **Operational clarity**
   - One daemon executable (`additiond`) with line-based RPC.
   - Deterministic command interface for automation.
   - Healthcheck and runbook scripts for repeatable launch.

3. **Economic boundedness**
   - Enforced hard cap: **50,000,000 ADD**.
   - Exposed monetary telemetry (`monetary_info`).

4. **Composable utility**
   - Native token and NFT engines.
   - Swap pool + best-route execution.
   - Bridge and privacy extension modules.

5. **Deployment pragmatism**
   - Local daemon + API backend + portal architecture.
   - Docker/GitHub/Cloudflare deployment folders included.

---

## 2. System Overview

### 2.1 Node runtime

Primary executable:
- `build/additiond.exe`

Core modules (current repository):
- Chain state and block validation
- Mempool transaction staging
- Miner block production
- Staking state + rewards accounting
- Contract runtime
- Token/NFT runtime
- Swap and route engine
- Privacy pool and external proof verification hooks
- Bridge attestation runtime
- Peer network + decentralized message relay

### 2.2 RPC and network interfaces

- Local RPC: `127.0.0.1:8545`
- LAN RPC: `0.0.0.0:18545`
- P2P transport RPC: `0.0.0.0:28545`

Transport model:
- 1 TCP request line = 1 command
- 1 response line returned

---

## 3. Cryptography and Security Model

### 3.1 Post-quantum mode

Runtime exposes strict PQ marker through `getinfo`:
- `pq_mode=strict`

Startup/health controls:
- `crypto_selftest` must return `ok:selftest: ok`

### 3.2 Signature model

- Wallet creation returns algorithm metadata (`algo=ml-dsa-87`).
- Transaction and message signatures are validated in strict format flows.
- Signed swap path verifies payload + trader signature + deadline.

### 3.3 Network hardening

Implemented protections include:
- Handshake and identity checks in decentralized node flow
- Replay controls and message deduplication
- Per-peer rate limits
- Line-size bounds and socket timeouts
- Consensus vote and quorum command support

---

## 4. Consensus and Block Production

### 4.1 Mining

Mining command:
- `mine <reward_address>`

If no address is given:
- default reward address is `miner1`

Mining is available from:
- daemon interactive console
- local RPC command submission
- wallet/automation wrappers

Current implementation note:
- Mining uses a memory-hard PoW path in consensus (`memory_hard_head64`).
- A dedicated Proof of Useful Work (PoUW) consensus mode is not yet implemented in the current runtime.

### 4.2 Staking

Staking commands:
- `stake <address> <amount>`
- `unstake <address> <amount>`
- `staked <address>`
- `stake_reward <amount>`
- `stake_claim <address>`

Staking accounting is persisted and queryable through runtime commands.

---

## 5. Monetary Policy

Monetary telemetry command:
- `monetary_info`

Key properties:
- `max_supply=50000000`
- Emitted and remaining supplies reported
- Next reward and halving height exposed

This enables deterministic supply monitoring for operators and exchange integration.

---

## 6. Transaction Lifecycle

1. Wallet keys created (`createwallet`)
2. Sender balance checked (`getbalance`)
3. Transaction built + signed via secure flow (`tx_build` + `sign_message` + `sendtx_signed[_hash]`)
4. Mempool admission and gossip relay
5. Block inclusion via mining
6. Status tracking (`tx_status`)

Dynamic fee guidance:
- `fee_info` returns recommended fee based on mempool pressure and last block fees.

---

## 7. Token, NFT, and Swap Runtime

### 7.1 Tokens

Capabilities include:
- token creation (`token_create`, `token_create_ex`)
- mint/transfer/burn
- policy controls (fees, treasury, pause)
- blacklist and fee-exempt lists
- max tx / max wallet limits

### 7.2 NFTs

Native NFT primitives:
- `nft_mint`
- `nft_transfer`
- `nft_owner`

### 7.3 Swap engine

Supported flows:
- pool creation and liquidity management
- direct quote and exact-in swap
- best-route discovery and execution
- signed best-route execution with deadline

This provides deterministic on-chain trading primitives for ecosystem assets.

---

## 8. Privacy and Bridge Extensions

### 8.1 Privacy

Privacy commands include:
- `privacy_mint_zk`, `privacy_spend_zk`

Verification is native in-process (ML-DSA-87), without external wrapper dependency.

Strict operations use native proof verification.

### 8.2 Bridge

Bridge module supports:
- chain registration
- lock/mint/burn/release flows
- attestation-aware operations
- per-chain balances

---

## 9. State Persistence and Recovery

Node state files are stored under `./data`.

Typical persisted domains:
- blocks
- mempool
- staking
- contracts
- tokens
- bridge
- peers and node identity
- privacy

Operational scripts include backup/sanitization steps to reduce startup failures from malformed state files.

---

## 10. Operations and Deployment

### 10.1 Local/hosted stack

Recommended production split:
- **Node + backend API** on VPS/Docker host
- **Portal frontend** on Cloudflare static assets/worker edge

### 10.2 Included deployment folders

- `deploy/github-docker/`
  - Dockerfile / compose / GitHub workflow
- `deploy/cloudflare/`
  - wrangler config / worker / deploy script

### 10.3 Main operator docs

- `docs/MAINNET_RUNBOOK.md`
- `docs/FINAL_COMMANDS.md`
- `scripts/start_mainnet.ps1`
- `scripts/healthcheck.ps1`

---

## 11. Threat Model (Practical)

In-scope mitigations in current architecture:
- malformed input rejection for command handlers
- strict fee checks and signature checks
- transport timeouts and bounded line sizes
- peer-level controls and dedup/rate limits
- crypto selftest for boot integrity checks

Operational risks still requiring disciplined ops:
- key custody and host hardening
- secure secrets management for deployment credentials
- backup/restore drill quality
- bootstrap peer trust and network policy

---

## 12. Governance and Upgrade Approach

Current model is operator-driven deployment with controlled releases.

Recommended process:
1. Build and test in staging
2. Run crypto self-test and health checks
3. Roll in phases (single node, then peers)
4. Monitor `getinfo`, `monetary_info`, peer/sync status
5. Keep rollback snapshots of `./data`

---

## 13. Performance and Metrics

Exposed/derived runtime metrics:
- block height
- mempool size
- peers
- current reward and fees-last-block
- emitted/remaining supply
- estimated dashboard KPIs via portal backend

These metrics are available through RPC and portal API for operator dashboards.

---

## 14. Limitations and Roadmap

Current practical limitations:
- local line-based RPC (not full authenticated JSON-RPC gateway)
- privacy ZK flow depends on external verifier integration quality
- internet launch quality depends on infra setup (DNS, firewall, secrets)

Roadmap priorities:
1. stronger authenticated remote RPC gateway
2. richer observability and alerting
3. hardened restart supervisor/service configuration
4. expanded formal test coverage for edge-case state recovery

---

## 15. Conclusion

Addition is positioned as a security-oriented, operator-practical blockchain runtime with strict PQ posture, bounded supply, native utility modules, and clear deployment structure. The stack is suitable for controlled mainnet operations when run with disciplined infrastructure practices and monitored health workflows.

---

## Appendix A — Core Operational Commands

- `getinfo`
- `monetary_info`
- `crypto_selftest`
- `createwallet`
- `mine <address>`
- `getbalance <address>`
- `tx_build <...>`
- `sendtx_signed_hash <...>`
- `tx_status <hash>`
- `peers`
- `sync`

See full list: `docs/FINAL_COMMANDS.md`.
