# ADDITION methods vs six public chains

In-repo technical note. Not a homepage slogan page. Numbers below are
only those a local test node measured; they are not a live mainnet or a
published TPS claim.

## What ADDITION does

| Area | ADDITION method in this tree |
| --- | --- |
| Ledger | Simple UTXO verify (`src/chain.cpp`) |
| PoW | `sha3_512` header hash (testnet) |
| Signatures | ML-DSA-87 via liboqs (`pq=` prefix) |
| Throughput path | Parallel ML-DSA verify + mempool that rejects unsigned / duplicate-outpoint junk |
| Sync | P2P handshake + block fetch; `sync` errors with `no peer` if the node has no peer |
| Privacy | SHA3-512 opening: `cm\|v1\|amount\|trapdoor` and `nf\|v1\|commitment\|trapdoor`. Claim: `opening_not_zk` |

`protocol_status` reports `measured_*` fields from the last local mine/verify
and labels `research_goal_tps=100000` as `research_goal_is_not_a_measurement=true`.
PoUW storage still uses a first-nibble parity check (`check=first_nibble_parity`),
not a storage proof.

## What this tree does not copy

| Chain | Problem they solve | ADDITION does not import |
| --- | --- | --- |
| Bitcoin | Simple UTXO + PoW | Bitcoin Core, SHA-256d, ECDSA/secp256k1 |
| Ethereum | General settlement / EVM | go-ethereum, EVM bytecode, `eth_sendRaw` |
| Solana | High-throughput execution | Sealevel, Turbine, Gulf Stream |
| XRP | Fast agreement among listed validators | rippled, RPCA |
| Monero | Hidden amounts / rings | RingCT, Bulletproofs, Monero code |
| Zcash | Circuit privacy | Groth16, Halo2, librustzcash |

Read their public docs only to name the problem. The implementation here is
ADDITION’s: SHA3-512 + ML-DSA-87 + opening privacy + original mempool/mine/sync.

## What a local node can prove

Run `tests/test_core_path_smoke.py` against a built `additiond`. That script
mines, sends a PQ payment, spends an opening note, runs `benchmark_objective`
(no mempool inject), mines again, then syncs a second node. Report only the
fields the node printed (`last_mine_ms`, `last_verify_per_sec`,
`bench_verify_per_sec`, heights after sync).

Do not treat `research_goal_tps` as a measured result. Do not claim Groth16,
Bulletproofs, Halo2, a DEX, Filecoin, or a live public mainnet from this page.
