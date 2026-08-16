# ADDITION wallet packaging (testnet / local)

Package the local wallet helpers into a file a stranger can download and run.
This is **testnet / local** only. It is not a hosted web wallet and not a live
mainnet product.

Public contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

Flutter under `client/addition_app` is still a stub, so this path uses
PyInstaller around:

* `web/addition_wallet_gui.py` — GUI + `--cli` (node wallet files, loopback RPC)
* `web/addition_wallet.py` — caller-disk CLI (keys on the caller, loopback RPC)

Both refuse non-loopback RPC hosts (`127.0.0.1`, `::1`, `localhost`). Neither
prints a private key.

## One-command Linux build + smoke

From the repository root:

```bash
./scripts/build_wallet.sh
```

That installs PyInstaller if needed, writes:

* `web/public/download/addition-wallet-testnet`
* `web/public/download/addition-wallet-cli-testnet`

then runs `scripts/smoke_wallet_binary.py` (binary starts; non-loopback RPC is
refused).

GUI needs `python3-tk` on the build machine if you want the windowed binary.
`--cli` works without a display.

## Windows `.exe`

On a Windows machine with Python 3.10+:

```powershell
powershell -File scripts\build_wallet.ps1
```

Output: `web\public\download\addition-wallet-testnet.exe` and
`addition-wallet-cli-testnet.exe`.

## Run

```bash
additiond --network testnet
./web/public/download/addition-wallet-testnet --cli getinfo
```

Override loopback only:

```bash
ADDITION_LOCAL_RPC_HOST=127.0.0.1 ADDITION_LOCAL_RPC_PORT=8545 \
  ./web/public/download/addition-wallet-testnet --cli getinfo
```

`ADDITION_LOCAL_RPC_HOST=8.8.8.8` must exit non-zero with
`refuses non-loopback`.

## Site

`/download/` on the public site links these files and stays labeled
testnet / local. The public host has no write RPC.
