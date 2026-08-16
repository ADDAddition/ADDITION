# Testnet public-read RPC (systemd)

Research testnet only. This is a **public-read** listener on port **38545**, not a
live mainnet, not a token sale, and not a claim of public peer counts.

Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

Unit file: [`deploy/systemd/additiond-testnet.service`](../deploy/systemd/additiond-testnet.service)

## What the unit starts

```text
additiond --network testnet --public-rpc
```

| Listener | Bind | Port | Role |
|----------|------|------|------|
| Public read RPC | `0.0.0.0` | **38545** | Allowlisted reads only |
| Write / admin RPC | `127.0.0.1` | **8545** | Trusted local only |

`additiond` always binds write RPC to `127.0.0.1`. Do not publish **8545**.
Do not open LAN RPC (`18545`) from this unit.

The operator’s current public P2P is **34.27.30.115:28545** (IPv4 only).
`config.toml` / `getinfo.bootstrap_peers` advertise that one endpoint so
outsiders can `--bootstrap 34.27.30.115:28545`. That is not a peer-count
claim. Joining still needs `ADDITION_ENABLE_P2P_RPC=1` on the process that
listens on 28545. This unit leaves P2P off unless the operator sets that
env in `/etc/addition/testnet.env`.

Public allowlist: `getinfo`, `monetary_info`, `crypto_selftest`, `tx_status`,
`peers`, `getblock`, `getblockhash`. Writes (`mine`, `sendtx*`, `createwallet`,
`wallet_*`) return `error: command disabled on public RPC`.

Do not commit trycloudflare or other ephemeral tunnel URLs. Point a website
`PUBLIC_RPC_HTTP` at a durable HTTP URL you operate, or leave it empty so the
site stays **RPC offline**.

## Enable the unit

Build first (`cmake -S . -B build && cmake --build build --target additiond`).
Install the tree (or edit `WorkingDirectory` / `ExecStart` in the unit):

```bash
sudo useradd --system --home /var/lib/addition --shell /usr/sbin/nologin addition
sudo mkdir -p /opt/addition /var/lib/addition/testnet /etc/addition
sudo cp -a . /opt/addition
sudo chown -R addition:addition /var/lib/addition
sudo install -m 0644 deploy/systemd/additiond-testnet.service /etc/systemd/system/additiond-testnet.service
sudo systemctl daemon-reload
sudo systemctl enable --now additiond-testnet.service
sudo systemctl status additiond-testnet.service
```

Optional write-RPC token (loopback only):

```bash
# /etc/addition/testnet.env  (mode 0640, owner root:addition)
ADDITION_RPC_TOKEN=<strong_token>
```

Do not set `ADDITION_MAINNET_MODE`. This unit is testnet only.

Optional in-process auto-mine (off by default; never on public RPC):

```bash
# /etc/addition/testnet.env
ADDITION_AUTO_MINE=1
ADDITION_AUTO_MINE_INTERVAL=60
ADDITION_AUTO_MINE_REWARD=miner1
```

After N seconds the daemon mines one testnet block and writes `blocks.dat`.

## Firewall: allow 38545 only

Allow the public-read port. Do **not** allow **8545** (or 18545 / 28545).

```bash
# ufw
sudo ufw allow 38545/tcp
sudo ufw deny 8545/tcp

# firewalld
sudo firewall-cmd --permanent --add-port=38545/tcp
sudo firewall-cmd --reload
```

Cloud security groups: inbound TCP **38545** only. Leave **8545** off the
public interface. Confirm write RPC is loopback-only:

```bash
ss -lnt | grep -E '8545|38545'
# expect: 127.0.0.1:8545  and  0.0.0.0:38545 (or *:38545)
```

## Checks

```bash
printf 'getinfo\n' | nc 127.0.0.1 38545
curl -s 'http://127.0.0.1:38545/rpc?cmd=getinfo'
printf 'mine\n' | nc 127.0.0.1 38545
# error: command disabled on public RPC
```

`getinfo` should include `network=testnet`. That is a research testnet
public-read RPC, not a live mainnet.

`--data-dir` (`/var/lib/addition/testnet` in this unit) holds the chain.
`blocks.dat` is written after each accepted block. A `systemctl restart`
must keep the previous `getinfo` height and the same `getblock` hashes.
If the directory only has `node_identity.dat` and `wallets/`, no block
was persisted (old binary, or the node never mined).
