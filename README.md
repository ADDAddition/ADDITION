<p align="center">
  <img src="docs/assets/logo-transparent.png" alt="Addition Logo" width="250">
</p>

<h1 align="center">Addition (ADD)</h1>
<p align="center"><b>The Quantum-Secure Layer 1 Blockchain</b></p>

<p align="center">
  <img src="https://img.shields.io/badge/Status-Mainnet--Ready-brightgreen?style=for-the-badge" alt="Status">
  <img src="https://img.shields.io/badge/Security-Quantum--Resistant-blue?style=for-the-badge" alt="Security">
  <img src="https://img.shields.io/badge/Privacy-Enabled-blueviolet?style=for-the-badge" alt="Privacy">
  <img src="https://img.shields.io/badge/Supply-50M_Fixed-gold?style=for-the-badge" alt="Supply">
</p>

---

## ?? The Post-Quantum Revolution
Addition is a next-generation Layer 1 blockchain engineered for the post-quantum era. While legacy networks remain vulnerable to future quantum computing threats, Addition employs **ML-DSA-87 (Module-Lattice-Based Digital Signature Algorithm)** in strict mode to ensure absolute cryptographic longevity.

### ?? Competitive Analysis: Addition vs. The Market

| Feature | Addition (ADD) | Bitcoin (BTC) | Ethereum (ETH) |
| :--- | :--- | :--- | :--- |
| **Signature Security** | **Quantum-Resistant (ML-DSA)** | ECDSA (Vulnerable) | ECDSA (Vulnerable) |
| **Max Supply** | **50,000,000 ADD** | 21,000,000 BTC | Infinite (Burn-based) |
| **Privacy Engine** | **Native Privacy Pools** | Transparent Only | Public (L2 Needed) |
| **P2P Hardening** | **Signed Handshakes** | Basic / Anonymous | Basic / Anonymous |
| **Architecture** | **Mining + Staking Hybrid** | Proof of Work | Proof of Stake |
| **Asset Support** | **Tokens & NFTs Native** | Limited (Ordinals) | Native (ERC-20/721) |

---

## ?? Core Technical Architecture

### ??? Hardened Security
- **ML-DSA-87 Strict Mode:** Future-proof signatures protecting against Shor's algorithm.
- **Signed P2P Transport:** Every node handshake is signed to prevent replay attacks and relay deduplication.
- **Rate-Limited Gossip:** Advanced transport hardening against DDoS and network congestion.

### ??? Privacy & Compliance
- **Privacy Pool Primitives:** Native shielding for assets and smart contract state.
- **Confidential Assets:** Issue tokens and NFTs with built-in privacy controls.

### ? Professional Frontend Suite
- **Wallet Pro:** Advanced Python-based dashboard for power users, miners, and stakers.
- **Cross-Platform Mobile:** Flutter-powered app for iOS, Android, and Windows.
- **Portal Bridge:** Real-time metrics and a sleek web entry point.

---

## ?? Quick Start

### 1. Build from Source
```bash
cmake -S . -B build
cmake --build build
```

### 2. Launch the Daemon
```bash
# Linux/macOS
./build/additiond

# Windows
launch_daemon.bat
```

### 3. Professional Tools
- **Desktop Wallet:** `web/launch_wallet_pro.bat`
- **Mobile App:** `cd client/addition_app && flutter run`

---

## ?? Global Deployment
Addition is built for the modern cloud:
- **Scalable Infrastructure:** Docker-ready via `deploy/github-docker`.
- **Global Delivery:** Optimized for Cloudflare Workers via `deploy/cloudflare`.
- **Live Monitoring:** Integrated Portal Backend for real-time network health.

---

<div align="center">
  <h3>Project Showcase</h3>
  <video src="docs/assets/promo.mp4" width="800" controls muted autoplay loop>
    Your browser does not support the video tag.
  </video>
</div>

<p align="center">
  <br>
  <b>Developed with precision. Secured for the future. Welcome to Addition.</b>
</p>
