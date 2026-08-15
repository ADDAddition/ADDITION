# GitHub + Docker (research testnet)

This folder builds and runs the ADDITION **testnet** daemon. It is not a live mainnet.

## Files

- `Dockerfile` : builds `additiond` with liboqs + OpenSSL
- `docker-compose.yml` : runs the testnet node only

The previous `portal-backend` / `Dockerfile.backend` service was removed because that Dockerfile does not exist.

## Quick start

From the repository root:

```bash
docker compose -f deploy/github-docker/docker-compose.yml up --build
```

Then:

- Local write RPC (loopback only): `127.0.0.1:8545`
- `printf 'getinfo\n' | nc 127.0.0.1 8545` should include `network=testnet`

Public read RPC stays off unless you add `--public-rpc` to `command` (or set `ADDITION_ENABLE_PUBLIC_RPC=1`). Two local processes: [docs/TWO_NODE_TESTNET.md](../../docs/TWO_NODE_TESTNET.md). Do not publish write RPC on `0.0.0.0`.

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File deploy/github-docker/run-local.ps1
```
