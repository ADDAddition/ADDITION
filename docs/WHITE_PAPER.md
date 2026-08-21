# ADDITION: The Post-Quantum Layer 1 Protocol (White Paper V3.1)

**Date:** August 21, 2026  
**Version:** 3.1  
**Status:** Research prototype / testnet (not a live mainnet)

The canonical public copy is [additionblockchain.com/whitepaper/](https://additionblockchain.com/whitepaper/). This file must not invent a parallel execution engine, a 70/25/5 reward split, satoshi subunits, or a live public mainnet.

---

## 1. Abstract

ADDITION is a C++20 Layer 1 node (`additiond`) that uses **ML-DSA-87** signatures and **SHA3-512** hashing. The public product today is the **testnet** at [additionblockchain.com](https://additionblockchain.com). Supply is a hard cap of **50,000,000** whole units. There is no 8-decimal subunit.

---

## 2. Problem Statement: The Quantum Threat

Classic public-key cryptography (RSA, ECDSA) is believed to fall to Shor's algorithm on a large quantum computer.

ADDITION uses **ML-DSA-87** (FIPS 204) at the protocol layer. That is a lattice signature, not a claim that the chain is immune to every future cryptanalytic break. SLH-DSA (`slh-dsa-shake-256s`) is opt-in and stays disabled unless this liboqs build can sign with a non-empty context.

---

## 3. What ships

### 3.1 Cryptography
- **Signatures:** ML-DSA-87 (`pq=` format) in `pq_mode=strict`
- **Hash / PoW:** SHA3-512 of the header on testnet; `memory_hard` on the separate `--mainnet` profile
- **Addresses:** SHA3-512(scheme_id || 0x00 || pubkey) — 128 hex

### 3.2 What does not ship
- No `deterministic_schedule` parallel execution engine
- No measured Solana-scale TPS. `research_goal_tps=100000` is labeled `research_goal_is_not_a_measurement=true`
- No public EVM. Local `/evm/` is a loopback probe; `eth_sendRawTransaction` stays disabled
- No IBC / foreign-chain bridge. `BridgeEngine` is in-process counters

---

## 4. Consensus

Testnet PoW is SHA3-512 header work. The `--mainnet` profile is a **separate chain** (`ADDITION_MAINNET_V1`), not a label flip and not a live public network.

Block reward goes to the miner address of that block. There is no 70/25/5 miner/staker/treasury split. Staking is a loopback side map (`stake` / `unstake` / `stake_claim`) with `economic_security=none`.

`getinfo` may report `last_tps` from the last mined block. That is local telemetry, not a consensus incentive.

---

## 5. Contracts, tokens, swap

`contract_*` is a deterministic key-value store (`set` / `add` / `get`). It is not the EVM.

`token_*` and `swap_*` are an in-process ledger on trusted write RPC (`127.0.0.1`). Unsigned `token_transfer` remains for local research names (`alice` / `bob`). PQ-signed paths:

- `token_transfer_wallet` / `token_transfer_signed`
- `swap_exact_in_wallet`
- `swap_best_route_exact_in_signed`

This is not a public DEX, Uniswap, or token sale.

---

## 6. Privacy

`privacy_mint_open` / `privacy_spend_open` check a SHA3-512 commitment + nullifier opening. The node sees the trapdoor. `claim=opening_not_zk`. Not Groth16, not Bulletproofs, not Monero, not Zcash.

`ADDITION_PRIVACY_MASTER_KEY` must be at least 32 characters or note writes fail. Notes are a side ledger, not a native ADD lock.

---

## 7. Monetary policy

| Parameter | Value |
| :--- | :--- |
| Max supply | 50,000,000 whole units |
| Block reward | 50 |
| Halving interval | 210,000 blocks |
| Target block time | 60 seconds |
| Min fee | 0 (congestion can raise `recommended_min_fee`) |
| Precision | whole integers only |

---

## 8. Research status

Public product: `additiond --network testnet`. Write RPC stays loopback. Public read is an allowlist.

`--mainnet` is opt-in, local, and not the website.

---

## 9. Conclusion

ADDITION is a research prototype: ML-DSA-87, SHA3-512, SHA3 opening privacy, local tokens/swap. It does not claim production status or a live public L1.

---
*Research notes. Not an audit certificate.*
