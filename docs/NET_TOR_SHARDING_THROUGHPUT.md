# Networking, Tor, throughput and sharding (feat/net-tor)

## Measured throughput (this box, ML-DSA-87, tests/bench_throughput 40)
* Tx wire size inside BLKDATA: ~29.8 KB (pubkey 2592 B + sig 4627 B, hex in TXB2, then the
  whole block hex-encoded again => x2 then x2 on the key material).
* P2P line limit kMaxLineBytes = 256 KiB => **8 txs per relayable block => ~0.13 tx/s at 60 s blocks.**
  Before this branch the miner packed up to 500 txs and produced blocks peers could never fetch
  (self-orphaning / split risk). The miner now enforces a relay byte budget (policy, not consensus).
* validate_transaction: ~2.85k tx/s uncached, ~6.6k tx/s with the new verified-signature cache
  (each signature was verified 3x: mempool, miner, block). CPU is not the bottleneck; bytes are.

## Options to raise throughput (ordered by safety)
1. Done: miner byte budget, signature cache (positive results only, keyed SHA3-512(pk|msg|sig)).
2. Done (Phase 2): binary wire v3 (`TXV3`/`BLV3`, raw bytes, base64 framing, proto 3/4
   negotiation). Witnesses ride in the TXV3 witness section.
3. Done (Phase 2): 4 MiB max line with connection caps and large-message slots.
4. Done (Phase 2): consensus `max_block_weight` (2 MiB) checked in `add_block`.
5. Signature "aggregation": ML-DSA has no native aggregation. The pubkey registry is done
   (saves 2592 B/tx, ~35%). ML-DSA-87 is the only signature scheme; no alternative
   lattice scheme will be added. STARK proof of N ML-DSA verifies remains research-grade.

## Sharding evaluation (honest)
include/addition/shard_manager.hpp is an unreferenced header stub: no cross-shard messaging,
no data-availability sampling, no committee rotation, no fraud/validity proofs. Comparison:
* NEAR Nightshade: single chain of chunks, rotating validator committees, receipts for
  cross-shard txs, needs PoS committee sampling. ADDITION is PoW: no committees to sample.
* Polkadot parachains: shared security from a staked relay chain + validity proofs (PVF).
  Same PoS dependency; huge engineering effort.
* Danksharding: data blobs + KZG commitments + DAS for rollups. KZG is not post-quantum.
Under PoW, splitting hashpower across shards divides security per shard (1% attack). Realistic
gain with current wire format: zero until the byte limits above are fixed (a single shard can't
fill even 8 txs/min usefully). Recommendation: keep sharding OUT of consensus; do items 2-4
first (~100-500x headroom) and consider a rollup/DA layer later. No sharding code was enabled.

## P2P / Tor / Dandelion++ summary
* Peer scoring [-10,100], temp bans (default 1 h) instead of permanent, seeds never dropped
  (60 s backoff), table cap 125 with lowest-score eviction, ADDR/ADVERTISE results go to a
  capped candidate set and are promoted only after a successful handshake.
* Tor: v3 .onion endpoints (validated), SOCKS5 with ATYP=domain (no DNS leak), --tor-proxy,
  --onion-only (refuses clearnet, binds P2P/public RPC to loopback), torrc template in scripts/tor.
* Dandelion++ (--dandelion): epoch 600 s, 10% diffuser, 2 relays per epoch, per-source relay
  mapping, local txs always stem, 30-45 s embargo then fluff (black-hole protection).
* Known limits: inbound peers are identified by self-chosen labels (Sybil-able, can fill the
  table; Dandelion may stem back to the sender), no handshake gating of inbound messages,
  the node core is not thread-safe (all pre-existing).

## Phase 2 — speed (wire v3, weight, registry)

Approved by Jeremy ("C'est sur je veux optimiser toute").

- **Wire v3**: binary `TXV3`/`BLV3` records (hex fields stored as raw bytes,
  SHA3-512 checksum32), framed as `b64:<base64>` on the line protocol. Proto
  `"3"`; `"2"` still accepted. Handshake ACK returns the negotiated version
  (signed); client retries once with v2 on mismatch. v3 payloads are transcoded
  to hex v2 for v2 peers. `BLV3` carries an extension-section count (0 here),
  now carrying staking votes/slashes.
- **Consensus weight** = TXV3 bytes (+framing). `max_block_weight` = 2 MiB
  (config, 64 KiB..3 MiB-64 KiB), checked first in `add_block`.
- **Pubkey registry**: first key-carrying spend registers signer->pubkey; later
  txs may omit the ~2.6 KB key. Snapshotted/restored on reorg, rebuilt on replay.
- **Signature scheme**: ML-DSA-87 only (FIPS 204). Falcon/FN-DSA was removed and is rejected.
- **Caps**: 4 MiB max line, 256 KiB default; 128 inbound / 8 per IP (64 loopback
  for Tor); 16 large-message slots; ~92 MiB worst-case buffering.
- **Shielded** field: carried/hashed/weighted, consensus-rejected (fail closed).
  A 228 KB bundle weighs 238,369 B -> 8 per block.

`bench_throughput 1500` (20 s blocks, 2 MiB weight, this box):

| scheme / key | tx weight | txs/block | tx/s | peer cold add_block |
|---|---|---|---|---|
| ML-DSA-87 + pubkey | 7505 | 278 | 13.9 | 157 ms |
| ML-DSA-87 registry | 4913 | 426 | 21.3 | 64 ms |

Re-measured 2026-10-29 after Falcon removal: unchanged, ~21 tx/s with the registry.

Full blocks are ~2.79 MB on the wire (< 3 MiB relay, < 4 MiB line).
Previous hex v2: ~0.4 tx/s.
