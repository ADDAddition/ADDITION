# Join ADDITION mainnet

Public product: `ADDITION_MAINNET_V1`. Anyone can run a home node, sync from the public seed, mine, and use wallet RPCs — the same model as `bitcoind` against a known bootstrap peer.

| | |
| --- | --- |
| Network | `ADDITION_MAINNET_V1` |
| P2P bootstrap | **34.27.30.115:28546** |
| Public RPC | **http://34.27.30.115:38546** (read + write allowlist: `createwallet`, `mine`, `wallet_send` / sign / `tx_build` as CoS enabled) |
| Site proxy | `/api/rpc` → that public path |
| Home-node write | `127.0.0.1:8546` (never publish) |

Separate chain from the research testnet (`28545` / `38545`). Height comes from live `getinfo` only — may still be `0`. Do not invent peer counts, TPS, USD ticker, or DEX claims. Brand: **ADDITION** only.

Binary from [this repo](https://github.com/ADDAddition/ADDITION) on `main`. Full runbook: [docs/MAINNET_RUNBOOK.md](https://github.com/ADDAddition/ADDITION/blob/main/docs/MAINNET_RUNBOOK.md).

| Port | Role |
| --- | --- |
| 38546 | Public HTTP on the seed (`/rpc?cmd=…`); site `/api/rpc`. Write allowlist open per CoS. |
| 28546 | Public P2P bootstrap on the seed |
| 8546 | Home-node write RPC on `127.0.0.1` only — never publish |

## Start a mainnet node

```bash
export ADDITION_PRIVACY_MASTER_KEY='replace-with-32-or-more-chars____'
export ADDITION_ENABLE_P2P_RPC=1
additiond --mainnet --data-dir $HOME/addition-mainnet --local-rpc-port 8546 --p2p-port 28547 --bootstrap 34.27.30.115:28546
```

Type `sync` on the daemon stdin, or send it to write RPC on `127.0.0.1:8546` (or use public `38546` when the write allowlist answers). Never `--bootstrap 34.27.30.115:28545` for mainnet. Never publish port `8546`. Auto-mine is refused on mainnet.

## Public RPC

```bash
curl -s 'http://34.27.30.115:38546/rpc?cmd=getinfo'
curl -s 'https://additionblockchain.com/api/rpc?cmd=getinfo'
```

Expect `network=mainnet` and `network_id=ADDITION_MAINNET_V1`. Height may still be `0`; copy only fields the node prints.

On the seed, `createwallet` / `mine` / `wallet_send` / sign / `tx_build` are on the public write allowlist (CoS). The `/wallet/` page prefers that public path via `/api/rpc`, and falls back to `/local-rpc` → `127.0.0.1:8546` when public RPC is offline.

Seed operator: `ADDITION_ADVERTISED_P2P=34.27.30.115:28546` so public `getinfo` / `peers` list that IPv4 endpoint and do not list `self`.

## Research testnet (secondary)

Not the public product on this site.

```bash
additiond --network testnet --data-dir $HOME/addition-testnet --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545
```

| Port | Role |
| --- | --- |
| 38545 | Public RPC for the research chain |
| 28545 | P2P bootstrap for the research chain |
| 8545 | Write RPC on `127.0.0.1` only |

```bash
curl 'http://34.27.30.115:38545/rpc?cmd=getinfo'
```

## Wallet

The `/wallet/` page is a product UI (balance, receive, send, activity). It prefers public mainnet write on `38546` via `/api/rpc`. If that path is offline, use a home node with `ADDITION_NETWORK=mainnet python3 web/serve.py` so `/local-rpc` reaches `127.0.0.1:8546`.

Local desktop helper: [`/download/`](/download/) (loopback RPC only — self-custody on your disk). PWA: Add to Home Screen installs the site; spend uses the same public-first / loopback-fallback path.

Contact: contact@additionblockchain.com
