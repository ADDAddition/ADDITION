#include "addition/miner.hpp"

#include "addition/crypto.hpp"

#include <chrono>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace addition {
namespace {

bool pow_meets_target(PowAlgorithm algorithm, const std::string& header_hash, std::uint64_t target) {
    switch (algorithm) {
    case PowAlgorithm::Sha3_512:
        return hash_head64(header_hash) <= target;
    case PowAlgorithm::MemoryHard:
        return memory_hard_head64(header_hash) <= target;
    }
    const PowAlgorithm missing = algorithm;
    switch (missing) {
    case PowAlgorithm::Sha3_512:
    case PowAlgorithm::MemoryHard:
        break;
    }
    return hash_head64(header_hash) <= target;
}

std::size_t resolve_mine_threads(std::size_t requested) {
    const auto hw = std::thread::hardware_concurrency();
    const std::size_t max_n = hw > 0 ? static_cast<std::size_t>(hw) : 1;
    if (requested == 0) {
        return max_n;
    }
    return requested > max_n ? max_n : requested;
}

} // namespace

PowSearchResult search_block_pow(const BlockHeader& header, const PowSearchConfig& cfg) {
    PowSearchResult out{};
    const std::size_t threads = resolve_mine_threads(cfg.threads);
    out.threads_used = threads;

    std::atomic<bool> found{false};
    std::atomic<bool> deadline_hit{false};
    std::atomic<bool> stopped{false};
    std::atomic<std::uint64_t> hashes{0};
    std::atomic<std::uint64_t> winning_nonce{0};
    std::string winning_hash;
    std::mutex win_mu;
    std::vector<std::thread> workers;
    workers.reserve(threads);

    const bool use_deadline = cfg.deadline_sec > 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(cfg.deadline_sec);

    for (std::size_t tid = 0; tid < threads; ++tid) {
        workers.emplace_back([&, tid]() {
            BlockHeader local = header;
            const std::uint64_t step = static_cast<std::uint64_t>(threads);
            std::uint64_t local_hashes = 0;
            for (std::uint64_t nonce = static_cast<std::uint64_t>(tid);
                 nonce < std::numeric_limits<std::uint64_t>::max() && !found.load(std::memory_order_relaxed);
                 nonce += step) {
                if (cfg.stop != nullptr && cfg.stop->load(std::memory_order_relaxed)) {
                    stopped.store(true, std::memory_order_relaxed);
                    hashes.fetch_add(local_hashes, std::memory_order_relaxed);
                    return;
                }
                if (use_deadline && std::chrono::steady_clock::now() >= deadline) {
                    deadline_hit.store(true, std::memory_order_relaxed);
                    hashes.fetch_add(local_hashes, std::memory_order_relaxed);
                    return;
                }
                local.nonce = nonce;
                const auto h = hash_block_header(local);
                ++local_hashes;
                if (pow_meets_target(cfg.algorithm, h, cfg.target)) {
                    bool expected = false;
                    if (found.compare_exchange_strong(expected, true)) {
                        winning_nonce.store(nonce, std::memory_order_relaxed);
                        std::lock_guard<std::mutex> lk(win_mu);
                        winning_hash = h;
                    }
                    hashes.fetch_add(local_hashes, std::memory_order_relaxed);
                    return;
                }
            }
            hashes.fetch_add(local_hashes, std::memory_order_relaxed);
        });
    }

    for (auto& th : workers) {
        if (th.joinable()) {
            th.join();
        }
    }

    out.hashes = hashes.load(std::memory_order_relaxed);
    out.deadline_hit = deadline_hit.load(std::memory_order_relaxed);
    out.stopped = stopped.load(std::memory_order_relaxed);
    if (found.load(std::memory_order_relaxed)) {
        out.found = true;
        out.nonce = winning_nonce.load(std::memory_order_relaxed);
        out.header_hash = winning_hash;
    }
    return out;
}

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
    last_dropped_junk_ = 0;
    for (const auto& tx : fetched) {
        std::string verr;
        if (chain_.validate_transaction(tx, verr)) {
            valid.push_back(tx);
        } else {
            ++last_dropped_junk_;
        }
    }

    const auto tx_count = valid.size();
    if (!chain_.mine_and_add_block(reward_address, valid, threads, mined_hash, error, &stop_)) {
        for (const auto& tx : valid) {
            mempool_.submit(tx);
        }
        return false;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    last_mine_ms_ = static_cast<std::uint64_t>(elapsed > 0 ? elapsed : 1);
    last_mined_txs_ = tx_count;
    last_threads_ = resolve_mine_threads(threads);
    const double sec = static_cast<double>(last_mine_ms_) / 1000.0;
    last_tps_ = sec > 0.0 ? static_cast<double>(last_mined_txs_) / sec : static_cast<double>(last_mined_txs_);
    return true;
}

void Miner::request_stop() {
    stop_.store(true, std::memory_order_relaxed);
}

void Miner::clear_stop() {
    stop_.store(false, std::memory_order_relaxed);
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

std::size_t Miner::last_dropped_junk() const {
    return last_dropped_junk_;
}

std::size_t Miner::last_threads() const {
    return last_threads_;
}

} // namespace addition
