# Two-node local testnet runbook

This starts **two honest `additiond` processes** on one machine. It is not a public
mainnet, not a token sale, and it does not invent peer counts or hashrate.

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

## Ports

| Node | Write RPC (loopback only) | P2P | Public read RPC |
|------|---------------------------|-----|-----------------|
| A | `127.0.0.1:8545` | `28545` | `38545` (opt-in `--public-rpc`) |
| B | `127.0.0.1:8546` | `28546` | off |

Write RPC is always bound to `127.0.0.1`. Never publish it to `0.0.0.0`.

Public read RPC default bind is `0.0.0.0:38545`. For a local tunnel use
`--public-rpc-bind 127.0.0.1`. The helper script defaults to `127.0.0.1`; set
`ADDITION_PUBLIC_RPC_BIND=0.0.0.0` when you intentionally expose the read port.

## Start (scripts)

Build first:

```bash
cmake -S . -B build
cmake --build build --target additiond
```

Then:

```bash
chmod +x scripts/start_two_node_testnet.sh scripts/stop_two_node_testnet.sh
./scripts/start_two_node_testnet.sh
```

Equivalent manual commands:

```bash
export ADDITION_ENABLE_P2P_RPC=1

./build/additiond --network testnet --public-rpc \
  --data-dir data/two-node/node-a \
  --local-rpc-port 8545 --p2p-port 28545 \
  --public-rpc-port 38545 --public-rpc-bind 127.0.0.1

# second terminal
./build/additiond --network testnet \
  --data-dir data/two-node/node-b \
  --local-rpc-port 8546 --p2p-port 28546 \
  --bootstrap 127.0.0.1:28545
```

Node B also accepts `addpeer` on the trusted write port:

```bash
printf 'addpeer 127.0.0.1:28545\n' | nc 127.0.0.1 8546
printf 'peers\n' | nc 127.0.0.1 8546
printf 'sync\n' | nc 127.0.0.1 8546
```

## Start (compose)

Linux host network (P2P endpoints are IPv4 literals; hostnames are not resolved):

```bash
docker compose -f deploy/testnet-two-node/docker-compose.yml up --build
```

Write ports stay on loopback via the daemon bind. Public read RPC is on node A only.

## Public read checks (node A)

```bash
printf 'getinfo\n' | nc 127.0.0.1 38545
curl -s 'http://127.0.0.1:38545/rpc?cmd=getinfo'
printf 'mine\n' | nc 127.0.0.1 38545
printf 'createwallet\n' | nc 127.0.0.1 38545
```

Writes (`mine`, `sendtx*`, `createwallet`, `wallet_*`, identity rotation) return
exactly:

```text
error: command disabled on public RPC
```

HTTP responses include `Access-Control-Allow-Origin: *`, `OPTIONS` → `204`, and
`Cache-Control: no-store` so a static site can call the port without caching.

## Point the website at a real public RPC

`web/public/wrangler.toml` ships with:

```toml
PUBLIC_RPC_HTTP = ""
```

Leave it empty. The worker and static pages fail closed with **RPC offline**.
They do not invent blocks, peers, or hashrate.

When an operator has a **real** public-rpc HTTP URL (this machine, a VPS, or a
tunnel the operator created), set the worker variable to that URL:

```toml
PUBLIC_RPC_HTTP = "http://127.0.0.1:38545/rpc"
```

or, for a durable hostname the operator controls:

```bash
npx wrangler secret put PUBLIC_RPC_HTTP
# paste the real http(s) URL ending in /rpc
```

Do **not** commit trycloudflare or other ephemeral tunnel URLs.

Local site proxy (`python3 web/serve.py`) talks to `127.0.0.1:38545` when the
public port is up. If the daemon is down, pages show **RPC offline**.

## Honest P2P limits

- P2P transport is off unless `ADDITION_ENABLE_P2P_RPC=1`.
- Endpoints are IPv4 `ip:port` only (`inet_pton`). Docker DNS names do not work.
- `bootstrap_peers` / `--bootstrap` skip this process’s own P2P port.
- IPv4 only. The operator’s current public P2P is `34.27.30.115:28545`
  (`--bootstrap 34.27.30.115:28545`). Do not invent extra peers. Write RPC
  stays `127.0.0.1:8545`.
- `addpeer` is a trusted-write command. Public RPC can only `peers`.
- `sync` on two fresh nodes at height 0 has nothing to fetch. That is not a
  live network and not a peer-count claim.
- DEX / ZK-Shield commands stay off the public allowlist. Do not advertise
  them unless they succeed on the trusted local port of a running node.

## Config keys

Same meaning as CLI / env:

- `enable_public_rpc`
- `ports.public_rpc` / `ports.public_rpc_bind`
- `ports.local_rpc` / `ports.p2p`
- `bootstrap_peers` (IPv4 `ip:port` only; repo `config.toml` lists `34.27.30.115:28545`)
- `enable_auto_mine` / `auto_mine_interval_sec` / `auto_mine_reward` (off by default)

Env: `ADDITION_ENABLE_PUBLIC_RPC=1`, `ADDITION_PUBLIC_RPC_PORT`,
`ADDITION_PUBLIC_RPC_BIND`, `ADDITION_LOCAL_RPC_PORT`, `ADDITION_P2P_PORT`,
`ADDITION_AUTO_MINE=1`, `ADDITION_AUTO_MINE_INTERVAL`, `ADDITION_AUTO_MINE_REWARD`.

Auto-mine is testnet only, off unless `--auto-mine` / `ADDITION_AUTO_MINE=1`.
It is not a public RPC command. Each mined block is written to `blocks.dat`.
