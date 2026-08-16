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

Windows (cashier PC, PowerShell). Do not paste `chmod`, `sudo`, or `apt`. Wallet files: [`/download/`](/download/) (`addition-wallet-testnet.exe`, `addition-wallet-cli-testnet.exe`). They talk to a local `additiond` on `127.0.0.1:8545` only. The public host has no write RPC.

```text
additiond --network testnet --data-dir $HOME\addition-testnet --local-rpc-port 8545 --p2p-port 28547 --bootstrap 34.27.30.115:28545
.\addition-wallet-testnet.exe --cli getinfo
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

Allowlist includes `getblockraw`. Public `mine` / `createwallet` return `error: command disabled on public RPC`.

Write RPC stays `127.0.0.1`. Do not publish port `8545`.

## Seed operator

On the public seed, set `ADDITION_ADVERTISED_P2P=34.27.30.115:28545` so public `getinfo` / `peers` list that IPv4 endpoint and do not list `self`.

That env is advertisement only. Public TCP 28545 can still timeout or be filtered. Joiners should `sync` over HTTP `:80`.

## Local write RPC

There is no public wallet UI. `createwallet`, balances, and swap commands exist on localhost write RPC only.

Pages under `/wallet/` and `/swap/` talk to `/local-rpc` → `127.0.0.1:8545`. On the public host they stay **RPC offline**.

Local desktop helper: [`/download/`](/download/) (testnet / local binary, loopback RPC only).

Raw markdown siblings:

* [`/docs/testnet-rpc-runbook.md`](/docs/testnet-rpc-runbook.md) — public-read RPC systemd runbook
* [`/docs/wallet.md`](/docs/wallet.md) — local loopback wallet
