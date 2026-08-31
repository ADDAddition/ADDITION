# ADDITION local mining pool (coordinator)

Honest local tools — **not NiceHash**, not a public mine port.

## What this is

Node `mine` is an **in-process** trusted write RPC (loopback). There is no
external share/PoW split protocol. This coordinator:

1. Binds **loopback only** (`127.0.0.1:18555` by default)
2. Accepts worker registrations over JSON HTTP
3. **Serializes** `mine <reward> <threads>` to native TEXT RPC on loopback
   (default testnet `127.0.0.1:8545`) so many workers do not open concurrent
   blocking mine clients against the node
4. Counts a **share** as one completed mine attempt credited to the requesting
   worker (block found or deadline miss)

Prefer **testnet** (SHA3-512, mine deadline). Mainnet profile only via loopback;
height follows live `getinfo` (0 stays 0; ≥1 is factual — no “mainnet is live”).

## Linux

```bash
# terminal A — node (testnet write; bootstraps operator P2P by default)
./scripts/setup_desktop.sh --start-only --mode testnet

# terminal B — pool coordinator
python3 tools/mining_pool.py coordinator --node-port 8545 --reward miner1

# terminal C — worker (requests slots)
python3 tools/mining_pool.py worker --once --name desk1 --threads 2

curl -s http://127.0.0.1:18555/status | python3 -m json.tool
```

## Windows (WSL node + Windows wallet)

```powershell
wsl -e bash -lc "cd /path/to/ADDITION && ./scripts/setup_desktop.sh --mode testnet --start-only"
# In WSL or any Python on the same loopback:
python3 tools/mining_pool.py coordinator --node-port 8545
python3 tools/mining_pool.py worker --once --name win1 --threads 2
```

Pool HTTP stays `127.0.0.1:18555`. Do not bind `0.0.0.0`.

## Hard limits

- Never expose testnet/mainnet **write** publicly
- Never enable `mine` on public read (`38545` / `38546`) or LAN write (`18545` / `18547`)
- Coordinator **refuses** those ports and any non-loopback host
- Serializes one `mine` at a time on loopback write only (does not pile blocking clients on a public seed)
- No token sale, no Addison branding

## Investigation note

`mine` in `src/rpc_server.cpp` runs `Miner::mine_next_block` in-process (SHA3-512 on
testnet; memory_hard on mainnet profile). There is no external share/PoW split and no
submit-share RPC. This tool is therefore a **local coordinator**, not NiceHash.

## Tests

```bash
python3 -m unittest tests.test_mining_pool tests.test_evm_rpc_bridge
```
