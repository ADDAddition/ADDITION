#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace addition {

struct TxInput {
    std::string previous_txid;
    std::uint32_t output_index{0};
};

struct TxOutput {
    std::string recipient;
    std::uint64_t amount{0};
};

struct Transaction {
    std::vector<TxInput> inputs;
    std::vector<TxOutput> outputs;
    std::string signer;
    std::string signer_pubkey;
    std::string signature;
    std::uint64_t fee{0};
    std::uint64_t nonce{0};
    // Optional shielded bundle (hex, see shielded.hpp on feat/shielded). Empty =>
    // legacy transparent tx. Same field/hash rule as feat/shielded for merge.
    std::string shielded;
};

struct BlockHeader {
    std::uint64_t height{0};
    std::string previous_hash;
    std::uint64_t timestamp{0};
    std::uint64_t nonce{0};
    std::uint64_t difficulty_target{0};
    std::string merkle_root;
};

struct Block {
    BlockHeader header;
    std::vector<Transaction> transactions;
};

std::string hash_transaction(const Transaction& tx);

// Consensus weight of a tx = its canonical binary (wire v3 TXV3) size in bytes,
// plus the 4-byte length prefix inside a block. Raw key/signature bytes, not hex.
std::size_t tx_weight(const Transaction& tx);
// Weight of a full block (header + all txs). Consensus-enforced <= max_block_weight.
std::size_t block_weight(const Block& block);
// Consensus max block weight (bytes of canonical binary). 2 MiB:
// base64 framing (x4/3) => ~2.8 MiB line, under the 4 MiB P2P line cap.
constexpr std::size_t kMaxBlockWeight = 2u * 1024u * 1024u;
// Exact size of a tx inside a v3 BLKDATA/BLK| line (base64 of the binary).
std::size_t tx_wire_bytes(const Transaction& tx);
// Header + framing of an otherwise empty block on the wire (conservative).
constexpr std::size_t kBlockWireOverheadBytes = 8192;
// Relay budget for a mined block (policy). Must stay below net_io kMaxLineBytes
// (4 MiB). With kMaxBlockWeight = 2 MiB the consensus cap binds first.
constexpr std::size_t kMaxRelayBlockWireBytes = 3u * 1024u * 1024u;
std::string compute_merkle_root(const std::vector<Transaction>& txs);
std::string hash_block_header(const BlockHeader& header);

} // namespace addition
