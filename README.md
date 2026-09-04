<p align="center">
  <img src="docs/assets/logo-transparent.png" alt="ADDITION" width="280">
</p>

<h1 align="center">ADDITION</h1>
<p align="center"><b>C++20 Layer 1 · ML-DSA-87 · SHA3-512 · public product: mainnet</b></p>

<p align="center">
  <img src="https://img.shields.io/badge/license-MIT-blue?style=for-the-badge" alt="MIT">
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge" alt="C++20">
  <img src="https://img.shields.io/badge/public_product-mainnet-brightgreen?style=for-the-badge" alt="Public product: mainnet">
  <img src="https://img.shields.io/badge/crypto-ML--DSA--87-red?style=for-the-badge" alt="ML-DSA-87">
  <img src="https://img.shields.io/badge/Supply-50M_Hard_Cap-gold?style=for-the-badge" alt="Supply 50M">
</p>

<p align="center">
  <a href="https://additionblockchain.com/join/">Join</a> ·
  <a href="https://additionblockchain.com/download/">Download wallet</a> ·
  <a href="https://additionblockchain.com/docs/">Docs</a> ·
  <a href="https://additionblockchain.com/join.md">join.md</a> ·
  <a href="mailto:contact@additionblockchain.com">contact@additionblockchain.com</a>
</p>

---

**ADDITION** is a C++20 Layer 1 node (`additiond`): SHA3-512 hashing, ML-DSA-87 signatures, hash-committed addresses, and SHA3 commitment openings. The public site [additionblockchain.com](https://additionblockchain.com) presents **ADDITION_MAINNET_V1** (public RPC `34.27.30.115:38546` / site `/api/rpc`). Research testnet remains a separate chain.

### Technical matrix

| Feature | ADDITION | Solana | Ethereum | Bitcoin |
| :--- | :--- | :--- | :--- | :--- |
| **Signatures** | **ML-DSA-87** | Ed25519 | ECDSA | ECDSA |
| **Hash** | **SHA3-512** | SHA-256 | Keccak-256 | SHA-256 |
| **Addresses** | **SHA3-512 commitment (128 hex)** | Ed25519 pubkey | keccak | HASH160 |
| **Privacy path** | **SHA3-512 opening (not ZK)** | public ledger | public ledger | public ledger |
| **Public network today** | **mainnet** | mainnet | mainnet | mainnet |
| **Max supply** | **50,000,000** (whole units) | no fixed cap | no fixed cap | 21,000,000 |
| **Min fee** | **0** (whole unit) | lamports | wei | satoshis |

---

## Networks

| | Research testnet | Public mainnet |
| :--- | :--- | :--- |
| Flag | `additiond --network testnet` | `additiond --mainnet` |
| `network_id` | `ADDITION_TESTNET_V1` | `ADDITION_MAINNET_V1` |
| Genesis | [`genesis.json`](genesis.json) | [`genesis-mainnet.json`](genesis-mainnet.json) |
| Bootstrap | `34.27.30.115:28545` | `34.27.30.115:28546` |
| Public RPC | `34.27.30.115:38545` | `34.27.30.115:38546` / site `/api/rpc` |
| Role | Research chain (separate) | Public product — explorer, join, wallet PWA |

`--mainnet` is the public ADDITION mainnet (`ADDITION_MAINNET_V1`), a separate chain from the research testnet. See [docs/MAINNET_RUNBOOK.md](docs/MAINNET_RUNBOOK.md) and the mainnet section in [`join.md`](web/public/join.md).

---

## Architecture

What `main` ships:

- **Hash / PoW** — SHA3-512 of the header on testnet; `memory_hard` on `--mainnet`
- **Signatures** — ML-DSA-87 (FIPS 204) in `pq_mode=strict`
- **Addresses** — hash-committed: `SHA3-512(scheme_id || 0x00 || pubkey)` (128 hex)
- **Privacy** — SHA3-512 commitment + nullifier **opening** (`privacy_mint_open` / `privacy_spend_open`). A hash relation, not ZK
- **SLH-DSA** — opt-in vault (`slh-dsa-shake-256s`) when this liboqs can sign with a non-empty context; otherwise disabled
- **RPC** — home-node write on `127.0.0.1:8546` (mainnet). Public seed `34.27.30.115:38546` write allowlist is open per CoS (`createwallet`, `mine`, `wallet_send`, sign, `tx_build`). Site wallet prefers `/api/rpc` → that path, with `/local-rpc` loopback fallback. Height from live `getinfo` may be `0`.

Whole integers. No 8-decimal subunit. Block reward `50` (100% to finding miner), `max_supply` `50000000`, min fee `0`.

---

## Cashiers / Windows

Cashiers and Windows users download the live mainnet / local wallet from [https://additionblockchain.com/download/](https://additionblockchain.com/download/) only (`addition-wallet-mainnet` and `addition-wallet-cli-mainnet`). Do not compile liboqs. There is no Windows compile path in this README. This host does not publish a `.exe`.

The helper talks to write RPC on `127.0.0.1:8546` on the machine that already runs `additiond --mainnet`. The desktop binary refuses non-loopback hosts. The in-browser `/wallet/` page prefers public `38546` write via `/api/rpc` when available.

### Flutter desktop + local operator tools (loopback)

Linux operators can also use the Flutter desktop wallet from this repo (`client/addition_app`, shipped in #55) and optional loopback helpers:

```bash
# Linux: deps + build + start testnet write on 127.0.0.1:8545
./scripts/setup_desktop.sh --mode testnet --run-wallet
```

| Mode | Write RPC | Notes |
| :--- | :--- | :--- |
| testnet (default) | `127.0.0.1:8545` | Prefer for demos / pool |
| mainnet | `127.0.0.1:8546` | Local/operator; height from live `getinfo` |
| regtest | `127.0.0.1:8547` | Local min-diff |

Optional loopback-only tools (never public write):
- EVM: `python3 web/evm/evm_rpc_bridge.py` (JSON-RPC `127.0.0.1:9545`, send disabled)
- Mining pool: `python3 tools/mining_pool.py coordinator` then `… worker --once` — serializes one loopback `mine` at a time (prefer testnet `:8545`; refuses `38545`/`38546`/`18545` and non-loopback; **not NiceHash**). See [`tools/mining_pool_README.md`](tools/mining_pool_README.md).

---

## Join the mainnet

Build `additiond` on Linux from this repository on `main`, then:

```bash
export ADDITION_PRIVACY_MASTER_KEY='replace-with-32-or-more-chars____'
export ADDITION_ENABLE_P2P_RPC=1
additiond --mainnet --data-dir $HOME/addition-mainnet --local-rpc-port 8546 --p2p-port 28547 --bootstrap 34.27.30.115:28546
```

Type `sync` on the daemon stdin, or send it to write RPC on `127.0.0.1:8546`. Height should move. Write RPC stays loopback.

```bash
curl -s 'http://34.27.30.115:38546/rpc?cmd=getinfo'
curl -s 'https://additionblockchain.com/api/rpc?cmd=getinfo'
```

Expect `network=mainnet` / `ADDITION_MAINNET_V1`. Height may be `0` — copy only live fields.

Desktop wallet (loopback RPC): [additionblockchain.com/download/](https://additionblockchain.com/download/) · in-browser PWA: [additionblockchain.com/wallet/](https://additionblockchain.com/wallet/)

Research testnet (separate): `additiond --network testnet … --bootstrap 34.27.30.115:28545` (write `8545`, read `38545`).
---

## Build on Linux (Ubuntu EliteDesk)

Compile path is **Linux only** (Ubuntu on the EliteDesk). `chmod`, `sudo`, `apt`, and `cmake` are Linux commands. Do not paste them into PowerShell. There is no Windows compile path and no MSVC recipe.

Cashiers and Windows users skip this section and use [https://additionblockchain.com/download/](https://additionblockchain.com/download/).

CMake 3.20+, `g++` on Ubuntu, OpenSSL (`libssl-dev`), and [liboqs](https://github.com/open-quantum-safe/liboqs).

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ ninja-build libssl-dev git
git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git
cmake -S liboqs -B liboqs/build -GNinja -DOQS_USE_OPENSSL=ON -DBUILD_SHARED_LIBS=ON
cmake --build liboqs/build --target install
sudo ldconfig

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target additiond
./build/additiond --network testnet --data-dir $HOME/addition-testnet
```

```bash
cmake -S . -B build -DADDITION_BUILD_TESTS=ON
cmake --build build --target test_chain
ctest --test-dir build --output-on-failure
```

---

## Docs

| | |
| :--- | :--- |
| Join (HTML) | [additionblockchain.com/join/](https://additionblockchain.com/join/) |
| Join (raw) | [additionblockchain.com/join.md](https://additionblockchain.com/join.md) |
| Join (raw copy) | [additionblockchain.com/docs/join.md](https://additionblockchain.com/docs/join.md) |
| Wallet download | [additionblockchain.com/download/](https://additionblockchain.com/download/) |
| Site docs | [additionblockchain.com/docs/](https://additionblockchain.com/docs/) |
| Architecture | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| Commands | [docs/FINAL_COMMANDS.md](docs/FINAL_COMMANDS.md) |
| Wallet (loopback) | [docs/WALLET.md](docs/WALLET.md) |
| Two-node | [docs/TWO_NODE_TESTNET.md](docs/TWO_NODE_TESTNET.md) |
| SHA3 opening | [docs/REAL_TESTNET_MINE_AND_PRIVACY.md](docs/REAL_TESTNET_MINE_AND_PRIVACY.md) |
| Public-read RPC | [docs/TESTNET_PUBLIC_RPC_RUNBOOK.md](docs/TESTNET_PUBLIC_RPC_RUNBOOK.md) |
| `--mainnet` chain | [docs/MAINNET_RUNBOOK.md](docs/MAINNET_RUNBOOK.md) |

Local static site: `python3 web/serve.py` (Pages root is `web/public/`).

---

## Contributing

1. Fork the repository
2. Create a feature branch
3. Keep secrets out of git (`build/`, `data/`, `.wrangler/`, `*.dat`, keys)
4. Open a pull request

<p align="center">
  <b>License:</b> <a href="LICENSE">MIT</a> ·
  <b>Contact:</b> <a href="mailto:contact@additionblockchain.com">contact@additionblockchain.com</a>
</p>
