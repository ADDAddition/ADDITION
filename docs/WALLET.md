# ADDITION wallet

Public product: **ADDITION_MAINNET_V1**. Anyone can sync, mine, and use wallet
RPCs against the public seed, or run a home node with loopback write.

Public contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

## Networks

| | Public mainnet | Research testnet (secondary) |
|---|---|---|
| Flag | `additiond --mainnet` | `additiond --network testnet` |
| `network_id` | `ADDITION_MAINNET_V1` | `ADDITION_TESTNET_V1` |
| Home-node write RPC | `127.0.0.1:8546` | `127.0.0.1:8545` |
| Public RPC | `34.27.30.115:38546` | `34.27.30.115:38545` |
| P2P bootstrap | `34.27.30.115:28546` | `34.27.30.115:28545` |

Public seed **38546** write allowlist is open (CoS): `createwallet`, `mine`,
`wallet_send`, `wallet_sign` / `sign_message`, `tx_build` / `sendtx_signed*`.
Site `/wallet/` prefers `/api/rpc` → that public path; falls back to
`/local-rpc` → loopback when public RPC is offline. Height from live `getinfo`
may still be `0` — do not invent TPS or a USD ticker.

Never bind home-node write RPC to `0.0.0.0`. Never publish `8545` / `8546`.

## What already exists on the daemon

Trusted TEXT RPC (one command line in, one line out), **not JSON-RPC**.

Relevant commands (see [FINAL_COMMANDS.md](FINAL_COMMANDS.md)):

| Command | Role |
|---|---|
| `createwallet [name] [scheme]` | Default scheme `ml-dsa-87`. Optional `slh-dsa-shake-256s` only if this liboqs can `OQS_SIG_sign_with_ctx_str` with a non-empty context; otherwise rejected in `pq_mode=strict`. Unknown schemes (including Falcon/FN-DSA) are rejected. Writes `data/wallets/<name>.wal`. Returns `address`, `pub`, `algo=...`, `priv_printed=0`. |
| `wallet_send <name> <to> <amount> [fee]` | Node signs from that `.wal` file. No privkey on the wire. |
| `getbalance <address>` | Confirmed balance |
| `fee_info` | `recommended_min_fee` (floor `0`; congestion can raise it) |
| `tx_build <from> <pubkey_hex> <to> <amount> <fee> <nonce>` | Builds the unsigned spend and returns `sign_hash=...` |
| `sendtx_signed_hash ... <sig_hex_without_pq_prefix>` | Submits a PQ signature. No private key argument. |
| `hygiene_classify [path]` | Offline Bitcoin script hygiene over operator samples / `fixtures/btc_hygiene_samples.json`. Does not move Bitcoin. Not BIP-360. Trusted RPC only. |
| `hygiene_attest <wallet> <btc_addr> <height> <class> [reuse] [pubkey_on_chain]` | Signed ADDITION receipt (`ADDITION-HYGIENE-REHEARSAL`, `moves_bitcoin=0`, `claim=attestation_not_bip360`). Attestation rehearsal, not a consensus change. |
| `hygiene_verify <receipt_note>` | Verify the signed receipt. A mutated note is rejected. |
| `mine <address>` | Mainnet: memory_hard, no 30s deadline, coinbase 50. Testnet: SHA3-512, 30s deadline. |
| `getinfo` | `network`, `network_id`, `height` (may be `0`), `peers`, `pq_mode=strict`, `allowed_sig_algs`, `max_supply=50000000` |

Legacy `sendtx` / `sendtx_hash` (private key on the RPC line) stay **disabled**
unless `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1`. Leave that unset.

`sign_message <privkey_hex> ...` still exists on the daemon. The standalone
client does **not** call it.

The site `/wallet/` page uses `wallet_send` against public `38546` when
available (via `/api/rpc`), or `data/wallets/` on a home node via `/local-rpc`.
This document also covers the **caller-disk** CLI: keys never enter the daemon
wallet store.

## Standalone CLI (keys on the caller disk)

File: [`web/addition_wallet.py`](../web/addition_wallet.py)

Keys are generated **locally** with liboqs ML-DSA-87, using the same address
formula as the node:

```text
address = sha3_512(scheme_id || 0x00 || pubkey_bytes)   # 128 hex; default scheme_id = "ml-dsa-87"
```

The spend path recomputes that hash from the revealed pubkey. A pubkey that
does not commit to the address is rejected. The raw 2592-byte ML-DSA-87 key
is not the address.

The secret is written to a gitignored file (default `data/addition.wallet`,
mode `0600`) and is used only in-process to sign `sign_hash`.

### Prerequisites

* A reachable TEXT RPC: home node `127.0.0.1:8546` (mainnet) / `8545` (testnet), or public `34.27.30.115:38546` when write is open
* Python 3.10+
* `liboqs` on the library path (the same library used to build `additiond`)

If CMake cannot find liboqs, the daemon will not build. The wallet client loads
`liboqs` via ctypes (`ADDITION_LIBOQS` overrides the path). The packaged desktop
helper still refuses non-loopback RPC hosts (self-custody on your disk).

### Exact commands

From the repository root, with a mainnet home node on `127.0.0.1:8546`:

```bash
# 1. Create a local address (keys stay on disk; priv_printed=0)
python3 web/addition_wallet.py --rpc-port 8546 createwallet

# 2. Confirm the node is public mainnet
python3 web/addition_wallet.py --rpc-port 8546 getinfo
# expect: network=mainnet  network_id=ADDITION_MAINNET_V1  pq_mode=strict

# 3. Optional: mine a coinbase to that address
python3 web/addition_wallet.py --rpc-port 8546 mine
python3 web/addition_wallet.py --rpc-port 8546 balance

# 4. Send: tx_build on the node, ML-DSA-87 sign locally, sendtx_signed_hash
python3 web/addition_wallet.py --rpc-port 8546 send <to_address> <amount>
# fee defaults to fee_info.recommended_min_fee (0 when the mempool is empty)
```

Research testnet (secondary): use `--rpc-port 8545` and
`additiond --network testnet`.

Optional flags:

```bash
python3 web/addition_wallet.py --wallet data/addition.wallet \
  --rpc-host 127.0.0.1 --rpc-port 8546 \
  createwallet

# If ADDITION_RPC_TOKEN is set on the daemon, export the same value
# or pass --rpc-token.
```

### Spend path (what hits the socket)

```text
tx_build <from> <pub> <to> <amount> <fee> <nonce>
        -> sign_hash=<hex>

local ML-DSA-87 sign of the UTF-8 bytes of that hex string
        -> sig_hex  (no pq= prefix, no private key)

sendtx_signed_hash <from> <pub> <to> <amount> <fee> <nonce> <sig_hex>
        -> tx hash
```

The client refuses to emit `sendtx`, `sendtx_hash`, or `sign_message`.

## Packaged desktop binary (mainnet / local)

```bash
./scripts/build_wallet.sh
./web/public/download/addition-wallet-mainnet --cli getinfo
```

Windows: `powershell -File scripts\build_wallet.ps1`. Details in
[`packaging/README.md`](../packaging/README.md). The public `/download/` page
links those files as mainnet helpers (default write `127.0.0.1:8546`). The
desktop helper refuses non-loopback RPC hosts. The in-browser `/wallet/` page
prefers public `38546` write via `/api/rpc` when available.

### Tests without a running daemon

```bash
python3 tests/test_wallet_client.py
python3 tests/test_wallet_packaging.py
```

These tests mock the TEXT RPC and fail if a private key appears on the wire.

### Out of scope

This path does not add ZK-Shield, a DEX, tokens, EVM/MetaMask, wallet-connect,
or a NiceHash pool. Brand is ADDITION only.
