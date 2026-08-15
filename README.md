# ADDITION — research prototype / testnet

![License](https://img.shields.io/badge/license-MIT-blue?style=for-the-badge)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue?style=for-the-badge)

**ADDITION** is a research prototype for a post-quantum Layer 1 experiment (Dilithium / ML-DSA + SHA3-512). It ships an **honest testnet**, not a live mainnet.

This repository does **not** claim production status, public node counts, a token sale, CoinMarketCap listing, or a live chain. CI badges are omitted until a green pipeline exists.

Contact: [labjay69@gmail.com](mailto:labjay69@gmail.com)

---

## What this is

* A local / research **testnet** named `addition-testnet`
* Default mode is **testnet** (`additiond --network testnet`)
* Mainnet profile is opt-in only (`--network mainnet` or an explicit `ADDITION_MAINNET_MODE=1`). That profile is still experimental and is **not** a live public network.

Checked-in network files:

* [`config.toml`](config.toml) — network name, ports, reward, localhost bootstrap examples
* [`genesis.json`](genesis.json) — testnet genesis parameters (no fake node counts)

`bootstrap_peers` lists localhost examples only (`127.0.0.1:28545`) until a second real node exists.

---

## Build (liboqs + OpenSSL)

### Prerequisites

* CMake 3.20+
* A C++20 compiler (GCC, Clang, or MSVC)
* **OpenSSL** development headers and libraries (`libssl-dev` on Debian/Ubuntu)
* **liboqs** (Open Quantum Safe) installed so CMake can find `oqs/oqs.h` and `liboqs`

#### Debian / Ubuntu

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ ninja-build libssl-dev git
git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git
cmake -S liboqs -B liboqs/build -GNinja -DOQS_USE_OPENSSL=ON -DBUILD_SHARED_LIBS=ON
cmake --build liboqs/build --target install
sudo ldconfig
```

#### Configure and compile

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target additiond
```

If CMake cannot find liboqs:

```bash
cmake -S . -B build \
  -DOQS_INCLUDE_DIR=/usr/local/include \
  -DOQS_LIBRARY=/usr/local/lib/liboqs.so
```

### Tests

```bash
cmake -S . -B build -DADDITION_BUILD_TESTS=ON
cmake --build build --target test_chain
ctest --test-dir build --output-on-failure
```

---

## Run the testnet daemon

From the repository root (so `config.toml` and `genesis.json` are found):

```bash
./build/additiond --network testnet
```

Equivalent:

```bash
./build/additiond --config config.toml --genesis genesis.json
```

The daemon reads commands on stdin. In another terminal:

```bash
printf 'getinfo\n' | nc 127.0.0.1 8545
```

`getinfo` reports `network=testnet` and `network_name=addition-testnet`.

### Public read-only RPC

Local RPC on `127.0.0.1:8545` stays trusted (mine, wallets, sends). To expose a **read-only** public bind:

```bash
./build/additiond --network testnet --public-rpc
# equivalent: ADDITION_ENABLE_PUBLIC_RPC=1 ./build/additiond --network testnet
```

Default public bind is `0.0.0.0:38545`. Allowlist only:

`getinfo`, `monetary_info`, `crypto_selftest`, `tx_status`, `peers`, `getblock`, `getblockhash`

```bash
printf 'getinfo\n' | nc 127.0.0.1 38545
curl 'http://127.0.0.1:38545/rpc?cmd=getinfo'
printf 'mine\n' | nc 127.0.0.1 38545   # error: command disabled on public RPC
```

LAN RPC (`18545`) still requires `ADDITION_ENABLE_LAN_RPC=1` and `ADDITION_LAN_RPC_TOKEN`. P2P stays off unless `ADDITION_ENABLE_P2P_RPC=1`. Do not open unauthenticated write RPC to the world.

### Honest website (static Pages)

`web/public/` is a complete static site (explorer, RPC how-to, mirrored docs, white paper, legal notice). Publish that folder as the Pages root (for example additionblockchain.com). GitHub: [ADDAddition/ADDITION](https://github.com/ADDAddition/ADDITION).

```bash
python3 web/serve.py
```

| Path | Content |
|------|---------|
| `/` | Research testnet home + live `getinfo` or **RPC offline** |
| `/explorer/` | Block/tx lookup from the read RPC only |
| `/status/` | getinfo, monetary_info, selftest, peers |
| `/rpc/` | How to talk to the public allowlist |
| `/docs/` | Architecture, commands, PoUW spec, getting started, runbook, ZK contract |
| `/contracts/` `/swap/` `/evm/` | Only methods verified on a local node; EVM is bootstrap |
| `/whitepaper/` `/legal/` | Honest research copy. No fake ticker or mainnet live |

Explorer/status call `/api/rpc`. On a static host without a backend they fail closed. Optional `?rpc=http://HOST:38545/rpc`.

`/contracts` and `/swap` use loopback `/local-rpc` → `127.0.0.1:8545`. They print the node’s real reply (`error: pool not found` if you have no pool).

```bash
python3 web/evm/evm_rpc_bridge.py
```

Contact: [labjay69@gmail.com](mailto:labjay69@gmail.com).

Help:

```bash
./build/additiond --help
```

Do not set `ADDITION_MAINNET_MODE=1` unless you are explicitly experimenting with the mainnet *profile*. It is not a live network.

Private keys are not printed and must not be committed. `data/node_identity.dat` (public identity only) and wallet files stay local and gitignored.

---

## Ports (testnet defaults)

| Service        | Bind                | Port  |
|----------------|---------------------|-------|
| Local RPC      | `127.0.0.1`         | 8545  |
| Public read RPC | `0.0.0.0` (opt-in) | 38545 |
| LAN RPC        | disabled by default | 18545 |
| P2P            | disabled by default | 28545 |

Public read RPC stays off unless `--public-rpc` or `ADDITION_ENABLE_PUBLIC_RPC=1`. LAN/P2P stay off unless `ADDITION_ENABLE_LAN_RPC=1` / `ADDITION_ENABLE_P2P_RPC=1`.

---

## Documentation

* [Architecture notes](docs/ARCHITECTURE.md)
* [Command reference](docs/FINAL_COMMANDS.md)
* [PoUW phase 1 spec](docs/POUW_PHASE1_SPEC.md)

Older docs may still say “mainnet”. Treat those as historical. This tree is a research testnet.

---

## Contributing

1. Fork the repository
2. Create a feature branch
3. Keep secrets out of git (`build/`, `data/`, `.wrangler/`, `*.dat`, keys)
4. Open a pull request

**License:** MIT
