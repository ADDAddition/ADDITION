# Fast Path v1 — design (separate network profile)

Status: **scaffold only**. The live public ADDITION product remains
`ADDITION_MAINNET_V1` with `pow_algorithm=memory_hard` at target
`0x000000FFFFFFFFFF`. This document describes a **separate** network profile
(`ADDITION_FAST_V1`) so high-throughput work does **not** pretend memory-hard
PoW is Solana-speed.

Brand: **ADDITION** only.

## Gap

| Profile | Consensus today | Throughput honesty |
| :--- | :--- | :--- |
| Live mainnet `ADDITION_MAINNET_V1` | `memory_hard` PoW (1 MiB × 16 rounds / attempt) | `last_tps` is local mine telemetry only; not a marketed Solana TPS |
| Research testnet `ADDITION_TESTNET_V1` | `sha3_512` header PoW | Same: no invented measured TPS |
| Target fast path `ADDITION_FAST_V1` | Leader / pipeline / parallel execution (not PoW-as-throughput) | `throughput_claim=none` until a real pipeline ships and is measured |

Do **not** loosen mainnet `memory_hard` / `0x000000FFFFFFFFFF` to “go faster.”
Do **not** stop mining or touch the public seed hasher. Do **not** invent
measured TPS. `research_goal_tps` stays labeled
`research_goal_is_not_a_measurement=true`.

## Chosen approach (ADDITION, C++20)

A **separate network id** so the fast path cannot be confused with mainnet PoW:

1. **Network profile** — `network_id=ADDITION_FAST_V1`, `network_mode=fast`,
   distinct genesis timestamp / data dir / ports from mainnet and testnet.
2. **Consensus sketch (not shipped)** — leader schedules, pipelined block
   propagation, and parallel transaction execution. PoW is not the latency
   path; mainnet keeps memory-hard PoW for the public product.
3. **Crypto unchanged** — ML-DSA-87 / SHA3-512, `pq_mode=strict`. Privacy
   labels stay honest (`opening_not_zk` live; `zk_pending` stub).
4. **This PR** — design doc, genesis/config stubs, CLI `--fast` /
   `--network fast`, getinfo fields that distinguish profiles, fail-closed
   boot if the pipeline is incomplete, tests that mainnet PoW knobs are
   untouched and the fast profile does not claim measured Solana TPS.

### Leader / pipeline / execution sketch (future PRs)

```text
clients --> RPC/ingest --> leader scheduler --> execution workers
                                |                    |
                                v                    v
                         block pipeline         state apply
                                |
                                v
                         replica verify / commit
```

| Stage | Intent | Shipped in this PR? |
| :--- | :--- | :--- |
| Leader election / rotation | Who proposes the next batch | No |
| Pipeline stages | Overlap verify / propagate / commit | No |
| Parallel execution | Non-conflicting txs across workers | No |
| Measured throughput RPC | Honest bench of the fast profile | No |

## Fail-closed rules

- `--network fast` / `--fast` **refuses to boot** while
  `fast_path_pipeline_shipped()==false` (scaffold incomplete).
- Loading `genesis-fast.json` / `config-fast.toml` onto mainnet or testnet
  modes is rejected (`network_id` / `network_mode` mismatch).
- Mainnet `--mainnet` path is unchanged: `ADDITION_MAINNET_V1`,
  `memory_hard`, target `0x000000FFFFFFFFFF`.
- getinfo / protocol_status must never print a measured Solana TPS figure.
  `throughput_claim=none`. Research goals stay non-measurements.

## getinfo honesty

| Field | Mainnet | Fast scaffold (when ready to boot) |
| :--- | :--- | :--- |
| `network_id` | `ADDITION_MAINNET_V1` | `ADDITION_FAST_V1` |
| `consensus_path` | `memory_hard_pow` | `leader_pipeline_scaffold` |
| `fast_path_status` | `not_this_network` | `scaffold_incomplete` (until shipped) |
| `fast_path_shipped` | `false` | `false` until pipeline lands |
| `throughput_claim` | `none` | `none` |
| `research_goal_is_not_a_measurement` | `true` (protocol_status / getinfo) | `true` |

## What is not shipped yet

- No public `ADDITION_FAST_V1` seed, peers, or explorer product surface
- No leader, pipeline, or parallel executor
- No claim that ADDITION matches Solana (or any chain) measured TPS
- No change to mainnet mining, difficulty, or seed hasher

## Honesty checklist (PR gate)

- [x] Mainnet `memory_hard` target `0x000000FFFFFFFFFF` untouched
- [x] No GCP seed hasher / mining stop
- [x] Fast path is a **separate** profile (`ADDITION_FAST_V1`)
- [x] Fail-closed while pipeline incomplete
- [x] No invented TPS; `research_goal_is_not_a_measurement=true`
- [x] `pq_mode=strict` / ML-DSA-87 / SHA3-512 kept
- [x] Privacy labels stay `opening_not_zk` / `zk_pending`
- [x] Site/whitepaper: speed path in progress; live product is memory_hard mainnet
