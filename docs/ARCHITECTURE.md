# ADDITION_FINAL Architecture (v1)

## Goals
- Production-oriented clean rewrite starting from zero
- Single canonical codebase
- Deterministic behavior and test coverage first

## Modules
- `block.*`: data model and hashing helpers
- `chain.*`: canonical ledger state and block validation
- `mempool.*`: pending transaction queue
- `miner.*`: block template and simplified PoW loop
- `rpc_server.*`: text-command RPC handling
- `wallet.*`: transaction creation and submission

## Current status (v2 in progress)
1. SHA3-512 hashing implemented with OpenSSL (`src/crypto.cpp`)
2. UTXO transaction model integrated (`TxInput`/`TxOutput` + `utxo_set_`)
3. Wallet/RPC now build spends from available UTXOs
4. Non-coinbase transactions now require signer + signature validation
5. Chain persistence: `--data-dir/blocks.dat` (text headers + txs). UTXOs are
   rebuilt by replay. Not LevelDB/RocksDB.

## Next hardening phases
1. Replace temporary deterministic signatures with real asymmetric keys
2. Add p2p networking layer and peer sync
3. Optional stronger on-disk format (LevelDB/RocksDB) — text `blocks.dat` already persists height across restart
4. Add authenticated JSON-RPC server
5. Add reproducible release pipeline
