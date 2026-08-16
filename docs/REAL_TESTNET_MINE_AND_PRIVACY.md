# Real local testnet: mined block + SHA3 opening privacy

Research testnet only. Contact: [contact@additionblockchain.com](mailto:contact@additionblockchain.com)

This document describes what a running `additiond --network testnet` actually
does after this change. It does **not** claim a live mainnet, Groth16,
Bulletproofs, ZK-Shield, a DEX, or a token sale.

## What was broken

1. Live `mine` used a 1 MiB × 16-round memory-hard header hash on the single
   RPC accept thread. With the default target (`0x0000FFFFFFFFFFFF`) that
   search did not produce a block in 180s, and `getinfo` could not run while
   `mine` was in flight.
2. `privacy_mint_zk` / `privacy_spend_zk` verify an ML-DSA-87 signature of the
   strings `mint|...` / `spend|...`. That is a signature wrap, not a
   commitment/nullifier relation and not a circuit.

## What this slice ships

### 1. Testnet PoW that mines a block in bounded time

- Testnet default: `pow_algorithm=sha3_512`. The header SHA3-512 digest’s
  first 64 bits are compared to `difficulty_target`. That is a real hash
  check, not a stub that skips PoW.
- `mine` has a **30s deadline**. On testnet a block is expected well inside
  that bound (about 2^16 cheap hashes at the default target).
- Each RPC connection is handled on its own thread. `handle_command` is
  mutex-serialized so chain state stays consistent.
- The experimental `--network mainnet` *profile* still uses `memory_hard`.
  That profile is not a live public network. A live memory-hard mine is
  **not** demonstrated here and may hit the 30s deadline.

`getinfo` prints `pow_algorithm=sha3_512` on testnet.

### 2. SHA3-512 commitment + nullifier opening (not ZK)

Smallest real proving path that fits this slice:

```text
commitment = SHA3-512("cm|" || amount || "|" || trapdoor)
nullifier  = SHA3-512("nf|" || trapdoor)
```

The verifier **recomputes both hashes** and rejects a mismatched trapdoor.
The trapdoor is the proof. The node learns the opening. This is **not**
zero-knowledge, not a circuit, not Groth16, not Bulletproofs, not ZK-Shield.

`privacy_mint_zk` / `privacy_spend_zk` are unchanged and still an ML-DSA wrap.
Do not treat them as a circuit.

Owner/amount sealing still needs `ADDITION_PRIVACY_MASTER_KEY` (≥32 chars).

## Commands

Trusted local RPC only (`127.0.0.1`, default `8545`):

```bash
./build/additiond --network testnet
printf 'getinfo\n' | nc 127.0.0.1 8545
printf 'createwallet alice\n' | nc 127.0.0.1 8545
printf 'createwallet bob\n' | nc 127.0.0.1 8545
printf 'mine <alice_address>\n' | nc 127.0.0.1 8545
printf 'wallet_send alice <bob_address> 10 1\n' | nc 127.0.0.1 8545
printf 'mine <alice_address>\n' | nc 127.0.0.1 8545
printf 'getbalance <bob_address>\n' | nc 127.0.0.1 8545
```

Privacy opening (set the master key first):

```bash
export ADDITION_PRIVACY_MASTER_KEY='addition-research-privacy-master-key-32'
printf 'privacy_note_prepare 25\n' | nc 127.0.0.1 8545
printf 'privacy_mint_open alice 25 <commitment> <nullifier> <trapdoor>\n' | nc 127.0.0.1 8545
printf 'privacy_spend_open alice <note_id> bob 10 <trapdoor>\n' | nc 127.0.0.1 8545
printf 'privacy_status\n' | nc 127.0.0.1 8545
```

A garbage trapdoor returns `error: opening relation rejected`.

## Tests against a running additiond

```bash
cmake -S . -B build -DADDITION_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`test_live_mine_and_privacy` starts `additiond`, mines at least one block in
≤30s, confirms an ML-DSA-87 `wallet_send` in a second block, then mint/spend
through `sha3_opening`.

`test_chain_persist_restart` mines N blocks, SIGKILLs `additiond`, restarts
with the same `--data-dir`, and checks that height and block hashes survive.
`--data-dir/blocks.dat` is the chain file; a restart does not wipe height.

`test_privacy` checks the hash relation in-process (good opening, garbage
trapdoor, double-spend).
