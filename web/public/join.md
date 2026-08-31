# Join the ADDITION testnet

Research testnet. Not a production L1.

Binary from [this repo](https://github.com/ADDAddition/ADDITION) on `main`.
Public read is `/rpc?cmd=getinfo`, not `/getinfo`.

HTML page (unchanged): [`/join/`](/join/).
Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

## Ports

| port | role |
|------|------|
| 80 | public HTTP. Reliable join path. Sync uses :80 first. |
| 38545 | public RPC, optional/filtered |
| 28545 | P2P, optional/filtered. Can timeout. Do not assume it always works. |

## Start a node

Build `additiond` from `main`, then:

```text
additiond --network testnet --data-dir $HOME/addition-testnet --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545
```

Type `sync` on the daemon stdin (or send it to write RPC on `127.0.0.1`). Height should move.

`addpeer` after `--bootstrap` is `invalid/duplicate` (the seed is already listed).

## What `sync` does

Current `main` tries public-read HTTP `:80` first, then `:38545`, then P2P `HELLO`.

`:80` is the reliable join path when `38545` or `28545` is filtered or times out. Ingest uses `getinfo` plus `getblockraw` (`ok:BLKDATA`). Do not claim public P2P 28545 always works.

## Public read

```bash
curl 'https://rpc.additionblockchain.com/rpc?cmd=getinfo'
curl 'http://34.27.30.115/rpc?cmd=getinfo'
curl 'http://34.27.30.115:38545/rpc?cmd=getinfo'
```

Those curls succeed only when the operator seed answers. If they timeout, run your own `--network testnet` node. Height stays local until the seed answers.

Allowlist includes `getblockraw`. Public `mine` / `createwallet` return `error: command disabled on public RPC`.

Write RPC stays `127.0.0.1`. Do not publish port `8545`.

## Seed operator

On the public seed, set `ADDITION_ADVERTISED_P2P=34.27.30.115:28545` so public `getinfo` / `peers` list that IPv4 endpoint and do not list `self`.

That env is advertisement only. Public TCP 28545 can still timeout or be filtered. Joiners should `sync` over HTTP `:80`.

## Local write RPC

The `/wallet/` page is a product UI (balance, receive, send, activity). It still only spends via `/local-rpc` → `127.0.0.1:8545`. On the public host that proxy is unavailable — the page explains how to connect a home node. It never POSTs `createwallet` / `wallet_send` / `mine` to public `:80`, `38545`, `38546`, or public `8545`.

Pages under `/local/` (`/tokens/`, `/swap/`, `/privacy/`, …) are the operator toolbox on the same loopback path.

Local forms: `/wallet/` (primary), `/local/` hub, `/tokens/`, `/swap/` (`fee_bps=0` allowed), `/privacy/` (`claim=opening_not_zk`). Floor fee is `min_fee=0` on current `main`. Spend signatures are ML-DSA-87. Public read cannot write.

Local desktop helper: [`/download/`](/download/) (testnet / local binary, loopback RPC only). PWA: Add to Home Screen installs the site; spending still needs a local node.

Raw markdown siblings:

* [`/docs/testnet-rpc-runbook.md`](/docs/testnet-rpc-runbook.md) — public-read RPC systemd runbook
* [`/docs/wallet.md`](/docs/wallet.md) — local loopback wallet

---

# Join the ADDITION mainnet

This is the public ADDITION mainnet (`network_id=ADDITION_MAINNET_V1`). Anyone can run a home node, sync from the public seed, and mine locally — the same model as `bitcoind` against a known bootstrap peer.

Public seed: **34.27.30.115:28546** (P2P) and **34.27.30.115:38546** (HTTP read). Separate chain from the research testnet above (`28545` / `38545` / `:80`). Not a label flip. The public website explorer stays on testnet in this change; join the chain with `additiond`, not the explorer UI.

Binary from [this repo](https://github.com/ADDAddition/ADDITION) on `main`. Full runbook: [docs/MAINNET_RUNBOOK.md](https://github.com/ADDAddition/ADDITION/blob/main/docs/MAINNET_RUNBOOK.md).

## Mainnet ports

| port | role |
|------|------|
| 38546 | public read HTTP on the seed (`/rpc?cmd=getinfo`) |
| 28546 | public P2P bootstrap on the seed |
| 8546 | write RPC on `127.0.0.1` only — never publish |

## Start a mainnet node

Set `ADDITION_PRIVACY_MASTER_KEY` (32+ characters), build `additiond` from `main`, then:

```text
export ADDITION_PRIVACY_MASTER_KEY='replace-with-32-or-more-chars____'
export ADDITION_ENABLE_P2P_RPC=1
additiond --mainnet --data-dir $HOME/addition-mainnet --local-rpc-port 8546 --p2p-port 28547 --bootstrap 34.27.30.115:28546
```

Type `sync` on the daemon stdin (or send it to write RPC on `127.0.0.1:8546`), then `getinfo`. Mine on that same loopback write RPC when you want to produce blocks (`memory_hard`, no 30s mine timeout).

Never `--bootstrap 34.27.30.115:28545` for mainnet (that is the research testnet seed). Never publish port `8546`. Auto-mine is refused on mainnet.

## Mainnet public read

```bash
curl -s 'http://34.27.30.115:38546/rpc?cmd=getinfo'
```

Expect `network=mainnet` and `network_id=ADDITION_MAINNET_V1`. Height may still be `0`; copy only fields the node prints. Do not invent peer counts or TPS.

Seed operator: `ADDITION_ADVERTISED_P2P=34.27.30.115:28546` so public `getinfo` / `peers` list that IPv4 endpoint and do not list `self`.
