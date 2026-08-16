# ADDITION: The Post-Quantum Layer 1 Protocol (White Paper V3.0)

**Date:** March 7, 2026  
**Version:** 3.0-Final-Audit  
**Status:** Research prototype / testnet (not a live mainnet)

---

## 1. Abstract

ADDITION (ADD) is a Layer 1 blockchain architected for the era of quantum computing. Unlike legacy networks (Bitcoin, Ethereum) built on elliptic curve cryptography (ECC) which is vulnerable to Shor's algorithm, ADDITION natively integrates hybrid **ML-DSA (Dilithium)** signatures and a **deterministic parallel execution engine**. With a fixed supply of **50,000,000 ADD** and a deflationary reward schedule, ADDITION combines absolute digital scarcity with technological resilience capable of surviving the next 50 years of computational evolution.

---

## 2. Problem Statement: The Quantum Threat

Classic public-key cryptography (RSA, ECDSA) relies on the hardness of factoring large integers or solving discrete logarithms. Quantum computers, using Shor's algorithm, reduce the time complexity of breaking these keys from exponential to polynomial time.

**The Risk:** A sufficiently powerful quantum computer could derive private keys from public keys on Bitcoin and Ethereum, allowing an attacker to drain wallets and forge transactions.

**The Solution:** ADDITION uses **Lattice-based cryptography** (ML-DSA / Dilithium), which is currently believed to be resistant to both classical and quantum attacks. By implementing this at the protocol layer (L1), ADDITION secures the entire network, not just a specific smart contract.

---

## 3. Technical Architecture: The 4th Generation Core

### 3.1 Hybrid Post-Quantum Cryptography (PQ)
ADDITION employs a hybrid signature scheme to ensure a smooth transition and maximum security:

*   **Primary Scheme:** ML-DSA (Module-Lattice-Based Digital Signature Algorithm) - NIST Standard.
*   **Hashing:** SHA3-512 (Keccak) - Provides superior collision resistance compared to SHA-256.
*   **Fail-Safe Design:** The `sign_message_hybrid` function allows the network to pivot between signature algorithms via soft forks without disrupting the chain state.

### 3.2 Deterministic Parallel Execution
Traditional blockchains process transactions sequentially (one after another) to avoid state conflicts. ADDITION introduces **Conflict-Aware Scheduling**:

1.  **Conflict Analysis:** The node inspects the UTXO inputs of incoming transactions.
2.  **Parallel Batching:** Transactions that do not touch the same funds are grouped into independent batches.
3.  **Execution:** The `deterministic_schedule` engine processes these batches simultaneously across all available CPU cores.
4.  **Result:** 100% hardware utilization and massive throughput scaling (TPS).

---

## 4. Consensus Mechanism: Proof of Measurable Work (PoMW)

ADDITION evolves the Proof-of-Work (PoW) concept into **Proof of Measurable Work (PoMW)**.

*   **Work:** Miners perform SHA3-512 hashing to secure the ledger.
*   **Measurability:** The protocol embeds real-time telemetry into the block headers (`latency_p50_ms`, `last_tps`).
*   **Incentive:** The network favors nodes that provide not just security, but also high performance and low latency connectivity.

**Reward Distribution:**
*   **70% Miners:** To pay for hardware and electricity (Security).
*   **25% Stakers:** To lock supply and stabilize the economy (PoS Layer).
*   **5% Treasury:** For protocol development and ecosystem grants.

---

## 5. Smart Contracts: Lightweight Deterministic Contract Engine (LDCE)

ADDITION eschews the complexity and vulnerability of the EVM (Ethereum Virtual Machine) for a safer alternative: **LDCE**.

*   **Architecture:** Deterministic Key-Value Store.
*   **Capabilities:**
    *   `deploy(owner, code, ttl)`: Create a contract with an optional Time-To-Live.
    *   `set(key, value)`: Store state.
    *   `get(key)`: Retrieve state.
    *   `add(key, amount)`: Atomic counters (perfect for tokens).
*   **Use Cases:** Tokenization (ADD-20), Voting Systems, Oracles, Decentralized Identity (DID).

---

## 6. Privacy & Messaging

### 6.1 Privacy pool (SHA3-512 opening)
The in-process `privacy.cpp` module checks a **SHA3-512 commitment + nullifier opening** (`privacy_mint_open` / `privacy_spend_open`). The node recomputes the hash relation and sees the trapdoor. This is not zero-knowledge, not Groth16, not Bulletproofs, and not a SNARK circuit.

### 6.2 On-Chain Messaging
ADDITION supports immutable messaging by utilizing the transaction `nonce` and `output` fields as data carriers. This allows for censorship-resistant communication and data anchoring directly on the blockchain.

---

## 7. Monetary Policy (Tokenomics)

The economic constants are immutable and inscribed in the C++ kernel (`config.hpp`).

| Parameter | Value | Note |
| :--- | :--- | :--- |
| **Max Supply** | **50,000,000 ADD** | Fixed cap. No inflation after emission. |
| **Initial Block Reward** | **50 ADD** | Emitted every block. |
| **Halving Interval** | **210,000 Blocks** | ~5 months. Rapid deflationary schedule. |
| **Target Block Time** | **60 Seconds** | Adjusted via `difficulty_window` (120 blocks). |
| **Atomic Precision** | **10^8** | 1 ADD = 100,000,000 Satoshis. |

---

## 8. Research status

This tree is a **research testnet** (`additiond --network testnet`). It is not a live public chain and not a shipping network. The experimental `--network mainnet` profile stays opt-in and is not a public chain.

---

## 9. Conclusion

ADDITION is a research prototype: ML-DSA-87 signatures, SHA3-512 hashing, and a SHA3 opening privacy path. It does not claim production status or a live public L1.

---
*Research notes. Not an audit certificate.*
