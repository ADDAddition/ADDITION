# AMLS1 — post-quantum 1:1 sessions (MLS-style, PQ primitives)

Not PGP. Built from: ML-DSA-87 (identity + key-package signatures), ML-KEM-1024
(handshake + ratchet), HMAC-SHA3-512 (KDF chains), AES-256-GCM (AEAD). Every frame
travels inside the existing PMSG envelope (ML-KEM-1024 + AES-256-GCM, padded,
mailbox-addressed), so relays see only the envelope.

## Key package (what gets discovered)
`AKP1 { index | identity_pub (ML-DSA-87) | init_pub (ML-KEM-1024) | relay_pub (ML-KEM-1024) | created | expires | sig }`
* `sig` = ML-DSA-87 by the identity key over the canonical TBS (suite string included).
* Lifetime ≤ 30 days; verifiers reject bad signature, wrong sizes, expiry, future `created`.
* Identity key is separate from the wallet spending key (no on-chain link by default).
* Fingerprint = SHA3-512(identity_pub)[:40 hex]; `msg_sessions` shows a per-pair safety number to compare out of band.

## Discovery
1. **Contact string** `addc1:<hex package>` (unlisted, `handle = -`): shared out of band. Best privacy.
2. **Opt-in directory**: `index = SHA3-512("ADDITION-KP-dir|v1|" + lower(handle))[:32 hex]`.
   Packages gossip as `KPKG|v1|<hex>` and are **replicated to every node** (cap 2048, expired ones purged),
   so `msg_kp_lookup` is answered locally — the query never leaves the node. Each relay verifies the
   ML-DSA-87 signature before storing/forwarding. First identity to claim a live index keeps it
   (TOFU); only the same identity can refresh it (newer `created`).

## Handshake (PQXDH-like, KEM-only)
Initiator: `ct1, ss1 = Encaps(peer.init_pub)`, fresh ratchet key `Q0`, transcript
`T = sid | H(peer pkg) | H(own pkg) | H(ct1) | H(Q0)` signed with ML-DSA-87; `RK0 = HMAC(H(T), ss1)`.
INIT frame carries the initiator's own signed package (so the responder learns and verifies who is talking and can reply).

## Ratchet (KEM double ratchet)
* Symmetric chains: `mk, ck' = HMAC(ck, 1), HMAC(ck, 2)` → per-message forward secrecy; keys wiped after use.
* Turn-based KEM step: on its first send after receiving a new peer ratchet key, a party encapsulates to it,
  rotates its own ML-KEM-1024 ratchet key, `RK, CKs = HMAC(RK, ss)` → post-compromise security after one round trip.
  Steps strictly alternate, so both sides evolve RK identically.
* Every header carries the sender's ratchet key, the step ciphertext of the current chain, previous-chain
  length and counter → out-of-order delivery (≤512 skipped, ≤256 stored keys); replays and tampering are rejected
  and never mutate state.

## RPC (loopback only)
`msg_kp_publish <wallet> [handle|-] [days]`, `msg_kp_lookup <handle|addc1:...>`,
`msg_session_start <wallet> <handle|addc1:...> <ttl> <text>`, `msg_session_send <wallet> <sid> <ttl> <text>`,
`msg_session_recv <wallet>`, `msg_sessions <wallet>`. Old `msg_*` (single-shot) unchanged.

## Privacy tradeoffs
* Listing a handle publishes a long-lived signed identity key under a guessable index (dictionary-enumerable):
  anyone can learn "handle X has a messaging identity" and correlate its rotations. Use unlisted contacts for unlinkability.
* Full replication avoids lookup leakage but costs every node storage/bandwidth (~10 KB per package) and lets anyone
  enumerate the directory. A PIR/bucketed lookup would reduce that cost later.
* The init key is last-resort (reused until rotated): the first message of a session has weaker forward secrecy until
  the first ratchet step. Rotate with `msg_kp_publish` often; old init keys are kept only until their package expires.
* KEM-only auth means INIT is ML-DSA-87 signed → **not deniable** (a recipient can prove the initiator started a session).
* TOFU on directory indexes: the first claimant wins; verify the fingerprint/safety number out of band.
* Session state, identity and init keys are stored 0600 on the node; a node compromise exposes current state
  (healed by the ratchet after a round trip, not retroactively).
* Relays see envelope sizes (padded to 256 B), timing and mailbox ids; INIT frames are larger (~19 KB) and thus distinguishable.

## Group plan (not implemented)
MLS (RFC 9420) structure with PQ primitives: ML-KEM-1024 as the HPKE KEM in a TreeKEM ratchet tree,
ML-DSA-87 leaf credentials = these key packages, epoch secrets via HMAC-SHA3-512, Commit/Welcome
messages carried as PMSG frames to each member's relay key, a delivery-service ordering rule (sequence by
the committer + epoch number, conflicts resolved by lowest commit hash). Sizes are large (ML-KEM ciphertexts per
path node), so start with small groups and a sender-keys fallback (per-member symmetric chains distributed
over the 1:1 AMLS1 sessions) before full TreeKEM.
