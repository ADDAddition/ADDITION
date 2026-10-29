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
2. TXB3 raw encoding (pubkey/sig as bytes, not hex) and BLKDATA without the outer hex: ~4x
   smaller. Wire-protocol change -> needs version negotiation. ~32 tx/block.
3. Raise kMaxLineBytes (e.g. 4 MiB => ~140 txs/block x4 with TXB3 => ~550). Two-phase rollout:
   first ship receivers accepting large lines, later raise the miner budget. **Prerequisite:**
   RpcNetworkServer spawns an unbounded thread per connection; add a connection cap and
   per-peer memory budget first or 4 MiB lines become a memory DoS.
4. Consensus max block bytes/tx count: currently none in Chain::validate_block. Should be added
   (hard-fork-ish rule; a decision for Jeremy).
5. Signature "aggregation": ML-DSA has no native aggregation. Realistic levers: pubkey registry
   (tx references a registered key id: saves 2592 B/tx, ~35%), Falcon-512 (~666 B sig, ~897 B pk;
   algorithm change, harder constant-time signing), STARK/SNARK proof of N ML-DSA verifies
   (research grade, prover cost large). None implemented.

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
