#pragma once

#include "addition/block.hpp"
#include "addition/config.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace addition {

class Chain {
public:
    explicit Chain(ChainConfig cfg = default_config());

    void reset();

    const Block& genesis_block() const;
    const Block& tip() const;
    std::uint64_t height() const;

    bool validate_transaction(const Transaction& tx, std::string& error) const;
    bool add_block(const Block& block, std::string& error);
    bool mine_and_add_block(const std::string& reward_address,
                            std::vector<Transaction> txs,
                            std::size_t threads,
                            std::string& mined_hash,
                            std::string& error);
    bool replace_with_chain(const std::vector<Block>& candidate,
                            std::string& error);

    bool build_transaction(const std::string& from,
                           const std::string& to,
                           std::uint64_t amount,
                           std::uint64_t fee,
                           std::uint64_t nonce,
                           Transaction& out_tx,
                           std::string& error) const;

    std::uint64_t balance_of(const std::string& address) const;
    bool credit_balance(const std::string& address,
                        std::uint64_t amount,
                        const std::string& reason,
                        std::string& error);
    bool apply_transaction(const Transaction& tx,
                           const std::string& txid,
                           std::string& error);

    std::string outpoint_key(const std::string& txid, std::uint32_t output_index) const;
    std::uint64_t current_difficulty_target() const;
    std::uint64_t current_block_reward() const;
    std::uint64_t total_fees_last_block() const;
    std::uint64_t max_supply() const;
    std::uint64_t total_emitted() const;
    std::uint64_t remaining_supply() const;
    std::uint64_t next_halving_height() const;

    const std::vector<Block>& blocks() const;
    std::optional<Block> block_at(std::uint64_t height) const;
    std::optional<Block> block_by_hash(const std::string& hash) const;
    bool has_block_hash(const std::string& hash) const;
    std::uint64_t cumulative_work() const;

private:
    struct UTXO {
        std::string owner;
        std::uint64_t amount{0};
        bool spent{false};
    };

    ChainConfig cfg_;
    std::vector<Block> blocks_;
    std::unordered_map<std::string, UTXO> utxo_set_;
    std::unordered_map<std::string, std::uint64_t> address_index_;
    std::unordered_map<std::string, std::uint64_t> signer_last_nonce_;
    std::unordered_set<std::string> seen_transactions_;
    std::uint64_t difficulty_target_{0};
    std::uint64_t total_emitted_{0};
    std::uint64_t total_fees_last_block_{0};
    std::uint64_t cumulative_work_{0};

    Block make_genesis() const;
    Block make_block_template(const std::string& reward_address,
                              std::vector<Transaction> txs,
                              std::uint64_t reward) const;
    bool validate_block_header(const Block& candidate, std::string& error) const;
    bool validate_block_transactions(const Block& candidate, std::string& error) const;
    bool validate_transaction_signature(const Transaction& tx, std::string& error) const;
    std::uint64_t compute_next_difficulty_target() const;
    std::uint64_t compute_block_reward(std::uint64_t height) const;
    bool hash_meets_target(const std::string& hex_hash, std::uint64_t target) const;
};

} // namespace addition
