# ADDITION — research prototype / testnet

![License](https://img.shields.io/badge/license-MIT-blue?style=for-the-badge)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue?style=for-the-badge)

**ADDITION** is a research prototype for a post-quantum Layer 1 experiment (Dilithium / ML-DSA + SHA3-512). Default network: `additiond --network testnet`. It ships a **testnet**, not a live mainnet.

This repository does **not** claim production status, public node counts, a token sale, CoinMarketCap listing, or a live chain. CI badges are omitted until a green pipeline exists.

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

---

## Cashiers / Windows

Cashiers and Windows users download the testnet / local wallet from [https://additionblockchain.com/download/](https://additionblockchain.com/download/) only. Do not compile liboqs. There is no Windows compile path in this README.

The helper talks to write RPC on `127.0.0.1:8545` on the machine that already runs `additiond --network testnet`. additionblockchain.com has no public write RPC.

---

## Join the live testnet

Build `additiond` on Linux from this repository (`main`), then:

```bash
additiond --network testnet --data-dir $HOME/addition-testnet --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545
```

Type `sync` on the daemon stdin (or send it to write RPC on `127.0.0.1`). Height should move. `addpeer` after `--bootstrap` is `invalid/duplicate`.

`sync` tries public-read HTTP `:80` first, then `:38545`, then P2P `HELLO`. `:80` is the reliable join path when `38545` or `28545` is filtered or times out. Do not claim public P2P 28545 always works. The seed operator sets `ADDITION_ADVERTISED_P2P=34.27.30.115:28545` so public `getinfo` / `peers` do not list `self`.

Public read is `/rpc?cmd=getinfo` (not `/getinfo`):

```bash
curl 'https://rpc.additionblockchain.com/rpc?cmd=getinfo'
curl 'http://34.27.30.115/rpc?cmd=getinfo'
curl 'http://34.27.30.115:38545/rpc?cmd=getinfo'
```

`getblockraw` is on the public allowlist (`ok:BLKDATA`). Public `mine` / `createwallet` return `error: command disabled on public RPC`. Write RPC stays `127.0.0.1`. There is no public wallet, token, or NFT UI.

---

## Research goals (design targets)

These are aims of the testnet / research prototype, not proof of a live public network:

1. **Quantum** — ML-DSA-87 (FIPS 204) signatures, `pq_mode=strict` when `getinfo` answers. Opt-in `slh-dsa-shake-256s` only if this liboqs can `OQS_SIG_sign_with_ctx_str` with a non-empty context; otherwise the scheme stays disabled. No Falcon/FN-DSA. No FIPS 140-3 claim. This does not make the chain hash-based.
2. **Privacy** — SHA3-512 commitment + nullifier **opening** (`privacy_mint_open` / `privacy_spend_open`). This is a real hash relation, not a circuit. `privacy_*_zk` remains an ML-DSA wrap of a mint/spend string. Not Groth16, not Bulletproofs, not ZK-Shield.
3. **Speed** — local testnet RPC; publish only real `getinfo` fields from a running node
4. **Cost of transaction** — spec `min_fee=1`; no invented USD fees or gas market
5. **Compatibility** — in-process `bridge_*` commands and the EVM bootstrap (`web/evm/evm_rpc_bridge.py`). Bootstrap / not a full EVM, not live Uniswap, not “connected to every chain today”

---

## What this is

* A local / research **testnet** named `addition-testnet`
* Default mode is **testnet** (`additiond --network testnet`)
* `--mainnet` / `--network mainnet` starts a **separate** chain (`ADDITION_MAINNET_V1`, `genesis-mainnet.json`, `data-mainnet`). That chain is not public and is **not** a live network. The site stays on the testnet. See [docs/MAINNET_RUNBOOK.md](docs/MAINNET_RUNBOOK.md).

Checked-in network files:

* [`config.toml`](config.toml) / [`genesis.json`](genesis.json) — testnet
* [`config-mainnet.toml`](config-mainnet.toml) / [`genesis-mainnet.json`](genesis-mainnet.json) — separate mainnet chain (not live)

`bootstrap_peers` is IPv4 `ip:port` only. The operator’s current public P2P is `34.27.30.115:28545`. That is one real endpoint, not a peer-count claim. Local two-node runs still use `--bootstrap 127.0.0.1:28545`. Write RPC stays `127.0.0.1:8545`.

---

## Build on Linux (Ubuntu EliteDesk)

Compile path is **Linux only** (Ubuntu on the EliteDesk). `chmod`, `sudo`, `apt`, and `cmake` are Linux commands. Do not paste them into PowerShell. There is no Windows compile path and no MSVC recipe.

Cashiers and Windows users skip this section and use [https://additionblockchain.com/download/](https://additionblockchain.com/download/).

### Prerequisites

* CMake 3.20+
* A C++20 compiler (`g++` on Ubuntu)
* **OpenSSL** development headers and libraries (`libssl-dev`)
* **liboqs** (Open Quantum Safe) installed so CMake can find `oqs/oqs.h` and `liboqs`

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ ninja-build libssl-dev git
git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git
cmake -S liboqs -B liboqs/build -GNinja -DOQS_USE_OPENSSL=ON -DBUILD_SHARED_LIBS=ON
cmake --build liboqs/build --target install
sudo ldconfig
```

### Configure and compile

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
./build/additiond --network testnet --data-dir $HOME/addition-testnet
```

Equivalent:

```bash
./build/additiond --config config.toml --genesis genesis.json --data-dir $HOME/addition-testnet
```

The daemon reads commands on stdin. In another terminal:

```bash
printf 'getinfo\n' | nc 127.0.0.1 8545
```

`getinfo` reports `network=testnet` and `network_name=addition-testnet`.

`--data-dir` (use `$HOME/addition-testnet`) holds the chain, not just identity and wallets. After each accepted block the node writes `blocks.dat` (text block index: headers + transactions). UTXOs are rebuilt by replaying that file on startup. A clean process restart with the same `--data-dir` keeps `getinfo` height and `getblock` hashes. Wallets stay in `$HOME/addition-testnet/wallets/`.

### Local wallet (Bitcoin-like user model)

Trusted RPC only (`127.0.0.1:8545`). `createwallet alice` generates keys (default ML-DSA-87) and writes them to `$HOME/addition-testnet/wallets/alice.wal` (owner-only). The reply has `priv_printed=0`. This is keys / UTXOs / send / receive / fee — not BIP compatibility and not a Bitcoin fork.

```bash
printf 'createwallet alice\n' | nc 127.0.0.1 8545
printf 'wallet_info alice\n' | nc 127.0.0.1 8545
printf 'getbalance alice\n' | nc 127.0.0.1 8545
printf 'wallet_send alice bob 10 1\n' | nc 127.0.0.1 8545
```

`wallet_send` signs on the node from the local file. The explicit path is `tx_build` + `wallet_sign` + `sendtx_signed` (still no raw privkey on the wire). Legacy `sendtx` needs `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1`.

Trusted RPC can also classify operator-supplied Bitcoin address samples and emit a signed ADDITION receipt (`hygiene_classify` / `hygiene_attest` / `hygiene_verify`). That path is an attestation rehearsal: `moves_bitcoin=0`, `claim=attestation_not_bip360`. It does not move Bitcoin and is not BIP-360. See [docs/BTC_HYGIENE.md](docs/BTC_HYGIENE.md).

Local UI:

* Page: `/wallet/` via `python3 web/serve.py` (loopback `/local-rpc` only)
* Desktop: `python3 web/addition_wallet_gui.py` or `--cli getinfo`
* Packaged binary: live files on [https://additionblockchain.com/download/](https://additionblockchain.com/download/) (`addition-wallet-testnet --cli getinfo`)

A fresh wallet balance is `0` until a block is mined to that address (testnet PoW is SHA3-512 of the header, 30s deadline, reward 50 on a fresh testnet) or a UTXO arrives. `mine alice` is trusted local RPC only. Optional in-process auto-mine (`--auto-mine`, off by default, testnet only) mines one block every N seconds and writes `blocks.dat` the same way as a manual mine. Public RPC cannot mine, create wallets, or send. See [docs/REAL_TESTNET_MINE_AND_PRIVACY.md](docs/REAL_TESTNET_MINE_AND_PRIVACY.md).

Standalone CLI that keeps keys on the **caller** disk (not `$HOME/addition-testnet/wallets/`) and signs ML-DSA-87 locally before `sendtx_signed_hash`:

```bash
python3 web/addition_wallet.py createwallet
python3 web/addition_wallet.py mine
python3 web/addition_wallet.py balance
python3 web/addition_wallet.py send bob 10
```

See [docs/WALLET.md](docs/WALLET.md).

### Public read-only RPC

Local RPC on `127.0.0.1:8545` stays trusted (mine, wallets, sends). To expose a **read-only** public bind:

```bash
./build/additiond --network testnet --data-dir $HOME/addition-testnet --public-rpc
# equivalent: ADDITION_ENABLE_PUBLIC_RPC=1 ./build/additiond --network testnet --data-dir $HOME/addition-testnet
```

Default public bind is `0.0.0.0:38545`. Allowlist only:

`getinfo`, `monetary_info`, `crypto_selftest`, `tx_status`, `peers`, `getblock`, `getblockhash`, `getblockraw`

```bash
printf 'getinfo\n' | nc 127.0.0.1 38545
curl 'http://127.0.0.1:38545/rpc?cmd=getinfo'
curl 'http://127.0.0.1:38545/jsonrpc?method=getinfo'
printf 'mine\n' | nc 127.0.0.1 38545   # error: command disabled on public RPC
```

Public-read JSON API (same allowlist, no writes): `GET /jsonrpc?method=getinfo` and `POST /jsonrpc` with JSON-RPC 2.0. Join path: `--bootstrap 34.27.30.115:28545`; `sync` uses HTTP `:80` then `:38545`.

LAN RPC (`18545`) still requires `ADDITION_ENABLE_LAN_RPC=1` and `ADDITION_LAN_RPC_TOKEN`. P2P stays off unless `ADDITION_ENABLE_P2P_RPC=1`. Do not open unauthenticated write RPC to the world.

Config keys: `enable_public_rpc`, `ports.public_rpc`, `ports.public_rpc_bind`, `enable_auto_mine`, `auto_mine_interval_sec`, `auto_mine_reward`, `bootstrap_peers`. Second local node: `--local-rpc-port 8546 --p2p-port 28546 --bootstrap 127.0.0.1:28545`. To join the operator testnet: `--bootstrap 34.27.30.115:28545` (IPv4 only; P2P stays off unless `ADDITION_ENABLE_P2P_RPC=1`). See [docs/TWO_NODE_TESTNET.md](docs/TWO_NODE_TESTNET.md).

### Testnet auto-mine (off by default)

The chain only grows when something mines. That can be a human/`mine` on localhost **or** an optional in-process timer:

```bash
./build/additiond --network testnet --data-dir $HOME/addition-testnet --auto-mine --auto-mine-interval 60 --auto-mine-reward miner1
# equivalent: ADDITION_AUTO_MINE=1 ADDITION_AUTO_MINE_INTERVAL=60 ./build/additiond --network testnet --data-dir $HOME/addition-testnet
```

Auto-mine is refused on `--network mainnet`. It is not a public RPC command (`mine` stays rejected on port 38545). Each accepted block is persisted to `$HOME/addition-testnet/blocks.dat` as today.

### Website (static Pages)

`web/public/` is a complete static site (explorer, RPC how-to, mirrored docs, white paper, legal notice). Publish that folder as the Pages root (for example additionblockchain.com). GitHub: [ADDAddition/ADDITION](https://github.com/ADDAddition/ADDITION).

```bash
python3 web/serve.py
```

| Path | Content |
|------|---------|
| `/` | Block explorer: search + latest `getblock` rows, or **RPC offline** |
| `/join/` | Run `additiond` from `main`, bootstrap `34.27.30.115:28545`, then `sync` |
| `/explorer/` | Redirects to `/` |
| `/status/` | Live `getinfo`, or **RPC offline** |
| `/rpc/` | Public read `/rpc?cmd=getinfo` on :80 and :38545 |
| `/docs/` | Architecture, commands, PoUW spec, getting started, runbook, SHA3 opening notes |
| `/wallet/` | Local createwallet / UTXO send via `/local-rpc` (loopback) |
| `/download/` | Testnet / local wallet binaries + Linux run steps |
| `/contracts/` `/swap/` `/evm/` | Local node methods only; EVM is bootstrap |
| `/whitepaper/` `/legal/` | Research copy. No fake ticker or live mainnet |

Explorer/status call `/api/rpc`. On a static host without a backend they show **RPC offline**. Optional `?rpc=http://HOST:38545/rpc`.

The Pages worker (`web/public/wrangler.toml`) ships `PUBLIC_RPC_HTTP = ""`. Leave it empty so a down node shows **RPC offline**. An operator who runs `--public-rpc` sets `PUBLIC_RPC_HTTP` to that process’s real HTTP URL (for example `http://127.0.0.1:38545/rpc`). Do not commit trycloudflare or other ephemeral URLs.

`/wallet`, `/contracts`, and `/swap` use loopback `/local-rpc` → `127.0.0.1:8545`. They print the node’s real reply (`error: pool not found` if you have no pool).

```bash
python3 web/evm/evm_rpc_bridge.py
# 127.0.0.1:9545 only, chainId 424242, eth_sendRawTransaction disabled
```

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com).

Help:

```bash
./build/additiond --help
```

`--mainnet` is a separate local/operator chain, not a live public network. Do not set `ADDITION_MAINNET_MODE=1` on the public testnet unit.

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

Two local processes: `./scripts/start_two_node_testnet.sh` (A: write `8545` / public `38545` / P2P `28545`; B: write `8546` / P2P `28546`). Details in [docs/TWO_NODE_TESTNET.md](docs/TWO_NODE_TESTNET.md).

---

## Documentation

* [Architecture notes](docs/ARCHITECTURE.md)
* [Command reference](docs/FINAL_COMMANDS.md)
* [Local testnet wallet](docs/WALLET.md)
* [PoUW phase 1 spec](docs/POUW_PHASE1_SPEC.md)
* [Two-node local testnet](docs/TWO_NODE_TESTNET.md)
* [Mined block + SHA3 opening privacy](docs/REAL_TESTNET_MINE_AND_PRIVACY.md)
* [Testnet public-read RPC (systemd)](docs/TESTNET_PUBLIC_RPC_RUNBOOK.md)
* [Mainnet node (separate chain, not live)](docs/MAINNET_RUNBOOK.md)
* Public raw markdown (no HTML scrape): [`/join.md`](web/public/join.md), [`/docs/testnet-rpc-runbook.md`](docs/TESTNET_PUBLIC_RPC_RUNBOOK.md), [`/docs/wallet.md`](docs/WALLET.md)

The public site and `rpc.additionblockchain.com` are the testnet. `--mainnet` does not change that.

---

## Contributing

1. Fork the repository
2. Create a feature branch
3. Keep secrets out of git (`build/`, `data/`, `.wrangler/`, `*.dat`, keys)
4. Open a pull request

**License:** MIT
