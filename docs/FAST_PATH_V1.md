# Fast Path v1 — design (separate network profile)

Status: **partial fill beyond scaffold**. The live public ADDITION product remains
`ADDITION_MAINNET_V1` with `pow_algorithm=memory_hard` at target
`0x000000FFFFFFFFFF`. This document describes a **separate** network profile
(`ADDITION_FAST_V1`) so high-throughput work does **not** pretend memory-hard
PoW is Solana-speed.

Brand: **ADDITION** only.

## What is REAL vs still scaffold

| Piece | Status |
| :--- | :--- |
| Network profile stubs (`ADDITION_FAST_V1`, genesis/config/CLI) | **Scaffold** (from #78) |
| Fail-closed `--fast` / `--network fast` boot | **REAL** (still refuses; pipeline not shipped) |
| getinfo labels (`throughput_claim=none`, `research_goal_is_not_a_measurement=true`) | **REAL** |
| Explicit pipeline stages + typed messages + SHA3-512 digests | **REAL** (local validation only) |
| Local `FastPipelineBatch` stage machine (deterministic apply order) | **REAL** (not consensus) |
| Leader election / rotation | **Scaffold** — not shipped |
| Parallel execution workers / state apply | **Scaffold** — not shipped |
| Replica verify / network commit | **Scaffold** — not shipped |
| Measured throughput RPC / published TPS | **Forbidden** until measured (`throughput_claim=none`) |
| Live public product | remains **memory_hard** mainnet |

`fast_path_slice=pipeline_stages_typed_v1` names this fill. It does **not** set
`fast_path_shipped=true` and does **not** allow boot.

## Gap

| Profile | Consensus today | Throughput label |
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
2. **Consensus sketch** — leader schedules, pipelined block propagation, and
   parallel transaction execution. PoW is not the latency path; mainnet keeps
   memory-hard PoW for the public product.
3. **Crypto unchanged** — ML-DSA-87 / SHA3-512, `pq_mode=strict`. Privacy
   labels stay accurate (`opening_not_zk` live; `zk_pending` stub).
4. **This fill PR** — typed pipeline stages + fail-closed message validation
   (SHA3-512 digests). Boot remains refused. No invented TPS.

### Leader / pipeline / execution sketch

```text
clients --> RPC/ingest --> leader scheduler --> execution workers
                                |                    |
                                v                    v
                         block pipeline         state apply
                                |
                                v
                         replica verify / commit
```

| Stage | Intent | Status |
| :--- | :--- | :--- |
| Typed stage enum + message kinds | Ordered ingest → schedule → execute → verify → commit | **REAL** (`pipeline_stages_typed_v1`) |
| SHA3-512 sealed message digests | Domain-separated `addition.fast_path_v1\|…` | **REAL** |
| Local batch stage machine | Deterministic apply; rejects OOO / bad digest / fake TPS magic | **REAL** (not consensus) |
| Leader election / rotation | Who proposes the next batch | No |
| Parallel execution | Non-conflicting txs across workers | No |
| Measured throughput RPC | Measured bench of the fast profile (when shipped) | No |

### Typed messages (research surface)

Domain-separated digest preimage (SHA3-512 → 128 hex):

```text
addition.fast_path_v1|<kind>|<batch_id>|<network_id>|<body>
```

| Kind | Advances stage to |
| :--- | :--- |
| `ingest_batch` | `ingested` |
| `schedule_ticket` | `scheduled` |
| `execution_receipt` | `executed` |
| `verify_ack` | `verified` |
| `commit_seal` | `committed` |

Fail-closed rejects: `network_id != ADDITION_FAST_V1`, wrong digest, out-of-order
kind, empty body, and bodies containing `CLAIM_FAST_LIVE` /
`CLAIM_MEASURED_TPS` / Solana-TPS slogans.

## Fail-closed rules

- `--network fast` / `--fast` **refuses to boot** while
  `fast_path_pipeline_shipped()==false` (full pipeline incomplete).
- Loading `genesis-fast.json` / `config-fast.toml` onto mainnet or testnet
  modes is rejected (`network_id` / `network_mode` mismatch).
- Mainnet `--mainnet` path is unchanged: `ADDITION_MAINNET_V1`,
  `memory_hard`, target `0x000000FFFFFFFFFF`.
- getinfo / protocol_status must never print a measured Solana TPS figure.
  `throughput_claim=none`. Research goals stay non-measurements.
- Typed stage progress must **not** flip `kFastPathPipelineShipped`.

## getinfo labels (Vision vs Live)

| Field | Mainnet | Fast profile |
| :--- | :--- | :--- |
| `network_id` | `ADDITION_MAINNET_V1` | `ADDITION_FAST_V1` |
| `consensus_path` | `memory_hard_pow` | `leader_pipeline_scaffold` |
| `fast_path_status` | `not_this_network` | `scaffold_incomplete` (until shipped) |
| `fast_path_shipped` | `false` | `false` until full pipeline lands |
| `fast_path_slice` | `pipeline_stages_typed_v1` | same (names the fill; not a ship claim) |
| `throughput_claim` | `none` | `none` |
| `research_goal_is_not_a_measurement` | `true` | `true` |

## What is not shipped yet

- No public `ADDITION_FAST_V1` seed, peers, or explorer product surface
- No leader election, parallel executor, or replica commit path
- No claim that ADDITION matches Solana (or any chain) measured TPS
- No change to mainnet mining, difficulty, or seed hasher
- `--fast` still **refuses to boot**

## PR gate checklist

- [x] Mainnet `memory_hard` target `0x000000FFFFFFFFFF` untouched
- [x] No GCP seed hasher / mining stop
- [x] Fast path is a **separate** profile (`ADDITION_FAST_V1`)
- [x] Fail-closed while pipeline incomplete
- [x] Typed stages + message digests with deterministic tests
- [x] No invented TPS; `research_goal_is_not_a_measurement=true`
- [x] `pq_mode=strict` / ML-DSA-87 / SHA3-512 kept
- [x] Privacy labels stay `opening_not_zk` / `zk_pending`
- [x] Site/docs: honest REAL vs scaffold status
