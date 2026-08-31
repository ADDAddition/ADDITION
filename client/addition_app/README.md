# ADDITION desktop wallet (Flutter)

Local/testnet desktop wallet for Windows and Linux. Talks **TEXT write RPC** on
loopback (`127.0.0.1:8545` by default). Keys stay in the node
`data/wallets/<name>.wal` store via `createwallet` / `wallet_send`. This app
never prints or exports private keys.

Public product today is **testnet**. If `getinfo` reports mainnet, the UI labels
it as local/operator only — not a live public network. No token sale, no
multi-chain browser.

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

## What it does

| Action | RPC |
|---|---|
| Create / list / load wallet | `createwallet`, `wallet_list`, `wallet_info` |
| Balance / address | `wallet_balance` |
| Send | `wallet_send` (node signs) |
| Node status | `getinfo` (height, network, peers) |
| Optional public status | `GET https://rpc.additionblockchain.com/rpc?cmd=getinfo` (read-only) |

Write RPC is **loopback-only**. Public read cannot create wallets or send.

## Prerequisites

1. Flutter stable (3.35+ / Dart 3.9+) with desktop enabled:
   - Linux: `flutter config --enable-linux-desktop`
   - Windows: `flutter config --enable-windows-desktop`
2. A local `additiond` listening for write RPC on `127.0.0.1:8545`, e.g.:

```bash
./build/additiond --network testnet
```

Linux desktop build also needs GTK/CMake tooling (`clang`, `cmake`, `ninja-build`,
`pkg-config`, `libgtk-3-dev`).

## Run (development)

From this directory (`client/addition_app`):

### Linux

```bash
flutter pub get
flutter run -d linux
```

### Windows

```powershell
flutter pub get
flutter run -d windows
```

## Build release binaries

### Linux

```bash
flutter pub get
flutter build linux --release
```

Binary (typical path):

```text
build/linux/x64/release/bundle/addition_wallet
```

### Windows

```powershell
flutter pub get
flutter build windows --release
```

Binary (typical path):

```text
build\windows\x64\runner\Release\addition_wallet.exe
```

## First-run helper scripts

From the repo root (keeps write RPC on loopback; does not change the #55 wallet UI):

```bash
./scripts/setup_desktop.sh --mode testnet --run-wallet
./scripts/setup_desktop.sh --start-only --mode testnet
./scripts/setup_desktop.sh --stop --mode testnet
```

Windows: start the node in WSL via `setup_desktop.sh`, then
`powershell -ExecutionPolicy Bypass -File scripts\setup_desktop.ps1 -Mode testnet -RunWallet`.

Optional loopback tools:
- EVM bridge: `python3 web/evm/evm_rpc_bridge.py` (`127.0.0.1:9545`, send disabled)
- Local mining pool: `python3 tools/mining_pool.py coordinator` (not NiceHash; refuses public/LAN mine ports)

`lib/rpc/evm_jsonrpc_client.dart` is a loopback client for that EVM bridge (additive; not wired into a separate Status wizard).

## Tests

```bash
flutter test
python3 scripts/rpc_smoke.py   # needs a live local additiond
```

Unit tests cover write-RPC loopback policy, command builders, getinfo network
labels, and refuse private-key / insecure-command paths. They do not invent
balances or claim a live mainnet.

## Out of scope

DEX/swap UI, token sale, public write RPC, BIP wallets, and exporting private
keys. No #56 Status/first-run wizard overlay — the #55 wallet screen remains
the product UI.
