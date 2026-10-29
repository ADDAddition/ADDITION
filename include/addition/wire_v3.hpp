#pragma once

// Wire format v3 ("proto 3"): compact binary tx/block encoding.
//
// Magics are TXV3/BLV3 (not TXB3/BLB3: fix/core already uses "BLB3" for its
// hex staked-block variant).
//
// v2 (TXB2/BLB2) carried pubkeys/signatures as hex strings inside a binary
// record that was hex-encoded again (and a tx inside a block a third time
// conceptually: hex(bin(hex))). v3 stores every hex-looking field as raw bytes
// with a 1-byte tag so the round-trip is exact (lossless for any string), and
// frames the line with base64 instead of hex ("b64:" prefix): ~4x smaller for
// signature-heavy txs.
//
// The canonical v3 binary size of a tx is also its consensus *weight*
// (see block.hpp tx_weight / kMaxBlockWeight).

#include "addition/block.hpp"

#include <cstdint>
#include <string>

namespace addition {
namespace wire {

inline constexpr int kProtoV2 = 2;
inline constexpr int kProtoV3 = 3;
inline constexpr const char* kB64Prefix = "b64:";

std::string base64_encode(const std::string& bin);
bool base64_decode(const std::string& text, std::string& bin);

// Tagged lossless string: tag 0 raw, 1 lowercase hex -> bytes, 2 "pq=" + lowercase hex -> bytes.
void write_tagged(std::string& out, const std::string& s);
bool read_tagged(const std::string& in, std::size_t& off, std::string& s);

std::string encode_tx_v3(const Transaction& tx);  // binary "TXV3..." incl. checksum
bool decode_tx_v3(const std::string& bin, Transaction& tx, std::string& error);

std::string encode_block_v3(const Block& block);  // binary "BLV3..." incl. checksum
bool decode_block_v3(const std::string& bin, Block& block, std::string& error);

// Hard caps applied by decoders before allocating (DoS: no attacker-chosen reserve()).
inline constexpr std::uint32_t kMaxFieldBytes = 4u * 1024u * 1024u;
inline constexpr std::uint32_t kMaxTxIo = 10000;
inline constexpr std::uint32_t kMaxBlockTxs = 100000;

} // namespace wire
} // namespace addition
