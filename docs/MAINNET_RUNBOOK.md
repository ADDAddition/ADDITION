# ADDITION mainnet runbook (public chain)

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

This is the **public ADDITION mainnet**: `network_id=ADDITION_MAINNET_V1`, genesis `genesis-mainnet.json`, default data dir `data-mainnet`.

Run `additiond --mainnet` (same as `--network mainnet`) like `bitcoind`: anyone can join the P2P network, sync from the public seed, and mine locally on loopback write RPC.

| Public seed | Role |
|-------------|------|
| `34.27.30.115:28546` | P2P bootstrap |
| `34.27.30.115:38546` | HTTP public read (`/rpc?cmd=getinfo`) |

That is a separate chain from the research testnet (`28545` / `38545` / HTTP `:80`). Not a label flip.

The public website and explorer stay on testnet until the operator switches them. This runbook does not flip the explorer.

Do not open write RPC `8545`/`8546` to the internet. Never bind write RPC to `0.0.0.0`. Do not bootstrap `34.27.30.115:28545` for mainnet.

Unit file: [`deploy/systemd/additiond-mainnet.service`](../deploy/systemd/additiond-mainnet.service)

## Join mainnet (home node)

Build `additiond` from this repository on `main`. Set `ADDITION_PRIVACY_MASTER_KEY` to at least 32 characters. Then:

```bash
export ADDITION_PRIVACY_MASTER_KEY='replace-with-32-or-more-chars____'
export ADDITION_ENABLE_P2P_RPC=1
./build/additiond --mainnet \
  --data-dir $HOME/addition-mainnet \
  --local-rpc-port 8546 \
  --p2p-port 28547 \
  --bootstrap 34.27.30.115:28546
# then: sync   (HTTP :38546 getblockraw and/or P2P HELLO+REQBLK)
# then: mine on 127.0.0.1:8546 only
```

Type `sync` on the daemon stdin (or send it to write RPC on `127.0.0.1:8546`), then `getinfo`.

Never pass `--bootstrap 34.27.30.115:28545` on mainnet (research testnet seed only). Never publish port `8546`.

### Public read (HTTP ingest)

```bash
curl -s 'http://34.27.30.115:38546/rpc?cmd=getinfo'
```

Expect `network=mainnet` and `network_id=ADDITION_MAINNET_V1`. Height may still be `0`; copy only what `getinfo` prints. Auto-mine stays off on mainnet. Do not invent peer counts or TPS.

`config-mainnet.toml` lists bootstrap peer `34.27.30.115:28546`. Seed operators set `ADDITION_ADVERTISED_P2P=34.27.30.115:28546` so public `getinfo` / `peers` advertise that IPv4 endpoint and never list `self`.

## What `--mainnet` changes

| Item | Research testnet | Public mainnet |
|------|------------------|----------------|
| Flag | `--network testnet` | `--mainnet` or `--network mainnet` |
| `network` / `network_id` | `testnet` / `ADDITION_TESTNET_V1` | `mainnet` / `ADDITION_MAINNET_V1` |
| Genesis | `genesis.json` timestamp `1763000000` | `genesis-mainnet.json` timestamp `1770000000` |
| Data dir | `data` | `data-mainnet` |
| Write RPC | `127.0.0.1:8545` | `127.0.0.1:8546` |
| Public read | `0.0.0.0:38545` (opt-in) | `0.0.0.0:38546` (seed: `34.27.30.115:38546`) |
| P2P | `28545` (research seed) | `28546` public seed; home nodes use another port (e.g. `28547`) |
| PoW | SHA3-512 | memory_hard (1 MiB × 16 rounds) |
| Difficulty | easy `0x0000FFFFFFFFFFFF` (~4 ms observed) | hard floor `0x000000FFFFFFFFFF` |

`getinfo` reports `network=mainnet` and `network_id=ADDITION_MAINNET_V1` only when this chain is actually running.

A testnet process still reports `network=testnet`. Mixing a testnet `blocks.dat` or `network.dat` into a mainnet `--data-dir` is refused.

`ADDITION_PRIVACY_MASTER_KEY` (min 32 characters) is required to start `--mainnet`. Auto-mine is refused.

## Mine (local write RPC only)

On `--mainnet` the search runs until it finds a block (`mine_deadline_sec=0` in `getinfo`). There is no 30s mine timeout (that bound is testnet-only).

`mine` uses a multi-thread `memory_hard` miner (one 1 MiB scratch buffer per
thread, `hardware_concurrency` workers by default) against the existing
target `0x000000FFFFFFFFFF`. That is about 2^24 hashes. Do not loosen it.
Do not invent TPS.

Write RPC stays `127.0.0.1:8546`. Public read cannot mine. Public RPC still refuses `mine` / `createwallet` / `send`.

```bash
printf 'mine miner1\n' | nc 127.0.0.1 8546
# waits until a nonce meets the target; clients must not use a 30s timeout
```

After a local `mine`, the node announces `BLK|` and `gossip_flush` pushes to peers.

## Difficulty (what we picked and why)

The existing retarget in `src/chain.cpp` compares the last `retarget_window` (30) block timestamps to `window * target_block_time_sec` (30 × 60s).

If blocks arrive early, the target is multiplied by `9/10` (harder). If they arrive late, the target is scaled up, then clamped to `[min_difficulty_target, max_difficulty_target]`.

Smaller target = harder. The check is `head64(hash) <= target` (`sha3_512` or `memory_hard`).

Public testnet `getinfo` has shown `last_mine_ms=4` at the easy target `0x0000FFFFFFFFFFFF` (~16 leading zero bits, about 2^16 SHA3-512 header hashes). That is the trivial regime this chain must not use.

Mainnet uses the existing **testnet hard** numeric floor `0x000000FFFFFFFFFF` (documented in `config.hpp` as able to approach ~60s on SHA3-512) as `initial`, `min`, and `max`.

Retarget therefore cannot climb back to the ~4 ms easy target.

PoW is `memory_hard`: each nonce hashes a 1 MiB scratch buffer for 16 rounds, so each attempt is far more expensive than testnet SHA3-512 of a short header.

This is a target/retarget choice, not a throughput claim. Do not invent TPS.

## Local start (proof)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target additiond
export ADDITION_PRIVACY_MASTER_KEY='replace-with-32-or-more-chars____'
./build/additiond --mainnet --data-dir /tmp/addition-mainnet --local-rpc-port 8546
printf 'getinfo\n' | nc 127.0.0.1 8546
# expect: network=mainnet network_id=ADDITION_MAINNET_V1
```

Equivalent: `--network mainnet`. Do not set `ADDITION_MAINNET_MODE=1` on a testnet unit.

## systemd

```bash
sudo useradd --system --home /var/lib/addition --shell /usr/sbin/nologin addition
sudo mkdir -p /opt/addition /var/lib/addition/mainnet /etc/addition
sudo cp -a . /opt/addition
sudo chown -R addition:addition /var/lib/addition/mainnet
# /etc/addition/mainnet.env  (mode 0640, owner root:addition)
# ADDITION_PRIVACY_MASTER_KEY=<32+ chars>
# ADDITION_RPC_TOKEN=<optional write-RPC token>
# ADDITION_ENABLE_P2P_RPC=1
# ADDITION_ADVERTISED_P2P=34.27.30.115:28546   # public seed operator
sudo install -m 0644 deploy/systemd/additiond-mainnet.service /etc/systemd/system/additiond-mainnet.service
sudo systemctl daemon-reload
sudo systemctl enable --now additiond-mainnet.service
```

| Listener | Bind | Port | Role |
|----------|------|------|------|
| Public read RPC | `0.0.0.0` | **38546** | Allowlisted reads only (seed: `34.27.30.115:38546`) |
| Write / admin RPC | `127.0.0.1` | **8546** | Trusted local only; never public |
| P2P | `0.0.0.0` | **28546** | Public seed; home nodes use another port (e.g. 28547). Needs `ADDITION_ENABLE_P2P_RPC=1` |

Never publish **8545** or **8546**. Never enable LAN RPC (`18546`) on a public interface.

If you also run the research testnet unit, keep its ports (38545 / 8545 / 28545) and its `/var/lib/addition/testnet` data dir.

```bash
printf 'getinfo\n' | nc 127.0.0.1 38546
curl -s 'http://127.0.0.1:38546/rpc?cmd=getinfo'
printf 'mine\n' | nc 127.0.0.1 38546
# error: command disabled on public RPC
```

`getinfo` must include `network=mainnet` and `network_id=ADDITION_MAINNET_V1`.

## P2P (public mainnet seed)

P2P listen is off unless `ADDITION_ENABLE_P2P_RPC=1`. HELLO carries `ADDITION_MAINNET_V1`, so a testnet peer is rejected.

Public bootstrap is **34.27.30.115:28546**. Do not use the research testnet seed `34.27.30.115:28545`. If TCP 28546 is filtered on your path, use HTTP `:38546` for `getinfo` / ingest checks (`getblockraw`), then retry P2P (`HELLO`+`REQBLK`).

Do not loosen `memory_hard` / `0x000000FFFFFFFFFF`. Do not auto-mine. Do not bind write RPC to `0.0.0.0`.

## Rollback

Stop the unit (`systemctl stop additiond-mainnet`). Snapshot `/var/lib/addition/mainnet`.

Restore that directory if you need the same genesis and height. A testnet `data` / `blocks.dat` will not load here.
