#pragma once

#include "addition/shielded.hpp"

#include "addition/block.hpp"
#include "addition/config.hpp"
#include "addition/staking.hpp"

#include <map>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace addition {

class Chain {
public:
    explicit Chain(ChainConfig cfg = default_config());

    const ChainConfig& config() const { return cfg_; }

    void reset();

    const Block& genesis_block() const;
    const Block& tip() const;
    std::uint64_t height() const;

    bool validate_transaction(const Transaction& tx, std::string& error) const;
    bool validate_transaction(const Transaction& tx, std::string& error, bool check_signature) const;
    std::uint64_t last_batch_verify_ms() const;
    std::size_t last_batch_verify_count() const;
    double last_batch_verify_per_sec() const;
    bool add_block(const Block& block, std::string& error);
    bool mine_and_add_block(const std::string& reward_address,
                            std::vector<Transaction> txs,
                            std::size_t threads,
                            std::string& mined_hash,
                            std::string& error,
                            std::atomic<bool>* stop = nullptr);
    // Mines a block that carries staker votes on the current tip. The coinbase
    // pays each approving vote its stake share; the miner gets the rest.
    bool mine_and_add_block_with_votes(const std::string& reward_address,
                                       std::vector<Transaction> txs,
                                       std::vector<StakeVote> votes,
                                       std::vector<SlashEvidence> slashes,
                                       std::size_t threads,
                                       std::string& mined_hash,
                                       std::string& error,
                                       std::atomic<bool>* stop = nullptr);
    bool replace_with_chain(const std::vector<Block>& candidate,
                            std::string& error);

    // Invoked after a block is committed (add_block) or a heavier chain is
    // accepted (replace_with_chain). Not called during a failed reorg replay.
    void set_on_commit(std::function<void()> hook);

    bool build_transaction(const std::string& from,
                           const std::string& to,
                           std::uint64_t amount,
                           std::uint64_t fee,
                           std::uint64_t nonce,
                           Transaction& out_tx,
                           std::string& error) const;

    std::uint64_t balance_of(const std::string& address) const;
    std::uint64_t last_nonce(const std::string& address) const;
    std::uint64_t next_nonce(const std::string& address) const;
    bool credit_balance(const std::string& address,
                        std::uint64_t amount,
                        const std::string& reason,
                        std::string& error);
    bool apply_transaction(const Transaction& tx,
                           const std::string& txid,
                           std::string& error);
    bool apply_transaction(const Transaction& tx,
                           const std::string& txid,
                           std::string& error,
                           bool check_signature);

    // ---- consensus staking (see staking.hpp) ----
    bool staking_active_at(std::uint64_t height) const;
    std::vector<StakeTicket> tickets() const;
    std::vector<StakeTicket> tickets_of(const std::string& owner) const;
    std::optional<StakeTicket> ticket(const std::string& ticket_id) const;
    std::uint64_t staked_of(const std::string& owner) const;
    std::uint64_t total_staked() const;
    std::uint64_t total_slashed() const { return total_slashed_; }
    // Mature tickets eligible to vote on the current tip (for block height()+1).
    std::vector<StakeTicket> eligible_tickets_for_next_block() const;
    std::vector<std::string> selected_tickets_for_next_block() const;
    bool stake_votes_required_for_next_block() const;
    std::string stake_vote_message_for(std::uint64_t voted_height,
                                       const std::string& voted_hash,
                                       const std::string& ticket_id,
                                       bool approve) const;
    bool verify_stake_vote(const StakeVote& vote, std::string& error) const;
    // Filter a vote pool down to valid, selected, unique votes on the current tip.
    std::vector<StakeVote> usable_votes_for_next_block(const std::vector<StakeVote>& pool) const;
    // Unsigned stake lock (output to @stake + change) / unlock (spend ticket).
    bool build_stake_lock(const std::string& from,
                          std::uint64_t amount,
                          std::uint64_t fee,
                          std::uint64_t nonce,
                          Transaction& out_tx,
                          std::string& error) const;
    bool build_stake_unlock(const std::string& ticket_id,
                            std::uint64_t fee,
                            std::uint64_t nonce,
                            Transaction& out_tx,
                            std::string& error) const;
    std::uint64_t spendable_height_of(const std::string& outpoint) const;

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
    std::string genesis_hash() const;
    std::uint64_t tx_confirmations(const std::string& tx_hash) const;
    bool tx_in_best_chain(const std::string& tx_hash) const;

    // On-chain public-key registry: address -> pubkey hex, recorded the first
    // time a confirmed tx carries the full key. Later txs from that address may
    // leave signer_pubkey empty (saves 2592 B ML-DSA-87 / 1793 B Falcon-1024).
    // Deterministic: rebuilt by replay; snapshotted/restored like the UTXO set.
    bool registered_pubkey(const std::string& address, std::string& pubkey_hex) const;
    std::size_t registry_size() const { return pubkey_registry_.size(); }
    // Pubkey to put in a new tx: "" when this exact key is already registered.
    std::string pubkey_field_for(const std::string& address, const std::string& pubkey_hex) const {
        const auto it = pubkey_registry_.find(address);
        return (it != pubkey_registry_.end() && it->second == pubkey_hex) ? std::string() : pubkey_hex;
    }
public:
    const shielded::State& shielded_state() const { return shielded_; }

private:
    struct UTXO {
        std::string owner;
        std::uint64_t amount{0};
        bool spent{false};
        std::uint64_t spendable_height{0}; // unbonding freeze (0 = none)
        std::uint64_t height{0};  // block height that created it (for older())
    };

    ChainConfig cfg_;
    std::vector<Block> blocks_;
    std::unordered_map<std::string, UTXO> utxo_set_;
    std::unordered_map<std::string, std::uint64_t> address_index_;
    std::unordered_map<std::string, std::uint64_t> signer_last_nonce_;
    std::unordered_set<std::string> seen_transactions_;
    std::unordered_map<std::string, std::string> pubkey_registry_;
    std::uint64_t difficulty_target_{0};
    std::uint64_t total_emitted_{0};
    std::uint64_t total_fees_last_block_{0};
    std::uint64_t cumulative_work_{0};
    std::map<std::string, StakeTicket> tickets_;
    std::uint64_t total_slashed_{0};
    std::uint64_t applying_height_{0}; // block height being applied (0 = mempool context)
    shielded::State shielded_;
    std::function<void()> on_commit_;
    int suppress_commit_{0};
    mutable std::uint64_t last_batch_verify_ms_{0};
    mutable std::size_t last_batch_verify_count_{0};

    void notify_commit();
    Block make_genesis() const;
    Block make_block_template(const std::string& reward_address,
                              std::vector<Transaction> txs,
                              std::uint64_t reward) const;
    bool mine_block(Block b, std::size_t threads, std::string& mined_hash, std::string& error,
                    std::atomic<bool>* stop);
    bool validate_shielded_part(const Transaction& tx,
                                shielded::Bundle& bundle,
                                const std::unordered_set<std::string>& pending_nfs,
                                bool check_proof,
                                std::string& error) const;
    bool validate_block_header(const Block& candidate, std::string& error) const;
    bool validate_block_transactions(const Block& candidate, std::string& error) const;
    bool validate_block_staking(const Block& candidate, std::string& error) const;
    bool validate_slash(const SlashEvidence& ev, std::string& error) const;
    std::vector<StakeTicket> eligible_tickets_at(std::uint64_t voted_height) const;
    std::uint64_t context_height() const;
    bool validate_script_inputs(const Transaction& tx, std::string& error) const;
    bool validate_transaction_signature(const Transaction& tx, std::string& error) const;
    bool validate_transaction_signature(const Transaction& tx, std::string& error, bool check_crypto) const;
    // signer_pubkey, or the registry entry (optionally overlaid by same-block registrations).
    bool resolve_signer_pubkey(const Transaction& tx,
                               const std::unordered_map<std::string, std::string>* overlay,
                               std::string& pubkey_hex,
                               std::string& error) const;
    std::uint64_t compute_next_difficulty_target() const;
    std::uint64_t compute_legacy_difficulty_target() const;
    std::uint64_t max_mintable_at(std::uint64_t height) const;
    std::uint64_t clamp_difficulty_target(std::uint64_t target) const;
    std::uint64_t compute_block_reward(std::uint64_t height) const;
    bool hash_meets_target(const std::string& hex_hash, std::uint64_t target) const;
};

} // namespace addition
