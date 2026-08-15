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

- Local RPC: `127.0.0.1:8545`
- `printf 'getinfo\n' | nc 127.0.0.1 8545` should include `network=testnet`

Windows:

```powershell
powershell -ExecutionPolicy Bypass -File deploy/github-docker/run-local.ps1
```
