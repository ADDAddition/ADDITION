# Addition Core (Flutter desktop)

Bitcoin Core-style full-node desktop GUI for **ADDITION** /
`ADDITION_MAINNET_V1`. One window for operators: wallet, receive, send, mine,
peers, and a TEXT RPC console. Talks to a local `additiond` over **loopback
write RPC** (`127.0.0.1:8546` by default). Keys stay in the node
`data/wallets/<name>.wal` store via `createwallet` / `wallet_send`. This app
never prints or exports private keys.

Not SmartChain. Not a DEX. Not a hosted custodial wallet.

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

## Nav (real destinations)

| Nav | Role | RPC |
|---|---|---|
| Wallet / balance | Create / load / list wallets; live getinfo stats | `createwallet`, `wallet_*`, `getinfo` |
| Receive | Address + copy | `wallet_info` / `wallet_balance` |
| Send | Loopback `wallet_send` only | `wallet_send` |
| Mine | Local trusted mine; coinbase 50 ADD (100% finder) | `mine` |
| Peers | Honest peer count/list from the node | `getinfo` / `peers` |
| Console | Issue one TEXT RPC line to local node | any safe TEXT command |

Height / peers / `last_tps` / `next_reward` come from live `getinfo` only —
never invented. Public read (`/api/rpc` → `34.27.30.115:38546`) is status-only;
`wallet_send` stays off the public port.

## Banners (desktop chrome only)

Muted looping banner band in the Addition Core window — one durable live MP4
at a time, rotating the two official URLs. Not dual autoplay tiles, and not
wired into the public website hero.

- https://additionblockchain.com/banners/addition-banner-1.mp4
- https://additionblockchain.com/banners/addition-banner-2.mp4

Poster frames ship as assets for first paint / offline fallback. No invented
MP4 paths and no committed video binaries.

## RPC honesty

- Default local write: `127.0.0.1:8546` (loopback only; also `::1` / `localhost`)
- Refuse non-loopback send (including `0.0.0.0` and public seed IPs)
- Do not bind write RPC to `0.0.0.0:8545`
- Public read remains `34.27.30.115:38546` / site `/api/rpc`

## Prerequisites

1. Flutter stable (3.35+ / Dart 3.9+) with desktop enabled:
   - Linux: `flutter config --enable-linux-desktop`
2. A local `additiond` listening for write RPC on loopback, e.g.:

```bash
./build/additiond --mainnet
# or testnet on :8545 — change Host:port in Node settings
```

Linux desktop build needs GTK/CMake tooling (`clang`, `cmake`, `ninja-build`,
`pkg-config`, `libgtk-3-dev`). Video banners need a working `video_player`
backend (GStreamer on Linux).

## Run (development)

From this directory (`client/addition_app`):

```bash
flutter pub get
flutter run -d linux
```

## Build release binary (Linux first-class)

```bash
flutter pub get
flutter build linux --release
```

Binary (typical path):

```text
build/linux/x64/release/bundle/addition_core
```

This host/CI path publishes the Linux bundle. Do not claim a Windows `.exe`
unless that package was actually produced.

## Tests

```bash
flutter test
python3 scripts/rpc_smoke.py   # needs a live local additiond
```

## Out of scope

SmartChain, fake ZK, fake TPS, fake DEX / Swap-to-BTC / Solidity IDE, public
send, binding `0.0.0.0:8545`, and store shipping claims without real packages.
