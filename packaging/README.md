# ADDITION wallet packaging (mainnet / local)

Package the local wallet helpers into a file a stranger can download and run.
This is a **mainnet / local** helper for `ADDITION_MAINNET_V1`. It is not a hosted
web wallet and not a custodial product. Write RPC stays loopback.

Public contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

Flutter under `client/addition_app` is the desktop wallet (Windows + Linux; #55).
See [`client/addition_app/README.md`](../client/addition_app/README.md) and
`scripts/setup_desktop.sh` / `scripts/setup_desktop.ps1` for first-run helpers.

This packaging path still uses PyInstaller around:

* `web/addition_wallet_gui.py` — GUI + `--cli` (node wallet files, loopback RPC)
* `web/addition_wallet.py` — caller-disk CLI (keys on the caller, loopback RPC)

Both refuse non-loopback RPC hosts (`127.0.0.1`, `::1`, `localhost`). Neither
prints a private key. Default write port is `8546` (mainnet).

## One-command Linux build + smoke

From the repository root:

```bash
./scripts/build_wallet.sh
```

That installs PyInstaller if needed, writes:

* `web/public/download/addition-wallet-mainnet`
* `web/public/download/addition-wallet-cli-mainnet`

then runs `scripts/smoke_wallet_binary.py` (binary starts; non-loopback RPC is
refused).

GUI needs `python3-tk` on the build machine if you want the windowed binary.
`--cli` works without a display.

## Windows `.exe`

On a Windows machine with Python 3.10+:

```powershell
powershell -File scripts\build_wallet.ps1
```

Output: `web\public\download\addition-wallet-mainnet.exe` and
`addition-wallet-cli-mainnet.exe`. This Linux packaging host does not publish a
`.exe`.

## Run

```bash
additiond --mainnet --local-rpc-port 8546
./web/public/download/addition-wallet-mainnet --cli getinfo
```

Override loopback only:

```bash
ADDITION_LOCAL_RPC_HOST=127.0.0.1 ADDITION_LOCAL_RPC_PORT=8546 \
  ./web/public/download/addition-wallet-mainnet --cli getinfo
```

`ADDITION_LOCAL_RPC_HOST=8.8.8.8` must exit non-zero with
`refuses non-loopback`.

## Site

`/download/` on the public site links these files and is labeled mainnet / local.
The public host has no write RPC. Research testnet remains a separate chain.
