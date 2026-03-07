#include "addition/miner.hpp"

#include <chrono>

namespace addition {

Miner::Miner(Chain& chain, Mempool& mempool) : chain_(chain), mempool_(mempool) {}

bool Miner::mine_next_block(const std::string& reward_address,
                            std::size_t max_txs,
                            std::size_t threads,
                            std::string& mined_hash,
                            std::string& error) {
    const auto t0 = std::chrono::steady_clock::now();
    auto txs = mempool_.fetch_for_block(max_txs);
    auto txs_for_restore = txs;
    const auto tx_count = txs.size();
    if (!chain_.mine_and_add_block(reward_address, std::move(txs), threads, mined_hash, error)) {
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
