# Client-held keys: RPCs for the mobile app

The phone keeps its ML-DSA-87 wallet key and its AMLS1 identity, ratchet and session state.
The node only builds unsigned transactions, checks signatures, gossips them, and stores encrypted envelopes.
These RPCs are all on the local RPC (127.0.0.1, one line per request, space-separated, hex arguments).
Expose them to a phone only through your own authenticated tunnel. Do not expose them as a public RPC.

## Signing rules

- **Transactions:** sign the UTF-8 bytes of the `sign_hash` hex string (`hash_transaction`, SHA3-512) with ML-DSA-87. Send the signature as hex, with or without the `pq=` prefix.
- **Votes:** sign the bytes of `message_hex`, decoded from hex, exactly as returned by `vote_build`.
- Never send a private key or seed to the node. No RPC here accepts one.

## Staking (external signer)

| RPC | Returns |
|---|---|
| `get_nonce <address>` | `nonce=<n> address=… height=…` |
| `stake_build <address> <pubkey_hex> <amount> [fee]` | `sign_hash=… nonce=… fee=…` |
| `stake_submit <address> <pubkey_hex> <amount> <fee> <nonce> <sig_hex>` | `ok:stake_lock hash=… ticket=<txid:idx> fee=… nonce=…` |
| `unstake_build <address> <pubkey_hex> <ticket_id> [fee]` | `sign_hash=… nonce=… fee=…` |
| `unstake_submit <address> <pubkey_hex> <ticket_id> <fee> <nonce> <sig_hex>` | `ok:stake_unlock hash=… ticket=…` |
| `vote_build <ticket_id> [approve 1\|0]` | `voted_height=… voted_hash=… approve=… selected=0\|1 message_hex=…` |
| `vote_submit <ticket_id> <voted_height> <voted_hash> <approve> <sig_hex>` | `ok:vote ticket=… height=…` (SVOTE gossiped) |

- **How submit works:** `*_submit` rebuilds the same transaction from the arguments, so pass back the exact `fee` and `nonce` that `*_build` returned. If the chain moved (nonce taken or inputs spent), the submit fails. In that case call build again.
- **Errors:**
  - `address/pubkey mismatch`: the address does not match the public key.
  - `invalid signature`: the signature does not verify against the public key.
  - `fee too low`: the fee is under the current minimum.
- **Remote autovote (optional):** poll `stake_selected` or `vote_build` on each new tip. When `selected=1`, sign `message_hex` on the device and call `vote_submit`.
- **Node-held keys still work:** `stake_autovote on <wallet>`, `stake` and `unstake` are unchanged for regtest wallets kept on the node.

## AMLS1 messaging (relay only)

The node never sees the identity key, the ratchet or the plaintext.

| RPC | Purpose |
|---|---|
| `msg_kp_submit <pkg_hex>` | Publish a key package the client signed. The node verifies the ML-DSA-87 signature and expiry, and the index must not belong to another identity. The package is then replicated as `KPKG`. Returns `ok verified=ml-dsa-87 fingerprint=… index=… expires=… package=…` |
| `msg_kp_get <handle\|index32hex>` | Returns `package=<hex>`. The client must verify the package itself and pin the fingerprint. |
| `msg_env_post <PMSG\|v1\|…>` | Post an envelope the client built (ML-KEM-1024 + AES-256-GCM, format in `include/addition/pm_relay.hpp`). Returns `ok stored msg_id=…` or `ok duplicate …`. Relays forward it. |
| `msg_env_fetch <mailbox> [max≤256]` | First line is `envelopes=<n>`, then one `PMSG|v1|…` line per envelope, oldest expiry first. mailbox = first 128 bits of SHA3-512(recipient KEM pk). |
| `msg_env_delete <msg_id> <token_hex>` | Delete an envelope network-wide. The token is derived from the shared secret, so only the sender or the recipient can delete. |

- **Session frames:** AMLS1 session frames are carried inside the envelope ciphertext, up to 32 KiB (`PmRelay::kMaxFrame`). Envelope storage limits (TTL 60 s to 7 days, count and byte limits) are in `pm_relay.hpp`.
- **Node-held wallets still work:** `msg_kp_publish`, `msg_session_*`, `msg_send` and `msg_read` are unchanged for regtest. The two modes can mix: a node-held wallet can read an envelope that a client posted, and the reverse.

## Tests

`tests/test_regtest_client_signing.py` (ctest `test_regtest_client_signing`) uses throwaway regtest keys. It checks:

- Staking:
  - external-signed stake, the remote vote that unlocks mining, and unstake;
  - a wrong-key signature is rejected, and so is a replay.
- Messaging:
  - a tampered key package is rejected;
  - a package is relayed between nodes;
  - an envelope is posted and fetched;
  - a malformed envelope is rejected;
  - a node-held wallet can read a client-posted envelope.
