#pragma once

#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/mempool.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace addition {

struct PowSearchConfig {
    PowAlgorithm algorithm{PowAlgorithm::Sha3_512};
    std::uint64_t target{0};
    std::size_t threads{0};
    std::uint64_t deadline_sec{kTestnetMineDeadlineSec};
    std::atomic<bool>* stop{nullptr};
};

struct PowSearchResult {
    bool found{false};
    bool deadline_hit{false};
    bool stopped{false};
    std::uint64_t nonce{0};
    std::string header_hash;
    std::uint64_t hashes{0};
    std::size_t threads_used{0};
};

// Multi-thread nonce search. Uses the header's existing difficulty_target.
// deadline_sec=0 means search until a block is found (or stop is set).
PowSearchResult search_block_pow(const BlockHeader& header, const PowSearchConfig& cfg);

class Miner {
public:
    Miner(Chain& chain, Mempool& mempool);
    bool mine_next_block(const std::string& reward_address,
                         std::size_t max_txs,
                         std::size_t threads,
                         std::string& mined_hash,
                         std::string& error);
    // Optional source of stake votes on the current tip (e.g. the node's
    // relayed vote pool). When set, blocks are mined through the vote path.
    void set_vote_source(std::function<std::vector<StakeVote>()> source) { vote_source_ = std::move(source); }
    void request_stop();
    // Max BLKDATA wire bytes for a mined block (default: kMaxRelayBlockWireBytes).
    void set_relay_budget_bytes(std::size_t bytes);
    std::size_t last_deferred_for_size() const;
    std::size_t last_block_wire_bytes() const;
    void clear_stop();
    double last_tps() const;
    std::uint64_t last_mine_ms() const;
    std::size_t last_mined_txs() const;
    std::size_t last_dropped_junk() const;
    std::size_t last_threads() const;

private:
    Chain& chain_;
    Mempool& mempool_;
    std::atomic<bool> stop_{false};
    std::function<std::vector<StakeVote>()> vote_source_;
    double last_tps_{0.0};
    std::uint64_t last_mine_ms_{0};
    std::size_t last_mined_txs_{0};
    std::size_t last_dropped_junk_{0};
    std::size_t relay_budget_bytes_{kMaxRelayBlockWireBytes};
    std::size_t last_deferred_for_size_{0};
    std::size_t last_block_wire_bytes_{0};
    std::size_t last_threads_{0};
};

} // namespace addition
