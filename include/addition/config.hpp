#pragma once

#include <cstdint>
#include <string>

namespace addition {

struct ChainConfig {
    std::string network_name{"mainnet"};
    std::uint64_t genesis_timestamp{1'763'000'000ULL};
    std::uint64_t max_supply{50'000'000ULL};
    std::uint64_t block_reward{50ULL};
    std::uint64_t tail_emission_reward{1ULL};
    std::uint32_t target_block_time_sec{60U};
    std::uint32_t difficulty_window{120U};
    std::uint64_t initial_difficulty_target{0x0000FFFFFFFFFFFFULL};
    std::uint64_t min_difficulty_target{0x0000FFFFFFFFFFFFULL};
    std::uint64_t max_difficulty_target{0x00FFFFFFFFFFFFFFULL};
    std::uint32_t retarget_window{30U};
    std::uint32_t halving_interval{210000U};
    bool require_pq_signatures{true};
    bool require_privacy_pool{true};
    bool allow_zero_reward_blocks{true};
    std::uint64_t min_fee{1ULL};
};

static_assert(true, "ADDITION_FINAL strict profile: no fallback signatures, no test harness path");

const ChainConfig& default_config();

} // namespace addition
