# ADDITION iOS wallet

Native Swift / SwiftUI project for a research testnet / local-node ADDITION wallet.

This is not a hosted web wallet. It is not a multi-chain browser. It talks to ADDITION RPC only.

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

Screens: Home, Receive, Send, Activity. Node settings sit behind the gear on Home. Brand images are the website files (`logo-transparent.png`, `apple-touch-icon.png`, `og.png`, `favicon-32.png`) copied into the asset catalog.

## What it does

On a reachable ADDITION node you control:

- `createwallet <name>` — ML-DSA-87 keys stay in the node `data/wallets/` store
- `wallet_info` / receive — 128-hex hash-committed address
- `wallet_balance` / `getbalance` — whole ADD units from write RPC
- `wallet_send <name> <to> <amount> [fee]` — node signs; no private key on the wire

If write RPC is down or returns `error:`, the app shows **RPC offline** / the node error. It does not invent a balance or height.

## Write RPC vs public read

| Path | Allowed commands | Default |
|---|---|---|
| Write (wallet) | `createwallet`, `wallet_*`, `getbalance`, `fee_info`, `getinfo` | `127.0.0.1:8545` TEXT RPC |
| Public read | `getinfo`, `getblock`, `getblockraw` only | optional `https://rpc.additionblockchain.com/rpc` |

Write RPC accepts loopback (`127.0.0.1`, `localhost`, `::1`) or a LAN host you set. It refuses public operator hosts (`rpc.additionblockchain.com`, `34.27.30.115`, and other non-LAN addresses). Do not bind write RPC on a public interface. Do not treat public read as a send/createwallet backend.

On the iOS Simulator, `127.0.0.1:8545` is the Mac that already runs `additiond --network testnet`. On a phone, open the Home gear and point write RPC at your own loopback or LAN node (and token, if you set `ADDITION_RPC_TOKEN`).

Static layout previews (same brand files, fail-closed empty state): `ios/previews/`.

## Build on a Mac

Linux compile notes for `additiond` stay in the repository root README. Do not compile the node on iOS.

1. Install Xcode 15+ on macOS.
2. Open `ios/AdditionWallet/AdditionWallet.xcodeproj`.
3. Select an iPhone simulator.
4. Set your Apple development team if you want to run on a device.
5. Product → Run.

Client unit tests: Product → Test (scheme `AdditionWallet`).

Linux (this repo's usual CI host) cannot compile the Xcode target. Run the same client rules here:

```bash
python3 tests/test_ios_wallet_client.py
```

`ios/addition_ios_client.py` is the Python oracle for those rules. Keep it aligned with `ios/AdditionWallet/AdditionWallet/Client/`.

## Amounts and addresses

- Whole ADD units. No 8-decimal subunit.
- Address = `SHA3-512(scheme_id || 0x00 || pubkey_bytes)` (128 hex). Default scheme `ml-dsa-87`.
- Wallet names: 1–64 letters, digits, `_` or `-` (same as `additiond`).

## Out of scope

No public write API, no site download link, no Windows package, and no second-chain support.
