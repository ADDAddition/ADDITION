# Local testnet wallet

Research prototype / **testnet only**. This is not a live mainnet, not a token
sale, and not a hosted web wallet.

A stranger with `additiond --network testnet` running can create an address,
read a balance, and send a post-quantum signed transaction **without putting a
private key on the TEXT RPC socket**.

Public contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

## What already exists on the daemon

Trusted local TEXT RPC (one command line in, one line out), **not JSON-RPC**:

```text
127.0.0.1:8545
```

Relevant commands (see [FINAL_COMMANDS.md](FINAL_COMMANDS.md)):

| Command | Role |
|---|---|
| `createwallet [name] [scheme]` | Default scheme `ml-dsa-87`. Optional `slh-dsa-shake-256s` only if this liboqs can `OQS_SIG_sign_with_ctx_str` with a non-empty context; otherwise rejected in `pq_mode=strict`. Unknown schemes (including Falcon/FN-DSA) are rejected. Writes `data/wallets/<name>.wal`. Returns `address`, `pub`, `algo=...`, `priv_printed=0`. |
| `wallet_send <name> <to> <amount> [fee]` | Node signs from that `.wal` file. No privkey on the wire. |
| `getbalance <address>` | Confirmed balance |
| `fee_info` | `recommended_min_fee` (minimum `1`) |
| `tx_build <from> <pubkey_hex> <to> <amount> <fee> <nonce>` | Builds the unsigned spend and returns `sign_hash=...` |
| `sendtx_signed_hash ... <sig_hex_without_pq_prefix>` | Submits a PQ signature. No private key argument. |
| `hygiene_classify [path]` | Offline Bitcoin script hygiene over operator samples / `fixtures/btc_hygiene_samples.json`. Does not move Bitcoin. Not BIP-360. Trusted RPC only. |
| `hygiene_attest <wallet> <btc_addr> <height> <class> [reuse] [pubkey_on_chain]` | Signed ADDITION receipt (`ADDITION-HYGIENE-REHEARSAL`, `moves_bitcoin=0`, `claim=attestation_not_bip360`). Attestation rehearsal, not a consensus change. |
| `hygiene_verify <receipt_note>` | Verify the signed receipt. A mutated note is rejected. |
| `mine <address>` | Local trusted RPC. Testnet: SHA3-512, 30s deadline, coinbase 50. Mainnet profile: memory_hard, no 30s deadline (not a live public network). |
| `getinfo` | `network=testnet`, `height`, `peers`, `pq_mode=strict`, `allowed_sig_algs`, `max_supply=50000000` |

Legacy `sendtx` / `sendtx_hash` (private key on the RPC line) stay **disabled**
unless `ADDITION_ALLOW_INSECURE_TX_COMMANDS=1`. Leave that unset.

`sign_message <privkey_hex> ...` still exists on the daemon. The standalone
client does **not** call it.

The node GUI / `/wallet/` page uses `wallet_send` and `data/wallets/`. This
document is the **caller-disk** CLI: keys never enter the daemon wallet store.

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

* A running local daemon: `./build/additiond --network testnet`
* Python 3.10+
* `liboqs` on the library path (the same library used to build `additiond`)

If CMake cannot find liboqs, the daemon will not build. The wallet client loads
`liboqs` via ctypes (`ADDITION_LIBOQS` overrides the path).

### Exact commands

From the repository root, with the daemon already listening on `127.0.0.1:8545`:

```bash
# 1. Create a local address (keys stay on disk; priv_printed=0)
python3 web/addition_wallet.py createwallet

# 2. Confirm the node is the research testnet
python3 web/addition_wallet.py getinfo
# expect: network=testnet  pq_mode=strict  max_supply=50000000

# 3. Optional local demo: mine a coinbase to that address
python3 web/addition_wallet.py mine
python3 web/addition_wallet.py balance

# 4. Send: tx_build on the node, ML-DSA-87 sign locally, sendtx_signed_hash
python3 web/addition_wallet.py send <to_address> <amount>
# fee defaults to fee_info.recommended_min_fee (at least 1)
```

Optional flags:

```bash
python3 web/addition_wallet.py --wallet data/addition.wallet \
  --rpc-host 127.0.0.1 --rpc-port 8545 \
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

## Packaged desktop binary (testnet / local)

```bash
./scripts/build_wallet.sh
./web/public/download/addition-wallet-testnet --cli getinfo
```

Windows: `powershell -File scripts\build_wallet.ps1`. Details in
[`packaging/README.md`](../packaging/README.md). The public `/download/` page
links those files. RPC stays loopback-only.

### Tests without a running daemon

```bash
python3 tests/test_wallet_client.py
python3 tests/test_wallet_packaging.py
```

These tests mock the TEXT RPC and fail if a private key appears on the wire.

### Out of scope

This path does not add ZK-Shield, a DEX, tokens, EVM/MetaMask, a public
explorer, wallet-connect, a miner pool, or a live mainnet.
