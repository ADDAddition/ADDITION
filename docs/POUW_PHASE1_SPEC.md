# ADDITION PoUW Phase 1 Specification (Storage + AI Compute + Private TTL Messaging)

Version: 1.0
Date: 2026-03-06
Status: Phase 1 (specification only)

---

## 1. Objectives

Define a production-oriented architecture for:

1. **PoUW Storage Layer** (decentralized cloud)
2. **PoUW Compute Layer** (AI/compute jobs)
3. **Private TTL Messaging** (E2E encrypted, expiring)
4. **Economic/security model** (rewards, staking, slashing)
5. **Web product architecture** (portal + explorer + cloud + AI jobs + messaging)

This spec is compatible with current ADDITION runtime posture:
- PQ signatures (`ml-dsa-87`)
- strict RPC hardening
- native token/swap/bridge/privacy modules
- optional EVM compatibility layer

---

## 2. High-level architecture

ADDITION target model:

- **Core chain (native, PQ)**
  - consensus + settlement + rewards + slashing state
- **PoUW coordinators (deterministic on-chain logic + off-chain workers)**
  - storage challenge lifecycle
  - compute job lifecycle
- **Worker nodes**
  - storage workers (store encrypted chunks)
  - compute workers (run AI/compute tasks)
- **Verifier network**
  - challenge verification
  - replicated result validation
- **Client layer**
  - wallet + cloud uploader + AI job submitter + private messaging
- **Web portal (professional)**
  - explorer, cloud console, AI jobs dashboard, messaging console, staking/slashing view

---

## 3. PoUW Storage Layer specification

### 3.1 Roles

- **Client**: uploads encrypted file, pays storage contract
- **Storage worker**: commits to storing chunks
- **Verifier**: validates periodic proofs
- **Chain**: records commitments/challenges/verdicts/rewards/slashes

### 3.2 Data model (logical)

- `StorageDeal`
  - `deal_id`
  - `client_addr`
  - `content_root` (Merkle root of encrypted chunks)
  - `chunk_count`
  - `replication_factor`
  - `start_height`
  - `end_height`
  - `price_per_epoch`
  - `worker_set[]`
  - `status`

- `StorageCommitment`
  - `deal_id`
  - `worker_addr`
  - `sealed_commitment`
  - `collateral_locked`

- `StorageChallenge`
  - `challenge_id`
  - `deal_id`
  - `worker_addr`
  - `random_seed`
  - `chunk_index`
  - `due_height`

- `StorageProof`
  - `challenge_id`
  - `proof_blob_hash`
  - `verdict` (pending/pass/fail)

### 3.3 Protocol flow

1. Client encrypts file client-side and chunks it.
2. Client computes chunk Merkle root and opens `StorageDeal`.
3. Workers register commitments + lock collateral.
4. Chain emits deterministic random challenges per epoch.
5. Workers submit proofs before `due_height`.
6. Verifiers/consensus validate proofs.
7. Rewards distributed on pass; slashing on fail/miss.

### 3.4 Security properties

- Files remain encrypted off-chain.
- Chain stores commitments/proof hashes, not plaintext.
- Challenge randomness must be unbiased and verifiable.
- Repeated non-response or invalid proofs => escalating slash.

---

## 4. PoUW Compute (AI jobs) specification

### 4.1 Roles

- **Requester**: submits compute/AI job + budget
- **Compute workers**: execute jobs
- **Validators**: verify output correctness/probabilistic consistency
- **Chain**: settlement, reward, slashing, reputation

### 4.2 Job model

- `ComputeJob`
  - `job_id`
  - `requester_addr`
  - `job_type` (inference/training/data-processing)
  - `input_ref` (content-addressed)
  - `determinism_profile` (strict/probabilistic)
  - `max_latency_sec`
  - `reward_budget`
  - `min_reputation`
  - `status`

- `ComputeAssignment`
  - `job_id`
  - `worker_addr`
  - `collateral_locked`
  - `assigned_at`

- `ComputeResult`
  - `job_id`
  - `worker_addr`
  - `output_ref`
  - `result_hash`
  - `proof_ref`
  - `submitted_at`

- `ComputeValidation`
  - `job_id`
  - `validator_addr`
  - `verdict`
  - `score`

### 4.3 Validation modes

- **Deterministic jobs**: hash equality across replicas
- **Probabilistic AI jobs**:
  - consensus by validator quorum
  - bounded-drift checks
  - optional challenge re-run samples

### 4.4 Anti-cheat

- mandatory collateral for workers
- timeout slash
- invalid result slash
- reputation decay for non-performance
- ban threshold for repeated fraud

---

## 5. Private TTL Messaging specification

## 5.1 Scope

Provide private messaging with expiration semantics using:
- E2E encryption off-chain payloads
- minimal on-chain anchors/metadata
- key deletion strategy for practical self-destruction behavior

## 5.2 Important constraint

Blockchains are immutable. “100% deletion on-chain” is not literal. Model is:
- encrypted content
- expiring access keys
- metadata TTL enforcement
- optional payload garbage collection by storage workers

## 5.3 Message model

- `PrivateMessageEnvelope`
  - `msg_id`
  - `sender_addr`
  - `recipient_addr`
  - `ciphertext_ref`
  - `created_at`
  - `ttl_sec`
  - `expire_at`
  - `policy` (single-read, auto-burn, forward-forbidden)
  - `status` (active/expired/destroyed)

- `MessageKeyMaterial`
  - ephemeral key agreement output (off-chain)
  - never stored plaintext on-chain

## 5.4 Protocol

1. Sender obtains recipient pre-key bundle.
2. Sender encrypts payload (E2E) and uploads ciphertext.
3. Sender submits envelope anchor to chain with TTL.
4. Recipient decrypts while message active.
5. On expiration: envelope state becomes expired and key material is discarded.

## 5.5 Optional advanced policies

- read-once receipts (signed)
- delayed delivery windows
- burn-on-open
- group messaging with rotating group keys

---

## 6. Economic model (PoUW + chain security)

## 6.1 Reward split

Per epoch reward partition:

- `R_security`: base chain security reward
- `R_storage`: storage proof success pool
- `R_compute`: compute validated results pool
- `R_validators`: verifier/validator compensation

Constraint:

`R_total = R_security + R_storage + R_compute + R_validators`

## 6.2 Collateral and slashing

- workers lock collateral before acceptance
- slashing events:
  - missed challenge
  - invalid proof/result
  - equivocation/fraud
- slash severity tiers:
  - minor miss
  - repeated miss
  - confirmed fraud (max slash + temporary ban)

## 6.3 Reputation

- rolling score per worker
- positive signals: uptime, valid submissions, low latency
- negative signals: misses, invalid outputs, disputes lost
- minimum score gates job eligibility

## 6.4 Abuse resistance

- requester deposits budget escrow
- anti-spam fee for job/deal submission
- max outstanding jobs per account tier

---

## 7. Consensus integration (Phase 2 target)

Consensus remains chain-safe first. PoUW additions are integrated as deterministic state transitions:

- storage challenges generated from chain randomness
- compute settlement based on validator quorum rules
- slashing/rewards encoded in block application rules

No opaque/non-deterministic execution inside block validation.

---

## 8. RPC design (Phase 2 target)

### 8.1 Storage RPC namespace

- `storage_deal_create`
- `storage_commit_register`
- `storage_challenge_list`
- `storage_proof_submit`
- `storage_deal_status`
- `storage_worker_status`

### 8.2 Compute RPC namespace

- `compute_job_submit`
- `compute_job_assign`
- `compute_result_submit`
- `compute_validate_submit`
- `compute_job_status`
- `compute_worker_status`

### 8.3 Private messaging RPC namespace

- `msg_prekey_publish`
- `msg_send_ttl`
- `msg_inbox`
- `msg_mark_read`
- `msg_destroy`
- `msg_status`

All sensitive write methods require strict signature verification (PQ/hybrid policy aligned).

---

## 9. Web architecture (professional portal)

## 9.1 Product areas

1. **Explorer**
   - blocks, tx, validator stats, slashing events
2. **Cloud Storage Console**
   - upload, deals, replicas, proof status, billing
3. **AI Jobs Console**
   - submit jobs, track execution, validator results, costs
4. **Private Messaging**
   - secure inbox, TTL controls, read receipts, burn policies
5. **DeFi/Wallet**
   - send, staking, swap, token creator, bridge, governance
6. **Node/Worker Ops**
   - worker registration, reputation, earnings, penalties

## 9.2 Technical stack target

- Frontend: Next.js + TypeScript + Tailwind + component system
- API gateway: authenticated backend (no direct privileged RPC from browser)
- Realtime: websockets for job/proof/message status
- Observability: metrics + logs + tracing

## 9.3 Security baseline

- strict authn/authz at API layer
- signed requests for critical operations
- hardware-backed keys recommended for operators
- rate limiting + abuse throttling

---

## 10. EVM compatibility strategy

Recommended mode: **Native-first + EVM compatibility layer**

- Native modules implement PoUW/storage/compute/messaging economics
- EVM compatibility exposes interoperability for wallets/tools/dApps
- Keep PQ-native identity and signature policy as canonical trust layer

---

## 11. Phase delivery plan

## Phase 1 (this document)
- finalized architecture/spec
- RPC namespaces
- state schemas
- reward/slash model
- web product blueprint

## Phase 2 (MVP implementation)
- implement storage/compute/message states + RPC
- implement verification + settlement flows
- deliver web v1 with all major sections

## Phase 3 (hardening)
- security audit + fuzz + chaos
- load testing at target scale
- anti-abuse tuning and SRE runbooks
- mainnet-candidate gates

---

## 12. Mainnet-candidate acceptance gates

Before official mainnet declaration:

- deterministic replay consistency across nodes
- sustained load test pass (storage + compute + messaging)
- slashing and dispute workflows validated
- external security audit findings addressed
- incident response and backup/recovery drills passed

---

## 13. Notes on “100% safe” claims

No distributed system is literally 100% safe. Practical target is:

- defense-in-depth architecture
- independent audits
- continuous monitoring
- controlled rollout and rollback readiness

This spec is designed for that operational reality.
