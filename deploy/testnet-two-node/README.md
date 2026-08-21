# Two-node testnet compose

See [docs/TWO_NODE_TESTNET.md](../../docs/TWO_NODE_TESTNET.md).

```bash
docker compose -f deploy/testnet-two-node/docker-compose.yml up --build
```

Linux `network_mode: host` is required so node B can bootstrap `127.0.0.1:28545`.
P2P does not resolve Docker DNS names.

Write RPC: `127.0.0.1:8545` (A) and `127.0.0.1:8546` (B).
Public read RPC: node A `0.0.0.0:38545`.

Leave website `PUBLIC_RPC_HTTP` unset until you set a real HTTP URL as a
Worker secret. Do not commit trycloudflare URLs.
