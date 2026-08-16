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
| P2P (operator public node) | `0.0.0.0` | **28545** | Join path; IPv4 only |
| Write / admin RPC | `127.0.0.1` | **8545** | Trusted local only; never public |

`additiond` always binds write RPC to `127.0.0.1`. Never publish **8545**.
Never open LAN RPC (`18545`) from this unit.

The live operator node enables P2P with `ADDITION_ENABLE_P2P_RPC=1` and
advertises one IPv4 bootstrap: **34.27.30.115:28545**. That is the current
public P2P of this testnet, not a peer-count claim and not a mainnet.
`config.toml` / `getinfo.bootstrap_peers` list that single endpoint so
outsiders can `--bootstrap 34.27.30.115:28545`. Do not invent extra peers.
The binary still leaves P2P off until that env is set; the public GCP
node sets it.

## Live seed P2P dialect (required for a second machine)

The operator seed at **34.27.30.115:28545** still runs the persist-era
binary (`3a83a32`). That process does **one** `recv` (32 KiB) per TCP
connection, then replies `ok:` + `outbound_.front()`. A same-VM pair of
that binary can sync because loopback delivers the whole ML-DSA HELLO
(~15 KiB) in one read. A current-main home node on a real path cannot:
the first TCP segment is ~1.4 KiB, the seed parses a cut HELLO, and
`sync` used to print `ok:height=0`.

Current `additiond` (this tree) does this on the client:

- wire id `n-<32 hex of SHA3(node-id|pubkey)>`, never the shared `self`
  (persist bans `self` after five failed HELLOs)
- up to 8 HELLO attempts; later attempts use `n-<hash>xN` so a truncated
  HELLO does not 5-strike-ban the stable id
- leftover `ok`, `ok:BLK|`, `ok:TX|`, `ok:HAVE*`, `ok:INV|` counts as
  handshake-accepted only if the next `REQWORK` returns `HAVEWORK`
- `sync` returns `error: …` when handshake fails or the peer is still
  longer; it must not print `ok:height=0` in those cases

A two-node test on current main is necessary and not sufficient if the
seed still speaks persist I/O. For a WAN home node to pull blocks, the
seed must speak this reply dialect (current `handle_p2p_line` /
`socket_recv_request`):

```text
client: <peer_id> HELLO|2|ADDITION_TESTNET_V1|<unix_ts>|<nonce>|<ml-dsa-87-pubkey-hex>|<sig-hex>\n
seed:   loop-read until '\n' (max 262144 bytes; one recv(32768) is not enough)
seed:   ok:HELLO_ACK|2|ADDITION_TESTNET_V1|<unix_ts>|<same-nonce>|<ml-dsa-87-pubkey-hex>|<sig-hex>\n
```

Then, on new TCP connections, the same `<peer_id>` may send `REQWORK|`,
`REQH|`, `REQINV|<start>|<limit>`, `REQBLK|<height>`. Replies are
`ok:HAVEWORK|<n>`, `ok:HAVEH|<height>`, `ok:INV|<h:hash,…>`,
`ok:BLKDATA|<payload>`. Do not push those onto gossip `outbound_` and
do not return a leftover `BLK|` in place of `HELLO_ACK`. Handshake
state is keyed by the claimed `<peer_id>`, not by the TCP connection.

Write RPC stays `127.0.0.1`. Contact:
[contact@additionblockchain.com](mailto:contact@additionblockchain.com).

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
# Live operator public node (34.27.30.115): P2P on, write RPC stays loopback.
ADDITION_ENABLE_P2P_RPC=1
```

Do not set `ADDITION_MAINNET_MODE`. This unit is testnet only.
Do not set `ADDITION_ENABLE_LAN_RPC`.

Optional in-process auto-mine (off by default; never on public RPC):

```bash
# /etc/addition/testnet.env
ADDITION_AUTO_MINE=1
ADDITION_AUTO_MINE_INTERVAL=60
ADDITION_AUTO_MINE_REWARD=miner1
```

After N seconds the daemon mines one testnet block and writes `blocks.dat`.

## Firewall: 38545 always; 28545 only if P2P is enabled

Allow inbound **38545** for public-read RPC. Allow inbound **28545** only
when `ADDITION_ENABLE_P2P_RPC=1` (the live operator node does this). Never
open **8545** or **18545** on a public interface. 8545 stays localhost.

The live GCP node uses firewall rule `allow-addition-p2p` for TCP **28545**.
That matches `bootstrap_peers = ["34.27.30.115:28545"]`. If P2P is off,
do not open 28545.

```bash
# ufw
sudo ufw allow 38545/tcp
sudo ufw allow 28545/tcp   # only while ADDITION_ENABLE_P2P_RPC=1
sudo ufw deny 8545/tcp
sudo ufw deny 18545/tcp

# firewalld
sudo firewall-cmd --permanent --add-port=38545/tcp
sudo firewall-cmd --permanent --add-port=28545/tcp   # only while P2P is on
sudo firewall-cmd --reload
```

Cloud security groups: inbound TCP **38545** (public-read) and, when P2P
is enabled, TCP **28545** (`allow-addition-p2p`). Leave **8545** and
**18545** off the public interface.

```bash
ss -lnt | grep -E '8545|18545|28545|38545'
# expect: 127.0.0.1:8545
#         0.0.0.0:38545 (or *:38545)
#         0.0.0.0:28545 only if ADDITION_ENABLE_P2P_RPC=1
#         nothing on 18545
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
