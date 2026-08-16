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
    std::string signer_scheme{"ml-dsa-87"};
    std::string signature;
    std::string note;
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
std::string compute_merkle_root(const std::vector<Transaction>& txs);
std::string hash_block_header(const BlockHeader& header);

} // namespace addition
