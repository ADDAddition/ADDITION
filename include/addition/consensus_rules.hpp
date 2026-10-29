#pragma once
// Pure, deterministic consensus math (no wall clock, no floating point).
// Kept free of Chain state so it can be unit tested directly.
#include <cstdint>
#include <vector>

namespace addition {

// Default LWMA window (blocks). ~1 hour at 60s blocks.
inline constexpr std::uint32_t kDefaultLwmaWindow = 60U;
// Max solvetime credited per block, in multiples of target_block_time.
inline constexpr std::uint64_t kLwmaMaxSolvetimeMultiple = 6ULL;
// Consensus future-time limit for block timestamps, in multiples of the
// target block time (15 min at 60s). Bounds timestamp games against LWMA.
inline constexpr std::uint64_t kFutureTimeLimitBlocks = 15ULL;

// LWMA-1 (Zawy / Zcash-Monero-family) per-block retarget, expressed on a
// "higher target = easier" scale (head64(hash) <= target).
//
//   next = avg(target_i) * sum(j * solvetime_j) / (k * T),  k = n(n+1)/2
//
// `timestamps[i]` / `targets[i]` describe consecutive blocks, oldest first.
// The last element is the current tip. n = timestamps.size()-1 solvetimes are
// used (caller passes at most window+1 entries). Solvetimes are clamped to
// [0, 6T] (timestamps are already non-decreasing by consensus). The weighted
// sum is floored at k*T/10 so a burst of 0s cannot collapse the target.
// The per-block move is limited to [prev/2, prev*2] and the result is clamped
// to [min_target, max_target]. With fewer than 2 entries returns `fallback`
// clamped. All intermediate math is unsigned 128-bit: no overflow, no UB.
std::uint64_t lwma_next_target(const std::vector<std::uint64_t>& timestamps,
                               const std::vector<std::uint64_t>& targets,
                               std::uint64_t target_block_time_sec,
                               std::uint64_t min_target,
                               std::uint64_t max_target,
                               std::uint64_t fallback);

// Scheduled subsidy at `height` (halving phase, then tail emission).
std::uint64_t scheduled_block_subsidy(std::uint64_t height,
                                      std::uint64_t block_reward,
                                      std::uint64_t halving_interval,
                                      std::uint64_t tail_emission_reward);

// True once the halving schedule has decayed to zero (tail phase).
bool in_tail_emission(std::uint64_t height, std::uint64_t block_reward, std::uint64_t halving_interval);

// Max new coins a block at `height` may mint, given `total_emitted` so far.
// Halving phase: min(scheduled, max_supply - total_emitted) (cap enforced).
// Tail phase: tail_emission_reward unconditionally (perpetual, Monero-style:
// the tail is NOT bounded by max_supply; max_supply caps only the halving
// schedule).
std::uint64_t max_mintable_subsidy(std::uint64_t height,
                                   std::uint64_t total_emitted,
                                   std::uint64_t max_supply,
                                   std::uint64_t block_reward,
                                   std::uint64_t halving_interval,
                                   std::uint64_t tail_emission_reward);

} // namespace addition
