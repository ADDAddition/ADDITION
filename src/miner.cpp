#include "addition/miner.hpp"

#include <chrono>
#include <unordered_set>
#include <vector>

namespace addition {

Miner::Miner(Chain& chain, Mempool& mempool) : chain_(chain), mempool_(mempool) {}

bool Miner::mine_next_block(const std::string& reward_address,
                            std::size_t max_txs,
                            std::size_t threads,
                            std::string& mined_hash,
                            std::string& error) {
    const auto t0 = std::chrono::steady_clock::now();
    auto fetched = mempool_.fetch_for_block(max_txs);
    std::vector<Transaction> valid;
    valid.reserve(fetched.size());
    std::unordered_set<std::string> used_outpoints;
    for (const auto& tx : fetched) {
        std::string tx_error;
        if (!chain_.validate_transaction(tx, tx_error)) {
            continue;
        }
        bool conflict = false;
        std::vector<std::string> keys;
        keys.reserve(tx.inputs.size());
        for (const auto& in : tx.inputs) {
            const auto key = chain_.outpoint_key(in.previous_txid, in.output_index);
            if (used_outpoints.count(key) != 0) {
                conflict = true;
                break;
            }
            keys.push_back(key);
        }
        if (conflict) {
            continue;
        }
        used_outpoints.insert(keys.begin(), keys.end());
        valid.push_back(tx);
    }

    auto txs_for_restore = valid;
    const auto tx_count = valid.size();
    if (!chain_.mine_and_add_block(reward_address, std::move(valid), threads, mined_hash, error)) {
        for (const auto& tx : txs_for_restore) {
            mempool_.submit(tx);
        }
        return false;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    last_mine_ms_ = static_cast<std::uint64_t>(elapsed > 0 ? elapsed : 1);
    last_mined_txs_ = tx_count;
    const double sec = static_cast<double>(last_mine_ms_) / 1000.0;
    last_tps_ = sec > 0.0 ? static_cast<double>(last_mined_txs_) / sec : static_cast<double>(last_mined_txs_);
    return true;
}

double Miner::last_tps() const {
    return last_tps_;
}

std::uint64_t Miner::last_mine_ms() const {
    return last_mine_ms_;
}

std::size_t Miner::last_mined_txs() const {
    return last_mined_txs_;
}

} // namespace addition
