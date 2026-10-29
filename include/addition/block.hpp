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

// A staker's ML-DSA-87 (hybrid) signed vote on block `voted_height` /
// `voted_hash`. Included in the *child* block (Decred-style): block h carries
// the votes of the tickets selected by block h-1's hash, approving block h-1.
struct StakeVote {
    std::string ticket_id;      // outpoint "txid:index" of the @stake output
    std::uint64_t voted_height{0};
    std::string voted_hash;
    bool approve{true};
    std::string signature;      // sign_message_hybrid over stake_vote_message(...)
};

// Equivocation proof: two valid votes by the same ticket at the same height
// for different hashes (or different approve bits). The ticket is burned.
struct SlashEvidence {
    StakeVote a;
    StakeVote b;
};

struct Block {
    BlockHeader header;
    std::vector<Transaction> transactions;
    std::vector<StakeVote> votes;
    std::vector<SlashEvidence> slashes;
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
std::string hash_stake_vote(const StakeVote& v);
// header.merkle_root commitment: the tx merkle root when the block carries no
// votes/slashes (unchanged for legacy blocks), else H(tx_root | votes | slashes).
std::string compute_block_commitment(const Block& block);

// Byte weight of the staking extension (votes + slash evidence) as binary
// (hex fields count half). Must be added to consensus block weight so a block
// full of ~24 KB of votes cannot exceed max_block_weight (see ADDITION-net BLV3).
std::size_t stake_vote_weight(const StakeVote& v);
std::size_t stake_extension_weight(const Block& block);

} // namespace addition
