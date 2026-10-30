# Post-quantum payment channels (experimental, unaudited)

Status: research prototype on `feat/channels`. Not audited. Test keys only.

## On-chain primitive: Script v1
A minimal policy language (`src/script.cpp`):
`pk(addr)`, `sha3(hex128)`, `after(h)` (absolute height, CLTV), `older(d)` (relative to the
UTXO creation height, CSV), `and(a,b)`, `or(a,b)`.

* The output address is `sh1` + sha3_512("ADD/script/v1|" + script).
* Each input carries a witness: `script;nsigs;pub:sig...;npre;preimage...`. Every witness item has to be used.
* The witness is excluded from the txid (no malleability of pre-signed child txs). The block merkle root commits to the wtxid.
* The sighash is `"ADD/sighash/v1|" + txid`, signed with ML-DSA-87. It covers all inputs and outputs.
* Script txs have an empty signer, pubkey and signature, and no account nonce. Scripts are verified in
  `validate_transaction` unconditionally. Legacy txs that carry witnesses are rejected.
* UTXOs now record their creation height, which `older()` needs.
* Wire format: witnesses ride in the TXV3 witness section (kind 1, after the shielded field), written only when an input has a witness. Requires peer protocol 4; witness txs/blocks are withheld from proto 2/3 peers. The old TXB3 channel encoding is retired.
  **This needs coordination with the -net work before any deploy.**

## Channel construction (`src/channel.cpp`)
* Funding output: `and(pk(A),pk(B))`.
* Commitment tx (one per side and state; nonce = state number; the funder pays fee 1):
  * to_local: `or(and(pk(other),sha3(R_n)),and(pk(holder),older(D)))`
  * to_remote: the counterparty's plain address
  * HTLC: `or(and(pk(revoker),sha3(R_n)),or(and(pk(receiver),sha3(H)),and(pk(offerer),after(T))))`
* Revocation uses hash secrets: R_n = sha3(seed|n). Revealing R_n lets the peer claim every output of the old state
  in one penalty tx.
* Update (lock-step): sign the peer's commitment n+1, then verify it, then exchange the revoke for n plus the next revocation hash.
* Closes:
  * Cooperative: a 2-of-2 spend of the funding output with no timelocks.
  * Unilateral: broadcast the latest signed commitment. to_local is swept after CSV D, HTLCs settle via
    preimage or by timeout after CLTV T.
  * Penalty: broadcasting a revoked state lets the peer sweep everything.
* Multi-hop: `route_payment` chains HTLCs with the same hash across hops, with per-hop fee and expiry delta
  and fail-back on error. Tested with 3 nodes (2 hops).

## RPC (trusted local interface only, not in the remote allowlist)
`channel_open <funder_wallet> <peer_wallet> <capacity> [push]`, `channel_pay <id> <from_wallet> <amount>`,
`channel_close <id> [force_a|force_b]`, `channel_list`. Both peers are local wallets (loopback).
State is in-memory only and lost on restart.

## Measurements (box, single thread, ML-DSA-87 via liboqs 0.12.0)
* Off-chain payment (2 signs + 2 verifies): about 171–205 payments/s.
* 2-hop HTLC payment: about 35–50 payments/s.
* Coop-close witness: about 29 KB (key plus signature hex-encoded).

## Watchtower notes (not implemented)
* A tower needs, per revoked state, (commitment txid → encrypted penalty blob). The key is derived from the txid,
  so the tower learns nothing until a breach.
* Because hash revocation gives O(n) storage, replace it with a shachain or a per-commitment key tree.
* The penalty must confirm before CSV D expires. D sets the tower's response window.
* Large PQ witnesses (~29 KB+) make penalty fees significant, so pre-agree a fee reserve.

## Known gaps and audit caveats
* There are no second-stage HTLC txs (HTLC outputs pay directly). The interaction between the revocation path and HTLC timeouts is simplified.
* Revocation storage is O(n), and the full state history is kept.
* Messaging is in-process and lock-step. There is no concurrent update, no reconnection logic, and no persistence.
* Fee policy and size limits for large witnesses are not tuned. The mempool accepts signer-less txs only when every input has a witness.
* The preimage check on settle is done in the helper, not enforced by a protocol layer.
* Signatures are ML-DSA-87 only.
* The TXV3 witness section + proto-4 negotiation landed in release/v1 integration; needs external review.
