#include "addition/auto_mine.hpp"

#include <cstdlib>
#include <exception>
#include <thread>
#include <utility>

namespace addition {
namespace {

std::string trim_copy(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    if (i > 0) {
        s.erase(0, i);
    }
    return s;
}

} // namespace

AutoMiner::AutoMiner(Miner& miner, NetworkMode mode, AutoMineSettings settings)
    : miner_(miner),
      mode_(mode),
      settings_(std::move(settings)) {
    if (settings_.interval_sec == 0) {
        settings_.interval_sec = 1;
    }
    if (settings_.reward_address.empty()) {
        settings_.reward_address = "miner1";
    }
}

bool AutoMiner::enabled() const {
    return settings_.enabled && mode_ == NetworkMode::Testnet;
}

std::uint32_t AutoMiner::interval_sec() const {
    return settings_.interval_sec;
}

const std::string& AutoMiner::reward_address() const {
    return settings_.reward_address;
}

bool AutoMiner::maybe_mine(std::chrono::steady_clock::time_point now,
                           std::string& mined_hash,
                           std::string& error) {
    mined_hash.clear();
    error.clear();

    if (!settings_.enabled) {
        error = "auto-mine disabled";
        return false;
    }
    switch (mode_) {
    case NetworkMode::Testnet:
        break;
    case NetworkMode::Mainnet:
        error = "auto-mine is testnet only";
        return false;
    }

    if (!armed_) {
        started_ = now;
        armed_ = true;
        error = "auto-mine not due";
        return false;
    }

    const auto interval = std::chrono::seconds(settings_.interval_sec);
    const auto due_from = mined_once_ ? last_mine_ : started_;
    if (now - due_from < interval) {
        error = "auto-mine not due";
        return false;
    }

    const auto hw = std::thread::hardware_concurrency();
    const std::size_t threads = hw > 0 ? static_cast<std::size_t>(hw) : 1;
    if (!miner_.mine_next_block(settings_.reward_address, 500, threads, mined_hash, error)) {
        return false;
    }
    last_mine_ = now;
    mined_once_ = true;
    return true;
}

AutoMineSettings auto_mine_from_node(const NodeConfig& cfg) {
    AutoMineSettings s;
    s.enabled = cfg.enable_auto_mine;
    s.interval_sec = cfg.auto_mine_interval_sec;
    s.reward_address = cfg.auto_mine_reward;
    return s;
}

bool apply_auto_mine_env(NodeConfig& cfg) {
    if (const char* v = std::getenv("ADDITION_AUTO_MINE")) {
        if (std::string(v) == "1") {
            cfg.enable_auto_mine = true;
        }
    }
    if (const char* v = std::getenv("ADDITION_AUTO_MINE_INTERVAL")) {
        try {
            const auto n = std::stoul(v);
            if (n == 0 || n > 86400) {
                return false;
            }
            cfg.auto_mine_interval_sec = static_cast<std::uint32_t>(n);
        } catch (const std::exception&) {
            return false;
        }
    }
    if (const char* v = std::getenv("ADDITION_AUTO_MINE_REWARD")) {
        const auto reward = trim_copy(v);
        if (!reward.empty()) {
            cfg.auto_mine_reward = reward;
        }
    }
    return true;
}

} // namespace addition
