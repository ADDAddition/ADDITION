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

// Exact size of a tx inside a BLKDATA/BLK| line (TXB2 binary, length-prefixed,
// then hex-encoded by the block payload => x2).
std::size_t tx_wire_bytes(const Transaction& tx);
// Header + framing of an otherwise empty block on the wire (conservative).
constexpr std::size_t kBlockWireOverheadBytes = 8192;
// Relay budget for a mined block. Every BLKDATA line must fit the P2P line
// limit (net_io kMaxLineBytes = 256 KiB) or peers can never fetch it and the
// miner orphans itself. Policy only (not a consensus rule).
constexpr std::size_t kMaxRelayBlockWireBytes = 262144;
std::string compute_merkle_root(const std::vector<Transaction>& txs);
std::string hash_block_header(const BlockHeader& header);

} // namespace addition
