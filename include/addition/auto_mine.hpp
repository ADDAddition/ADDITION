#pragma once

#include "addition/config.hpp"
#include "addition/miner.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace addition {

struct AutoMineSettings {
    bool enabled{false};
    std::uint32_t interval_sec{60};
    std::string reward_address{"miner1"};
};

// In-process testnet timer. Off by default. Never a public RPC command.
class AutoMiner {
public:
    AutoMiner(Miner& miner, NetworkMode mode, AutoMineSettings settings);

    bool enabled() const;
    std::uint32_t interval_sec() const;
    const std::string& reward_address() const;

    // Mines at most one block when enabled, testnet, and `now` is at least
    // interval_sec after start (first block) or after the previous mine.
    bool maybe_mine(std::chrono::steady_clock::time_point now,
                    std::string& mined_hash,
                    std::string& error);

private:
    Miner& miner_;
    NetworkMode mode_;
    AutoMineSettings settings_;
    std::chrono::steady_clock::time_point started_{};
    std::chrono::steady_clock::time_point last_mine_{};
    bool armed_{false};
    bool mined_once_{false};
};

AutoMineSettings auto_mine_from_node(const NodeConfig& cfg);
bool apply_auto_mine_env(NodeConfig& cfg);

} // namespace addition
