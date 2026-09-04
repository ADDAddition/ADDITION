# ADDITION Technical Whitepaper (Complete)

**Brand:** ADDITION (never “SmartChain”)  
**Network (public product):** `ADDITION_MAINNET_V1` / `addition-mainnet`  
**Research chain:** `ADDITION_TESTNET_V1` / `addition-testnet`  
**Fast-path scaffold (not live product):** `ADDITION_FAST_V1` — fail-closed; see [`docs/FAST_PATH_V1.md`](FAST_PATH_V1.md)  
**Canonical source:** [github.com/ADDAddition/ADDITION](https://github.com/ADDAddition/ADDITION)  
**Contact:** [contact@additionblockchain.com](mailto:contact@additionblockchain.com)  
**Status:** Engineering description of the C++20 node, configs, and live public RPC as of this document. Values that can change (height, peers, fees, TPS telemetry) come only from live `getinfo` / related RPCs — do not invent figures.

This document supersedes the short sketch in `docs/WHITE_PAPER.md` and the thin site page previously at `/whitepaper/`.

---

## Vision roadmap vs Live product

| Topic | Live public product | Vision / scaffold / research |
| --- | --- | --- |
| Network | `ADDITION_MAINNET_V1`, `memory_hard` PoW | `ADDITION_FAST_V1` scaffold ([`FAST_PATH_V1.md`](FAST_PATH_V1.md)) — not the public product |
| Height / peers / fees | Copy live `getinfo` only (height may be `0`) | No invented height, peer counts, or USD ticker |
| Privacy | `privacy_claim=opening_not_zk` (SHA3-512 opening; node sees trapdoor) | ZK path fail-closed / `zk_pending` ([`PRIVACY_REAL_V1.md`](PRIVACY_REAL_V1.md)); circuit schema in progress ([`ZK_CIRCUIT_V1.md`](ZK_CIRCUIT_V1.md)) — never fake ZK |
| Throughput | `last_tps` = local mine telemetry when present; `throughput_claim=none` | `research_goal_tps` with `research_goal_is_not_a_measurement=true` — not a Solana TPS claim |
| PQ crypto | **ML-DSA-87** + **SHA3-512**, `pq_mode=strict` (kept) | Same stack on scaffolds; not “unbreakable forever” |
| Economic security | `economic_security=none` until real | Staking side map remains non-consensus |

### What ships today

- Public product **`ADDITION_MAINNET_V1`**: UTXO L1, P2P seed, public RPC, wallet spend path with **ML-DSA-87** / **SHA3-512** / `pq_mode=strict`
- Mainnet PoW **`memory_hard`** at target floor **`0x000000FFFFFFFFFF`**
- Privacy opening path: `privacy_mint_open` / `privacy_spend_open` labeled **`opening_not_zk`**
- Height and monetary fields from **live `getinfo` / `monetary_info` only**

### What is scaffold

- Real ZK privacy path: `privacy_*_zk_v1` + `FailClosedPrivacyZkVerifier` — always rejects; labels `zk_pending` until a verifier can succeed ([`PRIVACY_REAL_V1.md`](PRIVACY_REAL_V1.md), vision #77)
- Circuit slice: typed witness/public-input schema + fail-closed prover (`zk_circuit_status=not_proven`) — **not live ZK** ([`ZK_CIRCUIT_V1.md`](ZK_CIRCUIT_V1.md))
- Fast path network profile **`ADDITION_FAST_V1`**: config/genesis/CLI stubs; **`--fast` / `--network fast` refuse to boot** until the pipeline ships ([`FAST_PATH_V1.md`](FAST_PATH_V1.md), vision #78 / SHA `6d3f483`+)
- Legacy `privacy_mint_zk` / `privacy_spend_zk`: ML-DSA wrap of a string — still not a circuit

### What is research goal

- Measured high throughput on a separate fast profile (leader / pipeline / parallel execution) — **not** marketed as live mainnet TPS
- `research_goal_tps` remains labeled **`research_goal_is_not_a_measurement=true`**
- Future `zk_v1` only after a real proof backend verifies on-node
- `economic_security` stays **`none`** until a real security model ships

---

## Table of contents

1. [Abstract and motivation](#1-abstract-and-motivation)
2. [Architecture](#2-architecture)
3. [Cryptography](#3-cryptography)
4. [Privacy model (as shipped)](#4-privacy-model-as-shipped)
5. [Consensus and proof of work](#5-consensus-and-proof-of-work)
6. [Economics](#6-economics)
7. [Networks and ports](#7-networks-and-ports)
8. [Public RPC](#8-public-rpc)
9. [Wallet, transactions, contracts, tokens](#9-wallet-transactions-contracts-tokens)
10. [Bridges](#10-bridges)
11. [Security assumptions and threat model](#11-security-assumptions-and-threat-model)
12. [Vision vs Live (roadmap)](#12-vision-vs-live-roadmap)
13. [How to join](#13-how-to-join)
14. [Contact](#14-contact)

---

## 1. Abstract and motivation

ADDITION is a Bitcoin-like Layer 1: a single C++20 daemon (`additiond`), a UTXO ledger, peer-to-peer gossip, and text/HTTP RPC. The public product network is **ADDITION_MAINNET_V1** — a decentralized public mainnet that anyone can join by bootstrapping the published seed, syncing, mining, and using wallet RPCs (same operational model as running `bitcoind` against a known bootstrap peer).

The design priority is **post-quantum signatures at the protocol layer**. Spend transactions require **ML-DSA-87** (FIPS 204) via liboqs in `pq_mode=strict`. Hashing and PoW use **SHA3-512** (OpenSSL), with mainnet PoW wrapped in a memory-hard scratch (`memory_hard`).

Motivation in one sentence: keep a familiar UTXO / PoW L1 shape while replacing elliptic-curve signatures with a NIST-standardized lattice signature before large quantum computers become practical against ECDSA/Schnorr.

What this whitepaper does **not** claim:

- No invented peer counts, TPS, block height, or USD prices
- No Groth16 / SNARK / Bulletproofs / Monero / Zcash privacy
- No public DEX, token sale, or foreign-chain bridge product
- No “SmartChain” branding — the product name is **ADDITION** only

---

## 2. Architecture

The tree is a single canonical C++20 codebase. Major modules (from `docs/ARCHITECTURE.md` and `src/` / `include/addition/`):

| Module | Role |
| --- | --- |
| `block.*` | Block / header data model and hashing helpers |
| `chain.*` | Canonical ledger, UTXO set, block validation, retarget, coinbase |
| `mempool.*` | Pending txs; rejects unsigned / empty-input / duplicate-outpoint junk |
| `miner.*` | Block template + PoW search (`sha3_512` testnet; `memory_hard` mainnet) |
| `crypto.*` | ML-DSA-87 (liboqs), SHA3-512, memory-hard head64, address derivation |
| `rpc_server.*` | Trusted TEXT RPC command dispatch |
| `rpc_access.*` / `rpc_network_server.*` | HTTP `/rpc` and `/jsonrpc`, public vs trusted surfaces |
| `wallet.*` / `wallet_store.*` / `wallet_keys.*` | Local `.wal` files, `wallet_send`, key material |
| `p2p.*` / `decentralized_node.*` / `net_io.*` | Handshake, gossip, sync (`HELLO` / `REQBLK` / `BLKDATA`) |
| `privacy.*` | SHA3-512 commitment + nullifier opening (`opening_not_zk`); ZK scaffold `privacy_zk.*` (`zk_pending`, fail-closed) |
| `zk_circuit_v1.*` | Circuit schema + fail-closed prover (`zk_circuit_status=not_proven`); not live ZK — [`ZK_CIRCUIT_V1.md`](ZK_CIRCUIT_V1.md) |
| `contract_engine.*` | Deterministic in-process KV (`set` / `add` / `get`) — not the EVM |
| `token_engine.*` | In-process token / NFT / AMM ledger |
| `bridge.*` | In-process counters (`bridge.dat`) — not a live cross-chain bridge |
| `staking.*` | Loopback stake map; `economic_security=none` |
| `auto_mine.*` | Testnet-only timer miner; refused on mainnet |
| `btc_hygiene.*` | Offline Bitcoin script classifier + signed rehearsal receipt |
| `pouw_*` | PoUW research surfaces (storage/compute status RPCs) |
| `state_store.*` | Persistence helpers for side ledgers |
| `ai_optimizer.*` | Local fee-floor / difficulty-bias heuristics from mempool samples |

**Daemon:** `apps/additiond_main.cpp` builds `additiond`. Persistence: `--data-dir/blocks.dat` after each accepted block; UTXOs rebuild by replay. Side state (`tokens.dat`, `privacy.dat`, `staking.dat`, `bridge.dat`, …) flushes after successful writes and on shutdown.

**Layers (conceptual):**

1. **Consensus / ledger** — UTXO validation, PoW check, emission, retarget (`chain.cpp`)
2. **P2P** — network id in HELLO; mainnet peers rejected if they speak `ADDITION_TESTNET_V1`
3. **RPC** — loopback write on home nodes; public HTTP on the seed (see §8)
4. **Side engines** — privacy notes, tokens/NFT/swap, contracts KV, bridge counters — not consensus-native ADD locks unless stated

Build requires **liboqs** and **OpenSSL**. Missing liboqs fails the build.

---

## 3. Cryptography

### 3.1 Post-quantum signatures — ML-DSA-87 (FIPS 204) — kept

ADDITION’s signature path is a **real, shipped** post-quantum choice — celebrate that carefully:

| Claim | Status |
| --- | --- |
| Default scheme **ML-DSA-87** (`pq=`), FIPS 204 parameter set via liboqs | **Live** on mainnet spends |
| `pq_mode=strict`, `allowed_sig_algs=ml-dsa-87` | **Live** |
| Hashing / PoW digests / addresses / privacy openings use **SHA3-512** | **Live** |
| “Unbreakable forever” / immune to all future cryptanalysis | **Not claimed** |
| FIPS 140-3 validated cryptographic module | **Not claimed** |

- Thread-local `OQS_SIG` and parallel batch verify in `crypto.cpp`
- Opt-in **SLH-DSA** (`slh-dsa-shake-256s`) only if this liboqs build can `OQS_SIG_sign_with_ctx_str` with a non-empty context; otherwise rejected in strict mode
- Unknown schemes (including Falcon/FN-DSA) are rejected

Strict mode means the node only accepts the allowlisted PQ schemes it can verify. Lattice assumptions can age; operators should track NIST / cryptanalysis guidance over time. PQ here is a **grade and posture**, not a forever warranty.

### 3.2 Hashing — SHA3-512

SHA3-512 via OpenSSL is used for:

- Header / PoW digests (testnet direct; mainnet after memory-hard scratch)
- Address derivation
- Privacy commitments / nullifiers
- Various internal digests

### 3.3 Addresses

```text
address = sha3_512(scheme_id || 0x00 || pubkey_bytes)   # 128 hex
```

Default `scheme_id` is `ml-dsa-87`. The raw 2592-byte ML-DSA-87 public key is **not** the address. Spend paths recompute the hash from the revealed pubkey; a pubkey that does not commit to the address is rejected.

### 3.4 Self-test

`crypto_selftest` on the public seed returns a live report (example observed: `ok:selftest: ok allowed_sig_algs=ml-dsa-87`). Copy only what the node prints.

---

## 4. Privacy model (as shipped)

ADDITION privacy is a **SHA3-512 commitment + nullifier opening**. The node sees the trapdoor when minting/spending open notes.

| Field (from `getinfo`) | Value |
| --- | --- |
| `privacy_mode` | `sha3_opening` |
| `privacy_verifier` | `sha3_opening` |
| `privacy_claim` | `opening_not_zk` |
| `privacy_zk_roadmap` | `zk_pending` (scaffold; not live ZK) |
| `privacy_ok` | `true` (when the process answers) |

Relevant RPCs: `privacy_note_prepare`, `privacy_mint_open`, `privacy_spend_open`. Notes live in a **side ledger** (`privacy.dat`), not a native ADD lock into a consensus privacy pool.

**Vision vs Live:** live privacy is `opening_not_zk` (node sees the trapdoor). Real zero-knowledge is scaffold only: fail-closed RPCs `privacy_mint_zk_v1` / `privacy_spend_zk_v1` reject until a proof backend is wired (`claim=zk_pending` only; never `zk_v1` while stubbed). Circuit interface + schema: [`docs/ZK_CIRCUIT_V1.md`](ZK_CIRCUIT_V1.md) (`zk_circuit_status=not_proven`). Design + labels: [`docs/PRIVACY_REAL_V1.md`](PRIVACY_REAL_V1.md).

**Master key:** `ADDITION_PRIVACY_MASTER_KEY` must be at least **32 characters** or note writes fail with `error: ADDITION_PRIVACY_MASTER_KEY not set or too short (min 32)`. Mainnet start requires this key (`docs/MAINNET_RUNBOOK.md`).

### What this is NOT

- **Not** Groth16, Halo2, Bulletproofs, STARKs, or any live ZK circuit
- **Not** Monero ring signatures or Zcash Sapling/Orchard
- **Not** “ZK-Shield” or shielded balances on the UTXO set
- `privacy_mint_zk` / `privacy_spend_zk` are an **ML-DSA wrap** of a mint/spend string — still not a circuit (`tools/zk_backend_contract.md`)
- `tools/zk_verify_wrapper.py` errors if invoked; do not set `ADDITION_ZK_VERIFY_CMD` or advertise a ZK backend as live

Claim language: live `claim=opening_not_zk`; roadmap `privacy_zk_roadmap=zk_pending`.

---

## 5. Consensus and proof of work

### 5.1 Algorithms

| Network | `pow_algorithm` | Work |
| --- | --- | --- |
| Mainnet `ADDITION_MAINNET_V1` | `memory_hard` | ~**1 MiB × 16 rounds** scratch per attempt (`crypto.cpp` `memory_hard_head64`), then SHA3-512 head64 vs target |
| Testnet `ADDITION_TESTNET_V1` | `sha3_512` | SHA3-512 of the header |

Constants verified in `include/addition/config.hpp` and `src/crypto.cpp`:

```text
kScratchSize = 1 << 20   // 1 MiB
kRounds      = 16
```

Mainnet difficulty floor (initial = min = max): **`0x000000FFFFFFFFFF`**. Smaller target = harder. Check: `head64(hash) <= target`.

### 5.2 Timing and retarget

| Parameter | Value (code / genesis) |
| --- | --- |
| `target_block_time_sec` | **60** |
| `retarget_window` | **30** |
| Retarget compare window | `30 × 60s` of timestamps (`chain.cpp`) |
| Early blocks | target × `9/10` (harder) |
| Late blocks | target scaled up, clamped to `[min, max]` |
| Mainnet mine deadline | `mine_deadline_sec=0` (search until found; no 30s testnet abort) |

Because mainnet clamps min and max to the same hard floor, retarget cannot climb back to the testnet easy target `0x0000FFFFFFFFFFFF` (~milliseconds).

### 5.3 Auto-mine

`auto_mine` is **refused on mainnet** (`src/auto_mine.cpp`: `enabled()` returns false for `Mainnet` / `Regtest`; `maybe_mine` returns `auto-mine is testnet only`). Live `getinfo` reports `auto_mine=off`. Config `enable_auto_mine = false` in `config-mainnet.toml` does not override that refusal.

### 5.4 Coinbase

Block template builds a single coinbase output to the **finding miner’s** reward address (`chain.cpp` `make_block_template`). There is **no** 70/25/5 miner/staker/treasury split. Staking is a separate loopback side map with `economic_security=none`.

---

## 6. Economics

From `genesis-mainnet.json`, `config-mainnet.toml`, and `mainnet_chain_config()`:

| Parameter | Value |
| --- | --- |
| `max_supply` | **50,000,000** whole units |
| `block_reward` | **50** |
| `halving_interval` | **210,000** blocks |
| `tail_emission_reward` | 1 (after deep halvings / policy in chain) |
| `min_fee` (spec / genesis) | **0** |
| Precision | Whole integers only — no 8-decimal satoshi subunit |

**Coinbase:** 100% of the block reward (plus collected fees, subject to validation) goes to the finding miner’s address. No token sale, no pre-mine claim in genesis beyond the empty height-0 chain, no ICO language.

**Fees:** Spec / genesis / `mainnet_chain_config()` floor is `min_fee=0`. Congestion heuristics raise `recommended_min_fee` / `dynamic_min_fee` from mempool size and last-block fees (`rpc_server.cpp` `recommended_min_fee`, plus `AIRoutingOptimizer` fee floor). Live seed `fee_info` observed while drafting reported `base_min_fee=1` / `recommended_min_fee=1` and `getinfo` reported `dynamic_min_fee=1` — that is an operational snapshot, not a rewrite of the in-repo genesis constant. Copy live fields; do not invent a fee market narrative.

**Live `monetary_info`** — fetch; do not paste stale emitted/remaining into this doc:

```bash
curl -s 'https://additionblockchain.com/api/rpc?cmd=monetary_info'
```

The rendered site whitepaper loads that response live (fail closed → `RPC offline`).

---

## 7. Networks and ports

Three profiles — not a label flip. Only mainnet is the live public product:

| | Public mainnet (Live) | Research testnet | Fast path (scaffold) |
| --- | --- | --- | --- |
| Flag | `additiond --mainnet` | `additiond --network testnet` (binary default) | `additiond --fast` / `--network fast` (**refuses to boot** until pipeline ships) |
| `network_id` | `ADDITION_MAINNET_V1` | `ADDITION_TESTNET_V1` | `ADDITION_FAST_V1` |
| Genesis | `genesis-mainnet.json` (`timestamp` 1770000000) | `genesis.json` (`timestamp` 1763000000) | `genesis-fast.json` (stub) |
| Default data dir | `data-mainnet` | `data` | `data-fast` (stub) |
| Home-node write RPC | **`127.0.0.1:8546`** | `127.0.0.1:8545` | stub ports in `config-fast.toml` |
| Public HTTP | **`38546`** (seed `34.27.30.115:38546`) | `38545` | none (not a public product) |
| P2P | **`28546`** (seed `34.27.30.115:28546`) | `28545` | none public |
| Consensus | `memory_hard` | `sha3_512` | leader/pipeline sketch — see [`FAST_PATH_V1.md`](FAST_PATH_V1.md) |

**Hard rules:**

- Home write RPC binds **`127.0.0.1:8546`** — never public bind / never `0.0.0.0`
- Never publish `8545` / `8546`
- Never `--bootstrap 34.27.30.115:28545` for mainnet
- Mixing testnet `blocks.dat` into a mainnet `--data-dir` is refused
- Seed operators set `ADDITION_ADVERTISED_P2P=34.27.30.115:28546` so public `getinfo` / `peers` advertise that IPv4 endpoint and do not list `self`

---

## 8. Public RPC

**Seed:** `34.27.30.115:38546`  
**Site proxy:** `/api/rpc` (Cloudflare Worker → `PUBLIC_RPC_HTTP`)

Read path (stock allowlist in `rpc_access.cpp` `is_public_read_command`): `getinfo`, `monetary_info`, `crypto_selftest`, `tx_status`, `peers`, `getblock`, `getblockhash`, `getblockraw`.

**Write allowlist (CoS / public product, documented in `MAINNET_RUNBOOK.md` and verified live):** on seed `38546`, the following answer for join/wallet flows:

- `createwallet`
- `mine`
- `wallet_send`
- sign (`wallet_sign` / `sign_message`)
- `tx_build` / `sendtx_signed*`

Live probe while drafting: `createwallet` on `38546` returned a wallet-store error (`error: wallet already exists`) rather than `command disabled on public RPC` — i.e. the write surface is open on the seed as documented. Token create / presale / airdrop / farm stay off unless separately probed available.

Home operators still keep trusted write on **`127.0.0.1:8546`**. The site wallet prefers `/api/rpc` → public `38546`, with a loopback fallback to `/local-rpc` when public RPC is offline.

Example:

```bash
curl -s 'http://34.27.30.115:38546/rpc?cmd=getinfo'
curl -s 'http://34.27.30.115:38546/jsonrpc?method=getinfo'
```

---

## 9. Wallet, transactions, contracts, tokens

### 9.1 Wallet and spends (public product path)

- `createwallet [name] [scheme]` — default ML-DSA-87; keys in `data/wallets/<name>.wal` (`priv_printed=0`)
- `wallet_send <name> <to> <amount> [fee]` — node signs from the `.wal`; no privkey on the wire
- `tx_build` + `sendtx_signed*` — build unsigned spend, sign off-wire, submit PQ signature
- Legacy `sendtx` with privkey on the line stays disabled unless `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1` (leave unset)
- Standalone CLI: `web/addition_wallet.py` (keys on caller disk)

Site UI: `/wallet/` — product surface for mainnet.

### 9.2 Contracts (KV) — research / local

`contract_deploy` / `contract_call` with `set` | `add` | `get` is a **deterministic key-value store**, not the EVM, not Solidity, not Ethereum JSON-RPC.

### 9.3 Token / NFT / swap engines — research / local vs public product

Implemented in-process in `token_engine.cpp` (persist `tokens.dat`). Useful on trusted loopback for research. Labels:

| Surface | Classification |
| --- | --- |
| `/wallet/` create / send / mine via public `38546` or home `8546` | Public product |
| `token_*` / `nft_*` / `swap_*` on loopback | Research / local |
| Empty AMM pool | **`error: pool not found`** (real code path) |
| Unsigned `token_transfer` | Local research names only — prefer `token_transfer_wallet` / `token_transfer_signed` |

**Not** a public DEX, Uniswap fork, or mainnet token sale. Do not invent liquidity, TVL, or listing prices.

### 9.4 Staking

`stake` / `unstake` / `stake_claim` are a loopback side map. `getinfo` reports `economic_security=none`. Not consensus PoS.

---

## 10. Bridges

Status: **not a live cross-chain product** (`docs/BRIDGE.md`).

What exists:

- In-process `bridge_register` / `bridge_lock` / `bridge_mint` / `bridge_burn` / `bridge_release` / `bridge_balance` — local counters in `bridge.dat`. A string label like `btc` is not observation of Bitcoin.
- Local EVM probe `web/evm/evm_rpc_bridge.py` on **localhost** (`127.0.0.1:9545`). **`eth_sendRawTransaction` is disabled.**
- Bitcoin hygiene receipts (`hygiene_*`) — attestation rehearsal, `moves_bitcoin=0`, not BIP-360.

What does not exist: watched BTC/ETH/SOL custody, mint of ADD from a foreign deposit, public `/bridge` custody widget, or any claim that bridges move foreign assets.

---

## 11. Security assumptions and threat model

### Assumptions

1. **ML-DSA-87 remains hard** against classical and near-term quantum adversaries at the parameter set shipped by liboqs for FIPS 204 level used here — **not** a forever-unbreakable claim.
2. **SHA3-512** remains a secure hash for commitments and PoW digests under current public cryptanalysis.
3. **Majority of hashrate** under memory-hard PoW (same economic assumption family as Bitcoin PoW — not proven by this paper).
4. Operators protect `ADDITION_PRIVACY_MASTER_KEY`, wallet `.wal` files, and never expose home write RPC beyond loopback.
5. Public seed write allowlist is an operational trust surface: anyone who can reach those RPCs can create wallets / mine / send as allowed — treat keys and node storage accordingly.

### Threat model — what privacy is NOT

An observer with node access or the master key can open commitments. This is **not** zero-knowledge privacy against the verifying node. Do not market it as Zcash/Monero-equivalent.

### Other explicit non-goals

- Not FIPS 140-3 validated module
- Not immune to every future cryptanalytic break
- Not a claim of Solana-scale throughput (`research_goal_tps=100000` is labeled `research_goal_is_not_a_measurement=true` in protocol_status / bench paths)
- Staking side map does not provide economic security (`economic_security=none`)

---

## 12. Vision vs Live (roadmap)

Re-state the early table with live getinfo context. Design docs:

- Privacy scaffold: [`docs/PRIVACY_REAL_V1.md`](PRIVACY_REAL_V1.md) (vision #77)
- ZK circuit schema (not live): [`docs/ZK_CIRCUIT_V1.md`](ZK_CIRCUIT_V1.md)
- Fast path scaffold: [`docs/FAST_PATH_V1.md`](FAST_PATH_V1.md) (vision #78, tip SHA `6d3f483` or later)

**Live public `getinfo`** — do not paste stale height/peers into this doc. Fetch from the seed or the site:

```bash
curl -s 'https://additionblockchain.com/api/rpc?cmd=getinfo'
curl -s 'http://34.27.30.115:38546/rpc?cmd=getinfo'
```

The rendered site whitepaper (`/whitepaper/`) loads that response live (fail closed → `RPC offline`). Prefer [`/status/`](https://additionblockchain.com/status/) for the full panel. Height may be `0`; peers change; canned marketing numbers are out of scope.

| Field / topic | Live truth |
| --- | --- |
| `network_id` | From live `getinfo` only |
| Height | From live `getinfo` only — **may be `0`** |
| `pq_mode` / algs | `strict` / `ml-dsa-87` + SHA3-512 (product claim; confirm via live fields) |
| `privacy_claim` | From live `getinfo` (opening is `opening_not_zk`) |
| `privacy_zk_roadmap` | `zk_pending` (scaffold) |
| `consensus_path` | `memory_hard_pow` |
| `fast_path_status` | `not_this_network` on mainnet |
| `throughput_claim` | `none` |
| `research_goal_is_not_a_measurement` | `true` |
| `economic_security` | `none` until real |
| `last_tps` | Local mine telemetry when present — **not** marketed Solana TPS |
| `peers=…` | Live query only — not a permanent marketing number |

No USD ticker (`price_usd` stays null on the site API). PoUW phase-1 text (`docs/POUW_PHASE1_SPEC.md`) is a design target, not a claim that public mainnet already sells compute/storage.

Future hardening called out in-repo (architecture notes): stronger on-disk formats, native ADD lock into privacy (notes are still a side ledger), reproducible releases. Those are backlog items, not shipped guarantees.

**Privacy — Vision vs Live:** live remains `opening_not_zk`. A C++ fail-closed ZK scaffold (`privacy_zk.*`, [`PRIVACY_REAL_V1.md`](PRIVACY_REAL_V1.md)) reports `privacy_zk_roadmap=zk_pending` and does not mint/spend without a real verifier. Circuit work in progress (`zk_circuit_v1.*`, [`ZK_CIRCUIT_V1.md`](ZK_CIRCUIT_V1.md), `zk_circuit_status=not_proven`) — not live ZK. No false ZK claims.

**Fast path — Vision vs Live:** separate profile `ADDITION_FAST_V1` ([`FAST_PATH_V1.md`](FAST_PATH_V1.md), `--fast` / `--network fast`) is **fail-closed** until the leader/pipeline ships. Live public product remains **memory_hard mainnet** (`ADDITION_MAINNET_V1` at `0x000000FFFFFFFFFF`). Do not treat mainnet PoW as a Solana TPS claim.

---

## 13. How to join

1. Read **[/join/](https://additionblockchain.com/join/)** on the site
2. Follow **[`docs/MAINNET_RUNBOOK.md`](MAINNET_RUNBOOK.md)** in this repository
3. Set `ADDITION_PRIVACY_MASTER_KEY` (≥32 characters), build `additiond` from `main`, then:

```bash
export ADDITION_PRIVACY_MASTER_KEY='replace-with-32-or-more-chars____'
export ADDITION_ENABLE_P2P_RPC=1
./build/additiond --mainnet \
  --data-dir $HOME/addition-mainnet \
  --local-rpc-port 8546 \
  --p2p-port 28547 \
  --bootstrap 34.27.30.115:28546
# sync, then getinfo; mine on 127.0.0.1:8546 or public 38546 write allowlist
```

Never bootstrap the research testnet seed for mainnet. Never publish port `8546`. Auto-mine stays off on mainnet.

---

## 14. Contact

**Email:** [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

MIT-licensed software. This whitepaper is research/engineering documentation, not an audit certificate, not investment advice, and not a token-sale prospectus.

---

*Sources: `include/addition/config.hpp`, `src/crypto.cpp`, `src/chain.cpp`, `src/privacy.cpp`, `src/privacy_zk.cpp`, `src/zk_circuit_v1.cpp`, `src/fast_path.cpp`, `src/auto_mine.cpp`, `src/rpc_access.cpp`, `src/rpc_server.cpp`, `genesis-mainnet.json`, `config-mainnet.toml`, `docs/MAINNET_RUNBOOK.md`, `docs/ARCHITECTURE.md`, `docs/BRIDGE.md`, `docs/TOKENS.md`, `docs/WALLET.md`, `docs/PRIVACY_REAL_V1.md`, `docs/ZK_CIRCUIT_V1.md`, `docs/FAST_PATH_V1.md`, `tools/zk_backend_contract.md`, and live RPCs on `34.27.30.115:38546`.*
