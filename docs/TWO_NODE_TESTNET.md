# Two-node local testnet runbook

This starts **two local `additiond` processes** on one machine. It is not a public
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

HTTP responses include `Access-Control-Allow-Origin: *` (credential-less public
read allowlist; writes stay 403), `Access-Control-Allow-Methods: GET, OPTIONS`,
`OPTIONS` → `204`, and `Cache-Control: no-store`. `curl /rpc?cmd=getinfo` is
unchanged. CORS `*` is not a wallet-connect surface.

## Point the website at a real public RPC

`web/public/wrangler.toml` ships with:

```toml
PUBLIC_RPC_HTTP = ""
```

Leave it empty. The worker and static pages show **RPC offline** when the node is down.
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

## P2P limits

- P2P transport is off unless `ADDITION_ENABLE_P2P_RPC=1`.
- When the operator seed answers, it sets that env and allows inbound TCP
  **28545** (GCP rule `allow-addition-p2p`). Allow 28545 only while P2P is
  enabled. Never open **8545** or **18545**. A timeout means the seed is
  down — use this two-node script instead of inventing another peer.
- Endpoints are IPv4 `ip:port` only (`inet_pton`). Docker DNS names do not work.
- `bootstrap_peers` / `--bootstrap` skip this process’s own P2P port, `self` /
  `probe-self`, and `ADDITION_ADVERTISED_P2P` when set (seed must set that to
  its public `ip:port` so it does not add itself).
- `getinfo.peers` / `peers` list only non-loopback IPv4 endpoints. Loopback
  two-node peers stay in the internal set for `sync` and appear as
  `local=127.0.0.1:…`. Node ids and `self` are not an external peer count.
- IPv4 only. The configured operator public P2P is `34.27.30.115:28545`
  (`--bootstrap 34.27.30.115:28545`). Do not invent extra peers. Write RPC
  stays `127.0.0.1:8545`.
- Seed operators set `ADDITION_ADVERTISED_P2P=34.27.30.115:28545` so public
  `getinfo` / `peers` do not list `self`. Public TCP 28545 can timeout or
  be filtered; HTTP `:80` sync is the reliable join path. This local
  runbook does not claim public 28545 works.
- `sync` pulls the public chain over HTTP: `:80` first (`getblockraw`), then
  `:38545`. Public RPC still rejects `mine` / `createwallet` / `wallet_*`.
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
`ADDITION_ADVERTISED_P2P` (seed public IPv4 only),
`ADDITION_AUTO_MINE=1`, `ADDITION_AUTO_MINE_INTERVAL`, `ADDITION_AUTO_MINE_REWARD`.

## Local two-node HTTP ingest + HELLO test

This is a **local** check. It does not claim public P2P 28545 works.
Write RPC stays `127.0.0.1`.

```bash
cmake -S . -B build -DADDITION_BUILD_TESTS=ON
cmake --build build --target additiond
python3 tests/test_two_node_http_ingest.py
# or: ctest --test-dir build -R test_two_node_http_ingest --output-on-failure
```

The script starts two `additiond` processes. Node A mines on trusted write
RPC, advertises `ADDITION_ADVERTISED_P2P=34.27.30.115:28545` so public
`getinfo` / `peers` omit `self`, then node B:

1. pulls A's chain over **HTTP ingest** (`getinfo` + `getblockraw` on A's
   public-read port via `ADDITION_PUBLIC_HTTP_PORT`)
2. on a fresh data-dir, pulls again via local **HELLO** when HTTP ports
   are closed (localhost P2P only; not a public-28545 claim)

Auto-mine is testnet only, off unless `--auto-mine` / `ADDITION_AUTO_MINE=1`.
It is not a public RPC command. Each mined block is written to `blocks.dat`.

## Local `--regtest` min-diff + confirmations

`--regtest` (or `--network regtest`) starts `ADDITION_REGTEST_V1` with min
difficulty (`0xFFFFFFFFFFFFFFFF`) so two local processes can mine quickly.
This is not the public testnet and not mainnet. `ADDITION_MAINNET_V1`
difficulty stays `0x000000FFFFFFFFFF`. Write RPC stays `127.0.0.1`.

```bash
cmake -S . -B build -DADDITION_BUILD_TESTS=ON
cmake --build build --target additiond test_confirmations
ctest --test-dir build -R 'test_confirmations|test_regtest_two_node' --output-on-failure
```

`getinfo` reports `confirmations_policy` and `economic_security=none`. After
`wallet_send`, mine N blocks; `tx_status` on both nodes shows the same
`confirmations=N`. No ZK, no cross-chain, no mainnet claim. Live public RPC
stays testnet.
