# ZK Circuit v1 — real building blocks + fail-closed product path

Status: **circuit work in progress / not live**. Live privacy remains SHA3-512
opening with `privacy_claim=opening_not_zk`. The zk RPC path stays fail-closed
(`claim=zk_pending`). `zk_circuit_status` remains `not_proven` until Jeremy GO
wires a production verifier for the opening circuit. See `docs/PRIVACY_REAL_V1.md`.

Brand: **ADDITION** only.

## REAL vs still-pending

| Piece | Status | Label / caveat |
| :--- | :--- | :--- |
| Circuit statement + witness / public-input schema | **REAL** | typed C++ |
| Canonical public-input encoding (domain-separated) | **REAL** | Fiat–Shamir-ready string |
| Named constraints enum (`C_cm`, `C_nf`, …) | **REAL** | enumeration only until evaluated |
| SHA3-512 opening self-test | **REAL** hash check | **NOT** ZK |
| Named-constraint evaluator (`C_cm`/`C_nf` via opening) | **REAL** | **`constraint_check_not_zk`** (witness visible) |
| R1CS field evaluator + value-conservation / square circuits | **REAL** arithmetic CS | **`constraint_check_not_zk`** — not a SNARK |
| Toy Fiat–Shamir Schnorr PoK (discrete log) prove + verify | **REAL** NIZK for toy DL statement | **toy only** — not SHA3 opening; not PQ; not live |
| Lab Groth16 SNARK (Poseidon opening) | **REAL** prove+verify behind `ADDITION_ZK_SNARK_V1` | lab only — see `docs/ZK_SNARK_V1.md`; not live SHA3 claim |
| Production prover that emits opening-circuit acceptances | **Scaffold** | fail-closed; refuses to emit |
| On-node `PrivacyZkVerifier` for mint/spend | **Scaffold** | `backend_wired()==false` |
| Live product claim `zk_v1` / `zk_circuit_status=proven` | **Forbidden** | needs Jeremy GO + real opening-circuit verifier |

**Constraint check ≠ ZK.** Passing `zk_r1cs_evaluate` or
`zk_circuit_v1_eval_opening_constraints` means the witness satisfies the named
relation under a local evaluator that **sees the witness**. That is not
zero-knowledge and must not be marketed as a proof system for private mint/spend.

**Toy Schnorr ≠ opening ZK.** `zk_toy_schnorr_*` generates and verifies a real
Fiat–Shamir Schnorr proof of knowledge of `x` for `Y = g^x mod p` over a fixed
~256-bit safeprime group (OpenSSL BN). Tests prove accept/reject. It does **not**
encode `C_cm`/`C_nf`, does **not** hide a SHA3 trapdoor from a node, is **not**
post-quantum, and must **not** flip `privacy_claim` or `zk_circuit_v1_proven()`.

## Goal statement (still the target)

Prove knowledge of trapdoor `r` such that commitment `cm` and nullifier `nf`
match the existing v1 opening relation — **without** sending `r` to the node.

Live opening (node learns `r`):

```text
cm = SHA3-512("cm|v1|" || decimal(amount) || "|" || trapdoor_hex)
nf = SHA3-512("nf|v1|" || cm || "|" || trapdoor_hex)
```

Target zk_v1: same relation inside a production proof system; verifier sees only
public inputs + proof validity.

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

Value conservation across recipient + change notes has a **REAL R1CS evaluator**
for the linear toy `in == out + change` (`zk_circuit_v1_eval_value_conservation_r1cs`).
Full spend conservation + Merkle membership remain future work.

## Constraint scaffolding + evaluation

| ID | Kind | Informal constraint | Evaluator |
| :--- | :--- | :--- | :--- |
| `C_cm` | Mint + Spend | `cm == SHA3-512("cm\|v1\|" \|\| amount \|\| "|" \|\| r)` | REAL SHA3 opening check (**not ZK**) |
| `C_nf` | Mint + Spend | `nf == SHA3-512("nf\|v1\|" \|\| cm \|\| "|" \|\| r)` | REAL SHA3 opening check (**not ZK**) |
| `C_nf_fresh` | Spend (ledger) | nullifier not previously used | unimplemented in circuit eval |
| `C_value_conserved` | Spend | `in == out + change` (toy linear) | REAL R1CS field eval (**not ZK**) |
| `C_note_member` | Spend (future) | note in committed set / Merkle root | unimplemented |

Self-test API (`zk_circuit_v1_self_test_opening`) and named-constraint eval share
the opening relation. They must not set `claim=zk_v1`.

## Toy proof module (real prove + verify in tests)

```text
zk_toy_schnorr_keygen → (Y, x)
zk_toy_schnorr_prove  → (R, s)   Fiat–Shamir via SHA3-512
zk_toy_schnorr_verify → accept / reject
```

Domain: `addition.zk_toy_schnorr|v1|p|g|Y|R`. Invalid / tampered / empty proofs
reject. Success does **not** wire `FailClosedPrivacyZkVerifier`.

## Prover / verifier contract (fail-closed product path)

```text
zk_circuit_v1_proven() == false   →  no acceptances
prover.prove_*()                  →  always false + clear error
verifier.backend_wired()          →  false
verify_*()                        →  always false
claim on reject path              →  zk_pending (never zk_v1 success)
live getinfo privacy_claim        →  opening_not_zk
```

Fake proof blobs (empty, garbage hex, magic “CLAIM_ZK_V1” prefixes, ML-DSA wrap
bytes, toy Schnorr bytes reused as mint proofs) must be rejected.

## Backend candidates (none claimed live for opening)

PQ-friendly hash argument (FRI/STARK-style) for SHA3-in-circuit, lattice/hash ZK
library, or hybrid with ML-DSA-87 envelopes. The toy Schnorr DL proof is a
**building-block demo only** (classical assumption). A separate **lab** Groth16 /
Poseidon path ships in `docs/ZK_SNARK_V1.md` (opt-in `ADDITION_ZK_SNARK_V1`;
trusted setup; **not** the live SHA3 statement; not the toy Schnorr path).

Pick and wire a production opening backend only when compile + tests prove verify
can succeed **and** Jeremy GO allows claim cutover.

## Checklist (PR gate)

- [x] Mainnet `memory_hard` target `0x000000FFFFFFFFFF` untouched
- [x] No fake ZK product claims; constraint eval labeled `constraint_check_not_zk`
- [x] Toy Schnorr prove+verify covered by tests; live claim stays `opening_not_zk`
- [x] `zk_circuit_v1_proven()==false`; production prover/verifier fail-closed
- [x] ML-DSA-87 + SHA3-512 `pq_mode=strict` kept
- [x] Ship only what tests prove
