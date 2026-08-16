<p align="center">
  <img src="web/public/logo-transparent.png" alt="ADDITION" width="380">
</p>

# ADDITION

<p align="center">
  <img src="https://img.shields.io/badge/license-MIT-blue?style=for-the-badge" alt="MIT">
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge" alt="C++20">
  <img src="https://img.shields.io/badge/public_product-testnet-orange?style=for-the-badge" alt="Public product: testnet">
  <img src="https://img.shields.io/badge/crypto-ML--DSA--87-red?style=for-the-badge" alt="ML-DSA-87">
</p>

**ADDITION** is a C++20 Layer 1 node (`additiond`): SHA3-512 hashing, ML-DSA-87 signatures, and SHA3 commitment openings. The public site [additionblockchain.com](https://additionblockchain.com) and [rpc.additionblockchain.com](https://rpc.additionblockchain.com) are the **testnet** product until the operator switches them.

[Join](https://additionblockchain.com/join/) · [Download wallet](https://additionblockchain.com/download/) · [Docs](https://additionblockchain.com/docs/) · [Raw join.md](https://additionblockchain.com/join.md) · [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

---

## Join the testnet

Build `additiond` from this repository on `main`, then:

```bash
additiond --network testnet --data-dir <dir> --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545
```

Type `sync` on the daemon stdin, or send it to write RPC on `127.0.0.1`. Height should move. Write RPC stays loopback.

```bash
curl 'https://rpc.additionblockchain.com/rpc?cmd=getinfo'
```

Desktop wallet (loopback RPC): [additionblockchain.com/download/](https://additionblockchain.com/download/)

---

## Networks

| | Public testnet | Separate `--mainnet` chain |
|---|---|---|
| Flag | `additiond --network testnet` | `additiond --mainnet` |
| `network_id` | `ADDITION_TESTNET_V1` | `ADDITION_MAINNET_V1` |
| Genesis | [`genesis.json`](genesis.json) | [`genesis-mainnet.json`](genesis-mainnet.json) |
| Public product | [additionblockchain.com](https://additionblockchain.com) and [rpc.additionblockchain.com](https://rpc.additionblockchain.com) | Not the website. Operator public-read: `http://34.27.30.115:38546/rpc?cmd=getinfo` (`network=mainnet`, `height=0`) |

`--mainnet` is its own chain, not a label flip on the testnet. It is not a live public mainnet product.

See [docs/MAINNET_RUNBOOK.md](docs/MAINNET_RUNBOOK.md).

---

## Architecture

What `main` actually ships:

- **Hash / PoW** — SHA3-512 of the header on testnet; `memory_hard` on `--mainnet`
- **Signatures** — ML-DSA-87 (FIPS 204) in `pq_mode=strict`
- **Addresses** — hash-committed: `SHA3-512(scheme_id || 0x00 || pubkey)` (128 hex)
- **Privacy** — SHA3-512 commitment + nullifier **opening** (`privacy_mint_open` / `privacy_spend_open`). A hash relation, not ZK
- **SLH-DSA** — opt-in vault (`slh-dsa-shake-256s`) when this liboqs can sign with a non-empty context; otherwise disabled
- **RPC** — write on `127.0.0.1`; public read is an allowlist (`getinfo`, `getblockraw`, …). Public `mine` / `createwallet` stay disabled

---

## Units

Whole integers. No 8-decimal subunit.

| | Value |
|---|---|
| Block reward | `50` |
| `max_supply` | `50000000` |
| Minimum fee | `1` |

---

## Build

CMake 3.20+, a C++20 compiler, OpenSSL, and [liboqs](https://github.com/open-quantum-safe/liboqs).

```bash
sudo apt-get install -y cmake g++ ninja-build libssl-dev git
git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git
cmake -S liboqs -B liboqs/build -GNinja -DOQS_USE_OPENSSL=ON -DBUILD_SHARED_LIBS=ON
cmake --build liboqs/build --target install
sudo ldconfig

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target additiond
./build/additiond --network testnet
```

```bash
cmake -S . -B build -DADDITION_BUILD_TESTS=ON
cmake --build build --target test_chain
ctest --test-dir build --output-on-failure
```

---

## Docs

| | |
|---|---|
| Join (HTML) | [additionblockchain.com/join/](https://additionblockchain.com/join/) |
| Join (raw) | [additionblockchain.com/join.md](https://additionblockchain.com/join.md) |
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

**License:** [MIT](LICENSE) · Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)
