# ADDITION Technical Whitepaper

**Brand:** ADDITION (never “SmartChain”)  
**Network (public product):** `ADDITION_MAINNET_V1` / `addition-mainnet`  
**Canonical source:** [github.com/ADDAddition/ADDITION](https://github.com/ADDAddition/ADDITION)  
**Contact:** [contact@additionblockchain.com](mailto:contact@additionblockchain.com)  
**Status:** Engineering description of the C++20 node, configs, and live public RPC. Values that can change (height, peers, fees) come only from live `getinfo` / related RPCs.

This document supersedes the short sketch in `docs/WHITE_PAPER.md`.

---

## What ships on mainnet

| Topic | Live public product |
| --- | --- |
| Network | `ADDITION_MAINNET_V1`, `memory_hard` PoW |
| Height / peers / fees | Copy live `getinfo` only (height may be `0`) |
| Privacy | `privacy_claim=opening_not_zk` (SHA3-512 opening; node sees trapdoor) |
| PQ crypto | **ML-DSA-87** + **SHA3-512**, `pq_mode=strict` — not a forever-unbreakable claim |
| Throughput claim | `throughput_claim=none` on the live product (no invented TPS) |

- Public product **`ADDITION_MAINNET_V1`**: UTXO L1, P2P seed, public RPC, wallet spend path with **ML-DSA-87** / **SHA3-512** / `pq_mode=strict`
- Mainnet PoW **`memory_hard`** at target floor **`0x000000FFFFFFFFFF`**
- Privacy opening path: `privacy_mint_open` / `privacy_spend_open` labeled **`opening_not_zk`**
- Height and monetary fields from **live `getinfo` / `monetary_info` only**

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
9. [Wallet and transactions](#9-wallet-and-transactions)
10. [Security assumptions and threat model](#10-security-assumptions-and-threat-model)
11. [Live network fields](#11-live-network-fields)
12. [How to join](#12-how-to-join)
13. [Contact](#13-contact)

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

The tree is a single canonical C++20 codebase. Major modules for the live product (from `docs/ARCHITECTURE.md` and `src/` / `include/addition/`):

| Module | Role |
| --- | --- |
| `block.*` | Block / header data model and hashing helpers |
| `chain.*` | Canonical ledger, UTXO set, block validation, retarget, coinbase |
| `mempool.*` | Pending txs; rejects unsigned / empty-input / duplicate-outpoint junk |
| `miner.*` | Block template + PoW search (`memory_hard` mainnet) |
| `crypto.*` | ML-DSA-87 (liboqs), SHA3-512, memory-hard head64, address derivation |
| `rpc_server.*` | Trusted TEXT RPC command dispatch |
| `rpc_access.*` / `rpc_network_server.*` | HTTP `/rpc` and `/jsonrpc`, public vs trusted surfaces |
| `wallet.*` / `wallet_store.*` / `wallet_keys.*` | Local `.wal` files, `wallet_send`, key material |
| `p2p.*` / `decentralized_node.*` / `net_io.*` | Handshake, gossip, sync (`HELLO` / `REQBLK` / `BLKDATA`) |
| `privacy.*` | SHA3-512 commitment + nullifier opening (`opening_not_zk`) |
| `state_store.*` | Persistence helpers for side ledgers |
| `ai_optimizer.*` | Local fee-floor / difficulty-bias heuristics from mempool samples |

**Daemon:** `apps/additiond_main.cpp` builds `additiond`. Persistence: `--data-dir/blocks.dat` after each accepted block; UTXOs rebuild by replay. Side state (`privacy.dat`, …) flushes after successful writes and on shutdown.

**Layers (conceptual):**

1. **Consensus / ledger** — UTXO validation, PoW check, emission, retarget (`chain.cpp`)
2. **P2P** — network id in HELLO; mainnet peers rejected if they speak `ADDITION_TESTNET_V1`
3. **RPC** — loopback write on home nodes; public HTTP on the seed (see §8)
4. **Privacy notes** — SHA3-512 opening side ledger (`privacy.dat`)

Build requires **liboqs** and **OpenSSL**. Missing liboqs fails the build.

---

## 3. Cryptography

### 3.1 Post-quantum signatures — ML-DSA-87 (FIPS 204)

| Claim | Status |
| --- | --- |
| Default scheme **ML-DSA-87** (`pq=`), FIPS 204 parameter set via liboqs | Shipped on mainnet spends |
| `pq_mode=strict`, `allowed_sig_algs=ml-dsa-87` | Shipped |
| Hashing / PoW digests / addresses / privacy openings use **SHA3-512** | Shipped |
| “Unbreakable forever” / immune to all future cryptanalysis | **Not claimed** |
| FIPS 140-3 validated cryptographic module | **Not claimed** |

- Thread-local `OQS_SIG` and parallel batch verify in `crypto.cpp`
- Opt-in **SLH-DSA** (`slh-dsa-shake-256s`) only if this liboqs build can `OQS_SIG_sign_with_ctx_str` with a non-empty context; otherwise rejected in strict mode
- Unknown schemes (including Falcon/FN-DSA) are rejected

Strict mode means the node only accepts the allowlisted PQ schemes it can verify. Lattice assumptions can age; operators should track NIST / cryptanalysis guidance over time. PQ here is a **grade and posture**, not a forever warranty.

### 3.2 Hashing — SHA3-512

SHA3-512 via OpenSSL is used for:

- Header / PoW digests (mainnet after memory-hard scratch)
- Address derivation
- Privacy commitments / nullifiers
- Various internal digests

### 3.3 Addresses

```text
address = sha3_512(scheme_id || 0x00 || pubkey_bytes)   # 128 hex
```

Default `scheme_id` is `ml-dsa-87`. The raw 2592-byte ML-DSA-87 public key is **not** the address. Spend paths recompute the hash from the revealed pubkey; a pubkey that does not commit to the address is rejected.

### 3.4 Self-test

`crypto_selftest` on the public seed returns a live report. Copy only what the node prints.

---

## 4. Privacy model (as shipped)

ADDITION privacy is a **SHA3-512 commitment + nullifier opening**. The node sees the trapdoor when minting/spending open notes.

| Field (from `getinfo`) | Value |
| --- | --- |
| `privacy_mode` | `sha3_opening` |
| `privacy_verifier` | `sha3_opening` |
| `privacy_claim` | `opening_not_zk` |
| `privacy_ok` | `true` (when the process answers) |

Relevant RPCs: `privacy_note_prepare`, `privacy_mint_open`, `privacy_spend_open`. Notes live in a **side ledger** (`privacy.dat`), not a native ADD lock into a consensus privacy pool.

**Master key:** `ADDITION_PRIVACY_MASTER_KEY` must be at least **32 characters** or note writes fail with `error: ADDITION_PRIVACY_MASTER_KEY not set or too short (min 32)`. Mainnet start requires this key (`docs/MAINNET_RUNBOOK.md`).

### What this is NOT

- **Not** Groth16, Halo2, Bulletproofs, STARKs, or any live ZK circuit
- **Not** Monero ring signatures or Zcash Sapling/Orchard
- **Not** “ZK-Shield” or shielded balances on the UTXO set

Live claim language: `claim=opening_not_zk`.

---

## 5. Consensus and proof of work

### 5.1 Algorithms

| Network | `pow_algorithm` | Work |
| --- | --- | --- |
| Mainnet `ADDITION_MAINNET_V1` | `memory_hard` | ~**1 MiB × 16 rounds** scratch per attempt (`crypto.cpp` `memory_hard_head64`), then SHA3-512 head64 vs target |

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
| Mainnet mine deadline | `mine_deadline_sec=0` (search until found) |

Because mainnet clamps min and max to the same hard floor, retarget cannot climb to an easy testnet-style target.

### 5.3 Auto-mine

`auto_mine` is **refused on mainnet** (`src/auto_mine.cpp`). Live `getinfo` reports `auto_mine=off`. Config `enable_auto_mine = false` in `config-mainnet.toml` does not override that refusal.

### 5.4 Coinbase

Block template builds a single coinbase output to the **finding miner’s** reward address (`chain.cpp` `make_block_template`). There is **no** 70/25/5 miner/staker/treasury split. Coinbase is 100% to the finding miner.

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

**Fees:** Spec / genesis / `mainnet_chain_config()` floor is `min_fee=0`. Congestion heuristics may raise `recommended_min_fee` / `dynamic_min_fee` from mempool size and last-block fees. Copy live fields from the node; do not invent a fee market narrative.

**Live `monetary_info`** — fetched on this page (fail closed → `RPC offline`):

@@LIVE_MONETARY_SNAPSHOT@@

---

## 7. Networks and ports

Public product profile:

| | Public mainnet |
| --- | --- |
| Flag | `additiond --mainnet` |
| `network_id` | `ADDITION_MAINNET_V1` |
| Genesis | `genesis-mainnet.json` (`timestamp` 1770000000) |
| Default data dir | `data-mainnet` |
| Home-node write RPC | **`127.0.0.1:8546`** |
| Public HTTP | **`38546`** (seed `34.27.30.115:38546`) |
| P2P | **`28546`** (seed `34.27.30.115:28546`) |
| Consensus | `memory_hard` |

**Hard rules:**

- Home write RPC binds **`127.0.0.1:8546`** — never public bind / never `0.0.0.0`
- Never publish `8545` / `8546`
- Mixing another network’s `blocks.dat` into a mainnet `--data-dir` is refused
- Seed operators set `ADDITION_ADVERTISED_P2P=34.27.30.115:28546` so public `getinfo` / `peers` advertise that IPv4 endpoint and do not list `self`

A separate testnet profile (`ADDITION_TESTNET_V1`, ports `28545` / `38545`) exists in the same binary for operators; it is **not** the public product on this site. See `/join/#testnet`.

---

## 8. Public RPC

**Seed:** `34.27.30.115:38546`  
**Site proxy:** `/api/rpc` (Cloudflare Worker → `PUBLIC_RPC_HTTP`)

Read path (stock allowlist in `rpc_access.cpp` `is_public_read_command`): `getinfo`, `monetary_info`, `crypto_selftest`, `tx_status`, `peers`, `getblock`, `getblockhash`, `getblockraw`.

**Write allowlist (CoS / public product, documented in `MAINNET_RUNBOOK.md`):** on seed `38546`, the following answer for join/wallet flows:

- `createwallet`
- `mine`
- `wallet_send`
- sign (`wallet_sign` / `sign_message`)
- `tx_build` / `sendtx_signed*`

Home operators still keep trusted write on **`127.0.0.1:8546`**. The site wallet prefers `/api/rpc` → public `38546`, with a loopback fallback to `/local-rpc` when public RPC is offline.

Example:

```bash
curl -s 'http://34.27.30.115:38546/rpc?cmd=getinfo'
curl -s 'http://34.27.30.115:38546/jsonrpc?method=getinfo'
```

---

## 9. Wallet and transactions

### 9.1 Wallet and spends (public product path)

- `createwallet [name] [scheme]` — default ML-DSA-87; keys in `data/wallets/<name>.wal` (`priv_printed=0`)
- `wallet_send <name> <to> <amount> [fee]` — node signs from the `.wal`; no privkey on the wire
- `tx_build` + `sendtx_signed*` — build unsigned spend, sign off-wire, submit PQ signature
- Legacy `sendtx` with privkey on the line stays disabled unless `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1` (leave unset)
- Standalone CLI: `web/addition_wallet.py` (keys on caller disk)

Site UI: `/wallet/` — product surface for mainnet.

### 9.2 What is not a public product on this site

Loopback-only token / KV / swap / bridge operators tools may exist next to a home node. They are **not** a public DEX, Uniswap fork, mainnet token sale, or live cross-chain bridge. Do not invent liquidity, TVL, listing prices, or foreign-chain custody.

---

## 10. Security assumptions and threat model

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
- Not a Solana-scale (or any invented) TPS claim on live mainnet
- Live `getinfo` may report `economic_security=none` — that field is copied from the node, not upgraded in marketing copy

---

## 11. Live network fields

**Live public `getinfo`** (fetched from seed `34.27.30.115:38546` via `/api/rpc` — height may be `0`; peers change; never a canned marketing number). Prefer [`/status/`](/status/) for the full panel.

@@LIVE_GETINFO_SNAPSHOT@@

| Field / topic | Live truth |
| --- | --- |
| `network_id` | From live `getinfo` only |
| Height | From live `getinfo` only — **may be `0`** |
| `pq_mode` / algs | `strict` / `ml-dsa-87` + SHA3-512 (confirm via live fields) |
| `privacy_claim` | From live `getinfo` (opening is `opening_not_zk`) |
| `consensus_path` | `memory_hard_pow` on mainnet |
| `throughput_claim` | `none` |
| `peers=…` | Live query only — not a permanent marketing number |

No USD ticker (`price_usd` stays null on the site API).

---

## 12. How to join

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

Never publish port `8546`. Auto-mine stays off on mainnet.

---

## 13. Contact

**Email:** [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

MIT-licensed software. This whitepaper is engineering documentation, not an audit certificate, not investment advice, and not a token-sale prospectus.

---

*Sources: `include/addition/config.hpp`, `src/crypto.cpp`, `src/chain.cpp`, `src/privacy.cpp`, `src/auto_mine.cpp`, `src/rpc_access.cpp`, `src/rpc_server.cpp`, `genesis-mainnet.json`, `config-mainnet.toml`, `docs/MAINNET_RUNBOOK.md`, `docs/ARCHITECTURE.md`, `docs/WALLET.md`, and live RPCs on `34.27.30.115:38546`.*
