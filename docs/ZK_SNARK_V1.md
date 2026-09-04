# ZK SNARK v1 — real Groth16 prove+verify (lab / optional)

Status: **REAL zkSNARK prove+verify path** for a schema-analogous opening
statement, behind an opt-in flag. This is **not** live production privacy.

Live product remains SHA3-512 opening with `privacy_claim=opening_not_zk`.
`zk_circuit_status` stays `not_proven` for the **production SHA3-512** privacy
statement. Do not market this PR as shipping production ZK privacy.

Brand: **ADDITION** only.

## Proving system

| Item | Value |
| :--- | :--- |
| System | **Groth16** (arkworks) |
| Curve | BN254 |
| Setup | **Trusted setup** — circuit-specific CRS (`trusted_setup_circuit_specific`) |
| Library | `zk/snark_v1` Rust crate (`addition_snark_v1`), linked from C++20 |
| Enable flag | `ADDITION_ZK_SNARK_V1=1` (default off = fail-closed) |

Groth16 requires a trusted setup. Tests run a fresh in-process circuit-specific
setup. A production multi-party ceremony is **out of scope** and not claimed.

## Statement proven (REAL)

Mint-shaped opening (schema-analogous to `C_cm` / `C_nf` in `ZK_CIRCUIT_V1.md`):

```text
public:  amount, cm, nf
witness: trapdoor r
cm == Poseidon(TAG_CM, amount, r)
nf == Poseidon(TAG_NF, cm, r)
```

Poseidon is over BN254 Fr (arkworks sponge parameters). The verifier accepts a
real Groth16 proof of this relation without learning `r`.

## Gap vs production privacy

| Piece | Production today | This SNARK path |
| :--- | :--- | :--- |
| Commitment / nullifier hash | **SHA3-512** domain strings | **Poseidon** (BN254 Fr) lab hash |
| Live `privacy_claim` | `opening_not_zk` | unchanged (`opening_not_zk`) |
| `zk_circuit_status` (SHA3 statement) | `not_proven` | stays `not_proven` |
| Default `PrivacyZkVerifier` | fail-closed stub | still fail-closed |
| Optional lab verifier | n/a | wired only if `ADDITION_ZK_SNARK_V1=1` |

SHA3-512-in-circuit was not shipped here (heavy R1CS). The Poseidon circuit is a
real SNARK for a related opening shape; it is **not** a proof of the live SHA3
opening relation. Production cutover to `zk_v1` requires a verifier for the
**SHA3** privacy statement (or an explicit product decision to change the note
hash) plus ceremony / PQ story — none of that is claimed here.

## What this is NOT

- Not PR **#82** / `vision/zk-real-v1` toy Fiat–Shamir Schnorr for discrete-log
  knowledge. That path is HOLD / not merging; this PR does not merge it.
- Not a transparent STARK/FRI backend.
- Not post-quantum (Groth16 / BN254 pairing assumptions).
- Not a flip of mainnet `memory_hard` / `0x000000FFFFFFFFFF`.
- Not permission to set `privacy_claim=zk_v1` or `zk_circuit_status=proven`.

## REAL vs pending

| Piece | Status |
| :--- | :--- |
| Groth16 setup → prove → verify (Poseidon opening) | **REAL** (tests green) |
| Tampered proof / wrong public inputs reject | **REAL** |
| Opt-in flag fail-closed when unset | **REAL** |
| C++ wrapper + optional verifier | **REAL** (lab only) |
| Docs label the Poseidon vs SHA3 gap | **REAL** |
| Production SHA3-512 circuit SNARK | **Pending** |
| Live `privacy_claim=zk_v1` | **Forbidden** here |
| Trusted setup ceremony / ceremony transcript | **Pending** |
| PQ-friendly proof system | **Pending** |

## API surface

- Rust/C ABI: `zk/snark_v1/include/addition_snark_v1.h`
- C++: `include/addition/zk_snark_v1.hpp` / `src/zk_snark_v1.cpp`
- Tests: `tests/test_zk_snark_v1.cpp` (+ Rust unit test in the crate)

## Checklist (PR gate)

- [x] Mainnet `memory_hard` target `0x000000FFFFFFFFFF` untouched
- [x] No fake live ZK claims; `opening_not_zk` kept
- [x] `zk_circuit_status` not set to `proven` for SHA3 production statement
- [x] Optional verifier behind `ADDITION_ZK_SNARK_V1` fail-closed flag
- [x] ML-DSA-87 + SHA3-512 `pq_mode=strict` kept
- [x] Ship only what tests prove (setup/prove/verify + reject paths)
