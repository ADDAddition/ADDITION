# ADDITION_FINAL Architecture (v1)

## Goals
- Production-oriented clean rewrite starting from zero
- Single canonical codebase
- Deterministic behavior and test coverage first

## Modules
- `block.*`: data model and hashing helpers
- `chain.*`: canonical ledger state and block validation
- `mempool.*`: pending transaction queue; rejects unsigned / empty-input / duplicate-outpoint junk
- `miner.*`: block template and SHA3-512 PoW (testnet); `memory_hard` on the `--mainnet` profile
- `crypto.*`: default ML-DSA-87 with thread-local OQS_SIG and parallel batch verify; opt-in `slh-dsa-shake-256s` only if `OQS_SIG_sign_with_ctx_str` works with a non-empty context; addresses are SHA3-512(scheme_id || 0x00 || pubkey_bytes) (128 hex)
- `rpc_server.*`: text-command RPC handling
- `wallet.*` / `wallet_store.*`: transaction creation, local `.wal` files, `wallet_send`
- `token_engine.*`: in-process token / AMM ledger (loopback RPC; not a public DEX)
- `privacy.*`: SHA3-512 commitment + nullifier opening (`claim=opening_not_zk`); ZK path scaffold in `privacy_zk.*` (`zk_pending`, fail-closed) — see `docs/PRIVACY_REAL_V1.md`
- `zk_circuit_v1.*`: circuit interface + witness/public-input schema + fail-closed prover (`zk_circuit_status=not_proven`) — see `docs/ZK_CIRCUIT_V1.md`. Not live ZK
- `fast_path.*`: separate `ADDITION_FAST_V1` profile — typed pipeline stages + SHA3-512 message digests (`pipeline_stages_typed_v1`); leader/execution/boot still fail-closed — see `docs/FAST_PATH_V1.md`. Does **not** loosen mainnet `memory_hard`
- `p2p.*` / `decentralized_node.*`: peer handshake, gossip, sync
- `btc_hygiene.*`: offline Bitcoin script classifier and signed ADDITION hygiene receipt (attestation rehearsal; does not move Bitcoin; not BIP-360)

## Current status
1. SHA3-512 hashing implemented with OpenSSL (`src/crypto.cpp`)
2. UTXO transaction model integrated (`TxInput`/`TxOutput` + `utxo_set_`)
3. Wallet/RPC build spends from available UTXOs (`wallet_send`, `tx_build` + `sendtx_signed*`)
4. Spend transactions require ML-DSA-87 (`pq=` signatures) in `pq_mode=strict`
5. Chain persistence: `--data-dir/blocks.dat` after each accepted block. UTXOs rebuilt by replay
6. Side-state (`tokens.dat`, `privacy.dat`, `staking.dat`, …) flushed after each successful write and on shutdown
7. P2P + public-read allowlist shipped. Write RPC stays `127.0.0.1`

## Next hardening phases
1. Optional stronger on-disk format (LevelDB/RocksDB) — text `blocks.dat` already persists height across restart
2. Native ADD lock into the privacy pool (notes are still a side ledger today)
3. Add reproducible release pipeline
