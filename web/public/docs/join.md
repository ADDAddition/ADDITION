# Join ADDITION mainnet

Public product: `ADDITION_MAINNET_V1`. Anyone can run a home node, sync from the public seed, and mine locally — the same model as `bitcoind` against a known bootstrap peer.

Public seed: **34.27.30.115:28546** (P2P) and **34.27.30.115:38546** (HTTP). Site `/api/rpc` proxies that read path. Separate chain from the research testnet (`28545` / `38545`).

Binary from [this repo](https://github.com/ADDAddition/ADDITION) on `main`. Full runbook: [docs/MAINNET_RUNBOOK.md](https://github.com/ADDAddition/ADDITION/blob/main/docs/MAINNET_RUNBOOK.md).

| Port | Role |
| --- | --- |
| 38546 | public HTTP on the seed (`/rpc?cmd=getinfo`); site `/api/rpc` |
| 28546 | public P2P bootstrap on the seed |
| 8546 | write RPC on `127.0.0.1` only — never publish |

## Start a mainnet node

```bash
export ADDITION_PRIVACY_MASTER_KEY='replace-with-32-or-more-chars____'
export ADDITION_ENABLE_P2P_RPC=1
additiond --mainnet --data-dir $HOME/addition-mainnet --local-rpc-port 8546 --p2p-port 28547 --bootstrap 34.27.30.115:28546
```

Type `sync` on the daemon stdin, or send it to write RPC on `127.0.0.1:8546`. Never `--bootstrap 34.27.30.115:28545` for mainnet. Never publish port `8546`. Auto-mine is refused on mainnet.

## Public read

```bash
curl -s 'http://34.27.30.115:38546/rpc?cmd=getinfo'
curl -s 'https://additionblockchain.com/api/rpc?cmd=getinfo'
```

Expect `network=mainnet` and `network_id=ADDITION_MAINNET_V1`. Height may still be `0`; copy only fields the node prints. Do not invent peer counts or TPS.

On the seed, `createwallet` / `mine` may answer on `38546`. `wallet_send` / keys stay disabled on that public port. The `/wallet/` page still only spends via `/local-rpc` → `127.0.0.1` (never public send).

Seed operator: `ADDITION_ADVERTISED_P2P=34.27.30.115:28546` so public `getinfo` / `peers` list that IPv4 endpoint and do not list `self`.

## Research testnet (separate chain)

Not the public product on this site.

```bash
additiond --network testnet --data-dir $HOME/addition-testnet --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545
```

| Port | Role |
| --- | --- |
| 38545 | public RPC, optional/filtered |
| 28545 | P2P, optional/filtered |
| 8545 | write RPC on `127.0.0.1` only |

```bash
curl 'http://34.27.30.115:38545/rpc?cmd=getinfo'
```

## Wallet

The `/wallet/` page is a product UI (balance, receive, send, activity). It only spends via `/local-rpc` → loopback write RPC. On the public host that proxy is unavailable — the page explains how to connect a home node. It never POSTs `wallet_send` to public `:80`, `38545`, `38546`, or public `8545`/`8546`.

Local desktop helper: [`/download/`](/download/) (loopback RPC only). PWA: Add to Home Screen installs the site; spending still needs a local node.

Contact: contact@additionblockchain.com
