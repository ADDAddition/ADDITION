#pragma once
// Sphinx-style onion for multi-hop HTLC routing (post-quantum variant).
// Each hop gets its own ML-KEM-1024 encapsulation; per-hop keys are derived
// from the shared secret with HMAC-SHA3-512. The routing block is a fixed
// size (kMaxHops entries) stream-encrypted with AES-256-CTR and authenticated
// with HMAC-SHA3-512/256 bound to the payment hash. A hop peels its entry,
// shifts the block left and refills the tail from its keystream (Sphinx
// filler), so every forwarded packet has the same size and a hop learns only
// its own entry: the next channel (or "final"), the amount and the expiry to
// forward. It learns neither the payer, the payee nor its position.
#include <cstdint>
#include <string>
#include <vector>

namespace addition::onion {

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::size_t kMaxHops = 4;
inline constexpr std::size_t kKemPk = 1568, kKemSk = 3168, kKemCt = 1568;
inline constexpr std::size_t kHdr = 64, kMac = 32;
inline constexpr std::size_t kEntry = kHdr + kMac + kKemCt;
inline constexpr std::size_t kRouting = kMaxHops * kEntry;
inline constexpr std::size_t kPacket = kKemCt + kRouting + kMac;

struct Hop {
    Bytes pubkey;            // hop's ML-KEM-1024 route key
    std::string next_chan;   // channel to forward on ("" for the final hop)
    std::uint64_t amount{0}; // amount to forward (final: amount to receive)
    std::uint64_t expiry{0}; // outgoing absolute expiry (final: minimum expiry)
};

struct Peeled {
    bool final{false};
    std::string next_chan;
    std::uint64_t amount{0};
    std::uint64_t expiry{0};
    Bytes next_packet;  // kPacket bytes, empty when final
    std::string replay_tag;
};

bool keygen(Bytes& pk, Bytes& sk);
bool build(const std::vector<Hop>& hops, const std::string& assoc, Bytes& packet, std::string& error);
bool peel(const Bytes& sk, const Bytes& packet, const std::string& assoc, Peeled& out, std::string& error);

}  // namespace addition::onion
