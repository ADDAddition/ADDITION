#include "addition/consensus_rules.hpp"

#include <algorithm>

namespace addition {

namespace {
using u128 = unsigned __int128;

std::uint64_t clamp_u64(u128 v, std::uint64_t lo, std::uint64_t hi) {
    if (lo > hi) {
        std::swap(lo, hi);
    }
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return static_cast<std::uint64_t>(v);
}
} // namespace

std::uint64_t lwma_next_target(const std::vector<std::uint64_t>& timestamps,
                               const std::vector<std::uint64_t>& targets,
                               std::uint64_t target_block_time_sec,
                               std::uint64_t min_target,
                               std::uint64_t max_target,
                               std::uint64_t fallback) {
    const std::size_t count = std::min(timestamps.size(), targets.size());
    if (count < 2 || target_block_time_sec == 0) {
        return clamp_u64(fallback, min_target, max_target);
    }
    const std::size_t off_ts = timestamps.size() - count;
    const std::size_t off_tg = targets.size() - count;
    const std::uint64_t T = target_block_time_sec;
    const u128 max_st = static_cast<u128>(T) * kLwmaMaxSolvetimeMultiple;

    u128 weighted = 0;
    u128 sum_target = 0;
    const std::uint64_t n = static_cast<std::uint64_t>(count - 1);
    for (std::uint64_t j = 1; j <= n; ++j) {
        const auto cur = timestamps[off_ts + j];
        const auto prev = timestamps[off_ts + j - 1];
        u128 st = (cur > prev) ? static_cast<u128>(cur - prev) : 0;
        if (st > max_st) {
            st = max_st;
        }
        weighted += st * j;
        sum_target += targets[off_tg + j];
    }
    const u128 k = static_cast<u128>(n) * (n + 1) / 2;
    const u128 floor_w = (k * T) / 10;
    if (weighted < floor_w) {
        weighted = floor_w;
    }
    const u128 avg_target = sum_target / n;
    // avg_target < 2^64, weighted <= 6T * n^2 -> product fits easily in 128 bits.
    u128 next = (avg_target * weighted) / (k * T);

    const std::uint64_t prev_target = targets.back();
    const u128 lo = prev_target / 2;
    const u128 hi = static_cast<u128>(prev_target) * 2;
    if (next < lo) {
        next = lo;
    }
    if (next > hi) {
        next = hi;
    }
    if (next == 0) {
        next = 1;
    }
    return clamp_u64(next, min_target, max_target);
}

std::uint64_t scheduled_block_subsidy(std::uint64_t height,
                                      std::uint64_t block_reward,
                                      std::uint64_t halving_interval,
                                      std::uint64_t subsidy_floor) {
    if (halving_interval == 0) {
        return block_reward;
    }
    const auto halvings = height / halving_interval;
    const std::uint64_t r = halvings >= 64 ? 0 : (block_reward >> halvings);
    return std::max<std::uint64_t>(r, std::min(subsidy_floor, block_reward));
}

std::uint64_t max_mintable_subsidy(std::uint64_t height,
                                   std::uint64_t total_emitted,
                                   std::uint64_t max_supply,
                                   std::uint64_t block_reward,
                                   std::uint64_t halving_interval,
                                   std::uint64_t subsidy_floor) {
    const auto scheduled = scheduled_block_subsidy(height, block_reward, halving_interval, subsidy_floor);
    const auto left = (max_supply > total_emitted) ? (max_supply - total_emitted) : 0ULL;
    // Hard cap: the last scheduled blocks are clipped so emission lands exactly on max_supply.
    return std::min<std::uint64_t>(scheduled, left);
}

std::uint64_t scheduled_total_emission(std::uint64_t max_supply,
                                       std::uint64_t block_reward,
                                       std::uint64_t halving_interval,
                                       std::uint64_t* cap_height,
                                       std::uint64_t subsidy_floor) {
    // Genesis (height 0) mints nothing; blocks 1.. follow the schedule.
    unsigned __int128 total = 0;
    if (cap_height) {
        *cap_height = 0;
    }
    if (halving_interval == 0) {
        return 0;
    }
    for (std::uint64_t e = 0; e < 64; ++e) {
        const std::uint64_t floor_r = std::min(subsidy_floor, block_reward);
        const std::uint64_t r = std::max<std::uint64_t>(block_reward >> e, floor_r);
        if (r == 0) {
            break;
        }
        const std::uint64_t first = (e == 0) ? 1 : e * halving_interval;
        // Once the floor is reached the rate stays constant until the cap.
        const bool floor_era = floor_r > 0 && (block_reward >> e) <= floor_r;
        const std::uint64_t last = floor_era ? UINT64_MAX / 2 : (e + 1) * halving_interval - 1;
        const std::uint64_t n = last - first + 1;
        const unsigned __int128 add = static_cast<unsigned __int128>(r) * n;
        if (total + add >= max_supply) {
            const auto need = static_cast<std::uint64_t>(max_supply - total);
            if (cap_height) {
                *cap_height = first + (need + r - 1) / r - 1;
            }
            return max_supply;
        }
        total += add;
    }
    return static_cast<std::uint64_t>(total);
}

} // namespace addition
