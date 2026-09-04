# ZK Circuit v1 — interface + schema (fail-closed progress)

Status: **circuit work in progress / not live**. No production zero-knowledge
proof system ships in this PR. Live privacy remains SHA3-512 opening with
`privacy_claim=opening_not_zk`. The zk RPC path stays fail-closed
(`claim=zk_pending`). See `docs/PRIVACY_REAL_V1.md`.

Brand: **ADDITION** only.

## What this PR is (REAL vs scaffold)

| Piece | Status |
| :--- | :--- |
| Circuit statement + witness / public-input schema | **REAL** (documented + typed in C++) |
| Canonical public-input encoding (domain-separated) | **REAL** (byte string for a future Fiat–Shamir transcript) |
| Constraint scaffolding (named opening-relation constraints) | **REAL** (interface + enumeration; not a SNARK/STARK) |
| Self-test hook: witness → recompute SHA3-512 opening | **REAL** hash check; **NOT** zero-knowledge |
| Lab Groth16 SNARK (Poseidon opening) | **REAL** prove+verify behind `ADDITION_ZK_SNARK_V1` — see `docs/ZK_SNARK_V1.md` |
| Prover that emits acceptances / proving keys for **SHA3** zk_v1 | **Scaffold** — fail-closed; refuses to emit |
| On-node verifier that accepts **production SHA3** proofs | **Scaffold** — fail-closed; `backend_wired()==false` |
| Live product claim `zk_v1` | **Forbidden** until a proven backend verifies the production privacy statement |

Do **not** market this as live ZK. Do **not** treat the opening self-test as a
proof. Do **not** weaken mainnet `memory_hard` / `0x000000FFFFFFFFFF`.

## Goal statement

Prove knowledge of trapdoor `r` such that commitment `cm` and nullifier `nf`
match the existing v1 opening relation — **without** sending `r` to the node.

Live opening (node learns `r`):

```text
cm = SHA3-512("cm|v1|" || decimal(amount) || "|" || trapdoor_hex)
nf = SHA3-512("nf|v1|" || cm || "|" || trapdoor_hex)
```

Target zk_v1: same relation inside a proof system; verifier sees only public
inputs + proof validity.

## Public inputs / witness schema

### Mint (`ZkCircuitKind::Mint`)

| Field | Role | Encoding |
| :--- | :--- | :--- |
| `amount` (`uint64`) | Public | decimal ASCII in domain string |
| `commitment` | Public | 128 lowercase hex (SHA3-512 digest) |
| `nullifier` | Public | 128 lowercase hex |
| `trapdoor` | **Witness only** | 64 hex (32 bytes); never on the wire for zk_v1 |

Canonical public-input domain string:

```text
addition.zk_circuit_v1|mint|<amount>|<commitment_hex>|<nullifier_hex>
```

### Spend (`ZkCircuitKind::Spend`) — minimal v1

| Field | Role | Encoding |
| :--- | :--- | :--- |
| `amount` | Public (spent value slice) | decimal ASCII |
| `note_commitment` | Public (ledger commitment being spent) | 128 hex |
| `nullifier` | Public | 128 hex |
| `recipient_tag` | Public policy tag (not a hide-amount claim) | UTF-8 string |
| `trapdoor` | **Witness only** | 64 hex |

Canonical public-input domain string:

```text
addition.zk_circuit_v1|spend|<amount>|<note_commitment_hex>|<nullifier_hex>|<recipient_tag>
```

Value conservation across recipient + change notes is **out of scope** for this
slice (future constraint IDs). Membership against a note Merkle root is also
future work.

## Constraint scaffolding (named, not proven)

These are the algebraic / hash constraints a future backend must implement.
Listing them here does **not** mean they are proven in-process.

| ID | Kind | Informal constraint |
| :--- | :--- | :--- |
| `C_cm` | Mint + Spend | `cm == SHA3-512("cm|v1|" \|\| amount \|\| "|" \|\| r)` |
| `C_nf` | Mint + Spend | `nf == SHA3-512("nf|v1|" \|\| cm \|\| "|" \|\| r)` |
| `C_nf_fresh` | Spend (ledger) | nullifier not previously used (public set check) |
| `C_value_conserved` | Spend (future) | in_value == out_value + change |
| `C_note_member` | Spend (future) | note in committed set / Merkle root |

Self-test API (`zk_circuit_v1_self_test_opening`) recomputes `C_cm` / `C_nf`
with the witness via the same SHA3-512 path as `PrivacyPool::verify_opening`.
That proves the **relation hash** is wired correctly. It is **not** a ZK proof
and must not set `claim=zk_v1`.

## Prover / verifier contract (fail-closed)

```text
zk_circuit_v1_proven() == false   →  no acceptances
prover.prove_*()                  →  always false + clear error
verifier.backend_wired()          →  false
verify_*()                        →  always false
claim on reject path              →  zk_pending (never zk_v1 success)
live getinfo privacy_claim        →  opening_not_zk
```

Fake proof blobs (empty, garbage hex, magic “CLAIM_ZK_V1” prefixes, ML-DSA wrap
bytes) must be rejected. Claiming “zk” without a valid verified proof fails
closed.

## Backend candidates (none claimed live for SHA3)

Same as `PRIVACY_REAL_V1.md`. A **lab** Groth16 / Poseidon path ships in
`docs/ZK_SNARK_V1.md` (opt-in flag; trusted setup; **not** the live SHA3 statement).
PR #82 toy Fiat–Shamir Schnorr is a different HOLD path and is not this SNARK.

Pick and wire a production-SHA3 (or explicit hash-migration) backend in a later PR
only when compile + tests prove verify can succeed **and** product claims are
updated deliberately.

## Checklist (PR gate)

- [x] Mainnet `memory_hard` target `0x000000FFFFFFFFFF` untouched
- [x] No fake ZK claims; self-test labeled not-ZK
- [x] `opening_not_zk` remains live product claim
- [x] Prover/verifier fail-closed while `zk_circuit_v1_proven()==false`
- [x] ML-DSA-87 + SHA3-512 `pq_mode=strict` kept
- [x] Ship only what tests prove
