# Privacy Real v1 — design (scaffold)

Status: **scaffold only**. Live privacy remains SHA3-512 opening.
`getinfo` / `privacy_status` must keep `privacy_claim=opening_not_zk` until a real
zero-knowledge verifier accepts proofs on-node.

## Gap

| Path | What the node learns | Claim label |
| :--- | :--- | :--- |
| Live today: `privacy_mint_open` / `privacy_spend_open` | Trapdoor (opening), amount in the clear on the wire | `opening_not_zk` |
| Legacy: `privacy_mint_zk` / `privacy_spend_zk` | Public mint/spend string; ML-DSA-87 signature wrap | `mldsa_wrap_not_zk` |
| Target: real ZK mint/spend | Commitment, nullifier, proof — **not** the trapdoor | `zk_v1` only after verify works |

`opening_not_zk` is a correct hash relation and double-spend defense. It is **not**
zero-knowledge: the verifier recomputes

- `SHA3-512("cm|v1|" || amount || "|" || trapdoor)`
- `SHA3-512("nf|v1|" || commitment || "|" || trapdoor)`

and therefore learns the trapdoor. Vision item 1 (node must not learn the trapdoor)
requires a proof system that checks the same relation without the witness.

Do **not** relabel `opening_not_zk` as ZK. Do **not** treat the ML-DSA wrap as a circuit.

## Chosen approach (ADDITION, C++20)

Prefer a **PQ-friendly hybrid** that reuses the existing commitment/nullifier strings:

1. **Public objects (unchanged shape)**  
   Commitment and nullifier stay SHA3-512 digests so opening notes and future ZK notes
   share one note ledger format.

2. **Witness (private)**  
   Trapdoor (+ amount for mint; spend binds note membership / value conservation).

3. **Proof (future PRs)**  
   A statement roughly: “I know trapdoor `r` such that `cm` and `nf` match the v1
   opening relation for amount `v`, and (for spend) this nullifier has not been used
   and value is conserved.” The node verifies the proof and never receives `r`.

4. **Backend plan (multi-PR)**  
   Full circuit + proving keys are **out of scope for this PR**. Interfaces land first
   and are **fail-closed**. Candidate backends (pick one in a later PR; none claimed live):

   - Hash-based argument over SHA3-512 (FRI / STARK-style) — PQ-friendly, large proofs.
   - Lattice / hash ZK library bound from C++20 — research dependency risk.
   - Hybrid: PQ signatures (ML-DSA-87, `pq_mode=strict`) for authorization envelopes;
     separate ZK only for the opening relation (still not “unbreakable forever”).

5. **This PR (privacy scaffold #77)**  
   - `PrivacyZkVerifier` + `FailClosedPrivacyZkVerifier`  
   - RPC `privacy_mint_zk_v1` / `privacy_spend_zk_v1` reject every proof  
   - Labels: live `opening_not_zk`; zk path responses may say `claim=zk_pending`;
     `claim=zk_v1` is forbidden until `backend_wired()==true` and verify can succeed

6. **Circuit slice (#80 / `docs/ZK_CIRCUIT_V1.md`)**  
   Typed witness / public-input schema, constraint scaffolding, SHA3 opening
   self-test (not ZK), and fail-closed prover. Still **not live ZK**.

7. **Lab SNARK slice (`docs/ZK_SNARK_V1.md`)**  
   Real Groth16 prove+verify (arkworks / BN254) for a **Poseidon** opening
   analogous to `C_cm` / `C_nf`. Opt-in via `ADDITION_ZK_SNARK_V1=1`. Trusted
   setup. Does **not** flip `privacy_claim` or mark the SHA3 circuit `proven`.
   Not PR #82 toy Schnorr.

## Migration

| Stage | Live `privacy_claim` (getinfo) | ZK path RPC claim | Behavior |
| :--- | :--- | :--- | :--- |
| Now | `opening_not_zk` | `zk_pending` | Opening works; zk_v1 fail-closed |
| Proofs wired + tests green | still `opening_not_zk` until cutover | `zk_v1` on success | Parallel paths |
| Cutover (later PR) | may become `zk_v1` only if opening retired | `zk_v1` | Product decision |

Opening path stays. No silent rename. Roadmap field: `privacy_zk_roadmap=zk_pending`.

## Threat model

### Today (opening)

- **Node / operator** sees trapdoor, amount, owner tags (sealed at rest with
  `ADDITION_PRIVACY_MASTER_KEY`, but the opening RPC still presents the witness).
- **Network observer** of loopback RPC sees the same if they can read the socket.
- **Double-spend** blocked via nullifier + spent-commitment sets.
- **Not hidden**: amounts and links between mint and spend when trapdoors are logged.

### After real `zk_v1` (target)

- **Verifier** learns: public commitment, nullifier, amount policy / public inputs the
  circuit exposes by design, proof validity. Does **not** learn trapdoor.
- **Still not claimed**: perfect forever privacy, decoy anonymity sets, or network-layer
  metadata hiding. PQ signatures remain ML-DSA-87; crypto assumptions can age.
- **Fail-closed until then**: garbage or empty proofs must error; no note mint on the
  zk_v1 path; no `zk_v1` claim string on success paths that do not verify.

## PR gate checklist

- [x] Mainnet `memory_hard` target `0x000000FFFFFFFFFF` untouched
- [x] No GCP seed hasher / mining changes
- [x] `opening_not_zk` not relabeled as ZK
- [x] No invented TPS; `research_goal_tps` stays non-measurement
- [x] `economic_security` / height not faked here
- [x] zk_v1 path rejects without a real backend
