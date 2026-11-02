# Mainnet onion seeds

Mainnet bootstrap uses **Tor v3 onion seeds only**: no DNS seeds, no clearnet default.
No seed address is compiled into the binary.

## Where the seeds come from

`additiond --mainnet` reads its seed list from the first of:

1. `--seeds-file PATH` (the file must exist)
2. `$ADDITION_SEEDS_FILE`
3. `./seeds-mainnet.txt` (working directory; ignored if absent)

`--bootstrap HOST:PORT` (repeatable) or `bootstrap_peers` in the config override it.

Format: one `<56-char-v3>.onion:28546` per line; `#` comments and blank lines are allowed.
Each entry is fully checked (base32, version byte 3, SHA3-256 checksum per rend-spec-v3).
**Any invalid line makes the daemon refuse to start** (fail closed). Duplicates are dropped.

The repo ships `seeds-mainnet.txt` with **no entries**: no seed has been published yet.
Without seeds the daemon warns and you must pass
`--tor-proxy 127.0.0.1:9050 --bootstrap <seed>.onion:28546`.

Check a file offline: `python3 scripts/tor/seed-node/check_seed_file.py seeds-mainnet.txt`.

## Creating a real seed (operator)

On each seed machine (its own host, not a shared server):

```sh
sudo apt install tor
sudo scripts/tor/seed-node/setup_seed_hidden_service.sh
```

The script adds `scripts/tor/seed-node/torrc.seed` to `/etc/tor/torrc`, restarts Tor,
and prints **only** the public line `xxxx.onion:28546`. The private key stays in
`/var/lib/tor/addition_mainnet_seed/`: never print, copy or commit it, and back it up
encrypted offline (losing it changes the address).

Run the node with `scripts/tor/seed-node/addition-seed.service` (systemd, onion-only).

Then add the printed line to `seeds-mainnet.txt` through a reviewed PR, run the checker,
and ship the file with releases. Aim for at least 3 seeds run by different people/hosts.
