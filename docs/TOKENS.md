# ADDITION tokens (research testnet)

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

This document describes the **token commands that already exist** on a local
`additiond` TEXT RPC (`127.0.0.1:8545`). It is not a token sale, not a live
mainnet, not an ERC-20 / Uniswap / public DEX, and not ZK-Shield.

## What works today

The daemon keeps an in-process `TokenEngine` (`src/token_engine.cpp`) and
exposes it as one-line TEXT commands (`src/rpc_server.cpp`). Verified local
commands:

| Command | Role | Notes |
|---|---|---|
| `token_create <symbol> <owner> <max_supply> <initial_mint>` | write | returns `ok` |
| `token_create_ex <symbol> <name> <owner> <max_supply> <initial_mint> <decimals> <burnable_0_1> <dev_wallet_or_dash> <dev_allocation>` | write | name has no spaces |
| `token_mint <symbol> <caller> <to> <amount>` | write | caller must be the token owner |
| `token_transfer <symbol> <from> <to> <amount>` | write | unsigned; local TEXT RPC only |
| `token_sign_payload <symbol> <from> <to> <amount>` | read | canonical string to sign |
| `token_transfer_signed <symbol> <from> <to> <amount> <pubkey> <sig>` | write | ML-DSA-87; `from` must bind the pubkey |
| `token_transfer_wallet <wallet> <symbol> <to> <amount>` | write | signs from `data/wallets/<name>.wal` |
| `token_balance <symbol> <owner>` | read | decimal string, `0` if missing |
| `token_info <symbol>` | read | `key=value` fields |
| `token_burn <symbol> <from> <amount>` | write | only if the token was created burnable |
| `nft_mint <collection> <token_id> <owner> <metadata>` | write | in-process NFT record |
| `nft_transfer <collection> <token_id> <from> <to>` | write | owner check only |
| `nft_owner <collection> <token_id>` | read | address or `error: nft not found` |
| `swap_pool_create <token_a> <token_b> <fee_bps>` | write | local TEXT RPC only; tokens must exist |
| `add_liquidity` / `swap_add_liquidity <token_a> <token_b> <provider> <amount_a> <amount_b>` | write | moves provider balances into pool reserves |
| `swap_exact_in <token_in> <token_out> <trader> <amount_in> <min_out>` | write | updates reserves; `ok:amount_out=N` |
| `swap_tvl` | read | sum of live pool reserves, or `tvl=0` |

State is persisted to `data/tokens.dat` after each successful write and again
on `quit`. It is reloaded on the next start.

`token_transfer` stays an unsigned research command (opaque names). Prefer
`token_transfer_wallet` or `token_transfer_signed` when the owner is a real
hash-committed address. Anyone who can talk to the trusted local TEXT RPC can
still call the unsigned path. Treat the unsigned ledger as local research, not
a public token security.

LAN / untrusted RPC (when enabled) already filters writes: `token_balance`,
`token_info`, `nft_owner`, `swap_quote`, `swap_pool_info`, and `swap_tvl` are
on the remote allowlist; `token_create` / `token_mint` / `token_transfer` /
`swap_pool_create` / `add_liquidity` / `swap_exact_in` are not. Public-read
RPC keeps the same tighter allowlist and still refuses those writes.

## What this is not

- Not a live mainnet and not a public token contract.
- Not a DEX, Uniswap fork, or public AMM. `swap_*` commands exist on the
  daemon as **in-process pool math** on the same `TokenEngine`. Local forms
  live at `/tokens/` and `/swap/` and talk to loopback `/local-rpc` only.
  The public host stays RPC offline. There is no public peer list for trading.
- Not Ethereum JSON-RPC and not MetaMask.
- Not ZK-Shield / private balances. Privacy commands are a separate, stricter
  path and are out of scope here.

## Local CLI

Requires a running testnet daemon:

```bash
./build/additiond --network testnet
```

In another terminal, from the repository root (so `tools/` is on `sys.path`
when you invoke the script as a file under `tools/`):

```bash
python3 tools/addition_tokens.py getinfo
python3 tools/addition_tokens.py create DEMO alice 1000000 1000
python3 tools/addition_tokens.py mint DEMO alice bob 50
python3 tools/addition_tokens.py transfer DEMO alice bob 10
python3 tools/addition_tokens.py transfer-wallet trader DEMO bob 7
python3 tools/addition_tokens.py sign-payload DEMO alice bob 10
python3 tools/addition_tokens.py balance DEMO alice
python3 tools/addition_tokens.py balance DEMO bob
python3 tools/addition_tokens.py info DEMO
```

`alice` / `bob` are opaque owner strings. They do not have to be mined
addresses. That is how the current engine works.

Optional NFT path:

```bash
python3 tools/addition_tokens.py nft-mint COL item1 alice demo-meta
python3 tools/addition_tokens.py nft-transfer COL item1 alice bob
python3 tools/addition_tokens.py nft-owner COL item1
```

Burn only works for tokens created with `create-ex` and `burnable=1`.

If `ADDITION_RPC_TOKEN` is set on the daemon, pass the same value with
`--rpc-token` or the environment variable. The CLI refuses non-loopback
`--rpc-host` values. No public bootstrap IPs are documented here.

## Local/testnet JSON-RPC adapter (optional)

`tools/addition_jsonrpc_adapter.py` is a **loopback HTTP wrapper** around the
same TEXT RPC. It is labeled a local/testnet adapter on purpose.

- Bind: `127.0.0.1:8645` by default (`POST /rpc`)
- Upstream: `127.0.0.1:8545` TEXT RPC
- Method names are the **exact** TEXT commands (`getinfo`, `token_balance`, …)
- `params` is a JSON array of strings/integers, joined as TEXT tokens
- `result` is the raw TEXT response line
- `eth_*` / `web3_*` / spend commands (`sendtx*`, `sign_message`, `tx_build`)
  are refused
- `swap_*` is not forwarded (in-process pool math is not published as a DEX)
- Non-loopback bind is refused

```bash
python3 tools/addition_jsonrpc_adapter.py --bind 127.0.0.1 --port 8645
```

```bash
curl -sS http://127.0.0.1:8645/rpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"getinfo","params":[]}'

curl -sS http://127.0.0.1:8645/rpc \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"token_balance","params":["DEMO","alice"]}'
```

`--read-only` disables token/NFT writes on the adapter. Writes still require
the trusted local TEXT RPC behind it.

## Peers

Do not publish public bootstrap IPs. `config.toml` lists localhost examples
only (`127.0.0.1:28545`) until a second real node is actually run and a peer
connection is proven. This slice does not add peers.

## Client for strangers

| Goal | Tool |
|---|---|
| Create / mint / transfer / balance | `python3 tools/addition_tokens.py …` |
| Optional JSON wrapper | `python3 tools/addition_jsonrpc_adapter.py` |
| Native ADD spend (keys on disk) | local wallet CLI from the separate wallet PR, when merged |

If a listed TEXT command returns `error: unknown command` on your build, the
daemon binary is older than `src/rpc_server.cpp` — rebuild `additiond`.
