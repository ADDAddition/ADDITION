# Shielded pool (`addz`) — EXPERIMENTAL, NOT AUDITED

> **Status:** experimental. Consensus-gated by `ChainConfig::shielded_enabled`
> (default **false** on every network). **An external cryptographic audit is
> required before enabling on mainnet.**

## Goal
Hide sender, receiver and amount (Zcash-Sapling/Orchard-style), with encrypted
memos usable as private messages, using **post-quantum** primitives only.

## Why this design
| Option | PQ | Reviewed implementation | Verdict |
|---|---|---|---|
| Monero (CLSAG ring sigs + Bulletproofs + stealth) | no (DLog) | yes | rejected |
| Zcash Groth16/Halo2 | no (pairings / DLog) | yes | rejected (old `zk_snark_v1` was Groth16/BN254 — removed) |
| Lattice ring sigs / MatRiCT / lattice ZK (LaBRADOR…) | yes | **no** production library | rejected (would be homemade crypto) |
| **Hash-based STARK (RISC Zero zkVM 3.x, succinct receipts)** | yes (hash/FRI) | audited, widely deployed | **chosen** |

Primitives: SHA-256 (commitments, nullifiers, Merkle tree, inside the proof),
RISC Zero STARK (Poseidon2/SHA-256 FRI, transparent setup), ML-KEM-1024 (liboqs)
for note encryption, AES-256-GCM (OpenSSL), ML-DSA-87 for transparent inputs.
The Groth16 wrapper of RISC Zero is **never** accepted (not PQ): only
`InnerReceipt::Succinct` receipts, dev-mode disabled at compile time.

## Note model
```
owner = SHA256("ADD/owner/v1" || nk)              (nk = spending secret)
cm    = SHA256("ADD/cm/v1" || le64 v || owner || rho || r)
nf    = SHA256("ADD/nf/v1" || nk || rho)
rho_j = SHA256("ADD/rho/v1" || bind || nf_1..nf_n || le32 j)   (prevents faerie-gold)
tree  = depth-32 SHA-256 Merkle tree of ALL commitments
```
The STARK (`zk/shielded_stark/core`, guest `shielded_spend`) proves, for a
public journal `ADDSHLD1 || bind || anchor || vpub_in || vpub_out || nfs || cms`:
each input note is in the tree at `anchor`, the prover knows its `nk`, `nf` is
correct and unique, output `cm`s are well-formed with derived `rho`, and
`Σin + vpub_in = Σout + vpub_out` with all values u64 (range proof). ≤4 in / ≤4 out.

`bind = SHA256("ADD/bind/v1" || transparent tx fields || vpubs || ciphertexts || image_id)`
makes the proof non-malleable w.r.t. the rest of the transaction.

## Receiver privacy / memos
Address `addz1<hex(owner || ML-KEM-1024 pk)>`. Each output: fresh ML-KEM
encapsulation, key = SHA3-512("ADD/notekey/v1"||ss||ct)[..32], AES-256-GCM over
fixed-size `value || r || memo_len || memo[512]` (AAD = KEM ct). Recipients
trial-decrypt and re-check `cm`. Fresh encapsulation per output gives one-time,
unlinkable ciphertexts (relies on ML-KEM anonymity / ANO-CCA).

## Transaction types
* t→z (shield): transparent inputs signed with ML-DSA-87, `vpub_in` into pool.
* z→z: no transparent inputs/signer; authorized by the STARK alone; `vpub_out = fee`.
* z→t (unshield): `vpub_out = transparent outputs + fee`.
Consensus: anchor must be one of the last 256 roots; nullifiers unspent (also
within the block); commitments fresh; coinbase may not carry a bundle.

## Known limitations / caveats
* Not audited. STARK soundness ~96–100 bits conjectured (RISC Zero params).
* Proofs are large (hundreds of KB) and CPU proving is slow (see test metrics).
* Number of inputs/outputs and `vpub` amounts at the pool boundary are public.
* No outgoing viewing key; no diversified addresses; wallet tree path is O(n).
* Proving runs locally on the wallet; witness never leaves the process.
* Anchor window: >256 shielded tx per block could evict a wallet's anchor.
* The legacy `PrivacyPool` ("opening" notes) is transparent and deprecated; the
  former `privacy_*_zk*` / `zk_mint` paths (server-signed "proofs") were removed.
