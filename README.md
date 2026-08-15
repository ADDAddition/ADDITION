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

Help:

```bash
./build/additiond --help
```

Do not set `ADDITION_MAINNET_MODE=1` unless you are explicitly experimenting with the mainnet *profile*. It is not a live network.

Private keys are not printed and must not be committed. `data/node_identity.dat` (public identity only) and wallet files stay local and gitignored.

---

## Ports (testnet defaults)

| Service   | Bind              | Port  |
|-----------|-------------------|-------|
| Local RPC | `127.0.0.1`       | 8545  |
| LAN RPC   | disabled by default | 18545 |
| P2P       | disabled by default | 28545 |

LAN/P2P stay off unless you set `ADDITION_ENABLE_LAN_RPC=1` / `ADDITION_ENABLE_P2P_RPC=1`.

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
