# ADDITION (ADD) — The Post-Quantum Layer 1 Blockchain

![Build Status](https://img.shields.io/badge/build-passing-brightgreen?style=for-the-badge)
![License](https://img.shields.io/badge/license-MIT-blue?style=for-the-badge)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue?style=for-the-badge)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey?style=for-the-badge)
![Status](https://img.shields.io/badge/status-MAINNET%20LIVE-red?style=for-the-badge)

**ADDITION** is a next-generation Layer 1 blockchain engineered to survive the quantum computing era. It features a native hybrid cryptographic core (Dilithium + SHA3-512), a deterministic parallel execution engine, and a fully modular runtime for privacy, smart contracts, and interoperability.

---

## 🌐 Official Network Portals

Access the mainnet explorer, wallet, and web nodes via our global edge network (powered by Cloudflare):

| Region / Mirror | Domain | Status |
| :--- | :--- | :--- |
| **Global Primary** | [**bitcoins.express**](https://bitcoins.express) | 🟢 Online |
| **Mirror A** | [**bnkchain.com**](https://bnkchain.com) | 🟢 Online |
| **Mirror B** | [**chainbnk.com**](https://chainbnk.com) | 🟢 Online |
| **Doge Ecosystem** | [**dogecointoday.com**](https://dogecointoday.com) | 🟢 Online |
| **AI Node** | [**gcli.ai**](https://gcli.ai) | 🟢 Online |
| **Exchange Hub** | [**tradexai.ai**](https://tradexai.ai) | 🟢 Online |

---

## 🚀 Key Features (Audited v3.0)

### 🛡️ Quantum-Resistant Core
While Bitcoin and Ethereum rely on ECDSA (vulnerable to Shor's algorithm), ADDITION implements **ML-DSA (Dilithium)** signatures native to the protocol. Your assets are secure for the next 50 years.

### ⚡ Parallel Deterministic Execution
The `deterministic_schedule` engine sorts and processes non-conflicting transactions in parallel across all CPU cores, enabling massive throughput scaling without centralization.

### 🕵️ Zero-Knowledge Privacy
Built-in **Privacy Pool** using range proofs allows for confidential transactions when needed, compliant with regulatory standards.

### ⛏️ Proof of Measurable Work (PoMW)
A modern take on PoW. The network rewards miners not just for hashing, but for verifiable node performance and telemetry, ensuring a robust and fast infrastructure.

### 🧩 Smart Contracts (LDCE)
Lightweight Deterministic Contract Engine (LDCE) for secure, gas-efficient tokenization, voting, and decentralized identity (DID).

---

## 💎 Tokenomics

*   **Ticker:** ADD
*   **Max Supply:** 50,000,000 (Fixed)
*   **Block Time:** 60 Seconds
*   **Halving:** Every 210,000 Blocks (~5 months)
*   **Rewards:**
    *   70% Miners (PoW)
    *   25% Stakers (PoS)
    *   5% Development Treasury

---

## 🛠️ Build & Run

### Prerequisites
*   CMake 3.20+
*   C++17 Compiler (MSVC / GCC / Clang)
*   Python 3.10+ (for tools)

### Compilation

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Run Node

```bash
./build/additiond
```

---

## 📄 Documentation

*   [**White Paper v3.0 (Audited)**](docs/WHITE_PAPER.md) - The complete technical specification.
*   [**Command Reference**](docs/FINAL_COMMANDS.md) - RPC and CLI manual.
*   [**Web Portal Guide**](docs/AGENT_LAUNCH_AND_CLOUDFLARE_GUIDE.md) - Hosting instructions.

---

## 🤝 Contributing

ADDITION is an open-source project. We welcome contributions to the core C++ engine, the Python tools, and the Web Portal.

1.  Fork the repository
2.  Create your feature branch (`git checkout -b feature/amazing-feature`)
3.  Commit your changes (`git commit -m 'Add amazing feature'`)
4.  Push to the branch (`git push origin feature/amazing-feature`)
5.  Open a Pull Request

---

**License:** MIT  
**Copyright:** © 2026 ADDITION Foundation
