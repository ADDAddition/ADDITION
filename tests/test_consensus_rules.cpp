// Unit tests: LWMA per-block retarget + block subsidy / tail emission.
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/consensus_rules.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

constexpr std::uint64_t T = 60;
constexpr std::uint64_t kMin = 0x0000000000FFFFFFULL;
constexpr std::uint64_t kMax = 0x000000FFFFFFFFFFULL;
constexpr std::uint64_t kMid = 0x0000000FFFFFFFFFULL;

// Build a run of n+1 blocks with a constant solvetime and constant target.
std::uint64_t run(std::uint64_t solvetime, std::uint64_t target, std::size_t n = 60) {
    std::vector<std::uint64_t> ts, tg;
    for (std::size_t i = 0; i <= n; ++i) {
        ts.push_back(1'000'000 + i * solvetime);
        tg.push_back(target);
    }
    return addition::lwma_next_target(ts, tg, T, kMin, kMax, target);
}
} // namespace

int main() {
    using namespace addition;

    // --- LWMA ---
    check(run(T, kMid) == kMid, "on-target solvetimes keep target unchanged");
    {
        const auto t = run(T / 2, kMid);
        check(t < kMid && t == kMid / 2, "2x fast blocks halve target (harder)");
    }
    {
        const auto t = run(2 * T, kMid);
        check(t > kMid && t == kMid * 2, "2x slow blocks double target (easier)");
    }
    check(run(0, kMid) == kMid / 2, "zero solvetimes: limited to prev/2 per block");
    check(run(100 * T, kMid) == kMid * 2, "huge solvetimes: limited to prev*2 per block");
    check(run(T, kMin) == kMin, "never harder than min bound");
    check(run(0, kMin) == kMin, "clamped at min (hardest) bound");
    check(run(10 * T, kMax) == kMax, "clamped at max (easiest) bound: mainnet never looser");
    {
        std::vector<std::uint64_t> ts{100}, tg{kMid};
        check(lwma_next_target(ts, tg, T, kMin, kMax, kMid) == kMid, "<2 blocks returns fallback");
    }
    {
        // Determinism + linear weighting: a recent slow block moves more than an old one.
        std::vector<std::uint64_t> ts, tg;
        for (int i = 0; i <= 60; ++i) {
            ts.push_back(1000 + i * T);
            tg.push_back(kMid);
        }
        auto old_slow = ts, new_slow = ts;
        for (std::size_t i = 1; i < old_slow.size(); ++i) old_slow[i] += 300;   // slow at the start
        new_slow.back() += 300;                                                  // slow at the tip
        const auto a = lwma_next_target(old_slow, tg, T, kMin, kMax, kMid);
        const auto b = lwma_next_target(new_slow, tg, T, kMin, kMax, kMid);
        check(b > a && a >= kMid, "recent solvetimes weigh more than old ones");
        check(lwma_next_target(new_slow, tg, T, kMin, kMax, kMid) == b, "deterministic");
    }
    {
        // Overflow safety at the 64-bit edge.
        const auto t = run(6 * T, 0xFFFFFFFFFFFFFFFFULL);
        std::vector<std::uint64_t> ts{0, 360}, tg{~0ULL, ~0ULL};
        check(lwma_next_target(ts, tg, T, 1, ~0ULL, 1) == ~0ULL, "no overflow at max target");
        (void)t;
    }

    // --- subsidy / tail emission (mainnet params: 50, halving 210000, tail 1) ---
    check(scheduled_block_subsidy(1, 50, 210000, 1) == 50, "era0 reward 50");
    check(scheduled_block_subsidy(210000, 50, 210000, 1) == 25, "era1 reward 25");
    check(scheduled_block_subsidy(210000 * 5, 50, 210000, 1) == 1, "era5 reward 1");
    check(scheduled_block_subsidy(210000 * 6, 50, 210000, 1) == 1, "era6 -> tail 1");
    check(scheduled_block_subsidy(210000ULL * 1000, 50, 210000, 1) == 1, "far future tail 1");
    check(in_tail_emission(210000 * 6, 50, 210000) && !in_tail_emission(210000 * 5, 50, 210000), "tail boundary");
    check(max_mintable_subsidy(1, 0, 50'000'000, 50, 210000, 1) == 50, "cap room -> full reward");
    check(max_mintable_subsidy(1, 49'999'990, 50'000'000, 50, 210000, 1) == 10, "halving phase capped by remaining");
    check(max_mintable_subsidy(1, 50'000'000, 50'000'000, 50, 210000, 1) == 1, "cap hit: miners still get tail");
    check(max_mintable_subsidy(210000 * 7, 60'000'000, 50'000'000, 50, 210000, 1) == 1, "tail exceeds cap forever");
    {
        // Total emitted by the halving schedule alone (before tail) for mainnet params.
        std::uint64_t total = 0;
        for (std::uint64_t era = 0; era < 6; ++era) total += (50ULL >> era) * 210000ULL;
        check(total == 20'370'000ULL, "halving schedule emits 20.37M (< 50M cap)");
    }

    // --- Chain integration: retarget is applied and consensus-validated ---
    {
        auto cfg = regtest_chain_config();
        cfg.pow_algorithm = PowAlgorithm::Sha3_512;
        cfg.initial_difficulty_target = 0x00FFFFFFFFFFFFFFULL;   // ~2^8 hashes
        cfg.max_difficulty_target = 0x00FFFFFFFFFFFFFFULL;
        cfg.min_difficulty_target = 0x0000FFFFFFFFFFFFULL;       // ~2^16 hashes
        cfg.legacy_min_difficulty_target = 0;
        cfg.difficulty_fork_height = 0;
        cfg.lwma_window = 60;
        cfg.min_fee = 0;
        Chain a(cfg);
        std::uint64_t prev = a.current_difficulty_target();
        bool monotone = true;
        for (int i = 0; i < 12; ++i) {
            std::string h, e;
            if (!a.mine_and_add_block("miner1", {}, 2, h, e)) {
                check(false, "mine failed: " + e);
                break;
            }
            const auto cur = a.current_difficulty_target();
            if (cur > prev) monotone = false;
            prev = cur;
        }
        check(a.height() == 12, "mined 12 blocks");
        check(monotone && a.current_difficulty_target() < cfg.initial_difficulty_target,
              "fast blocks make the chain harder (difficulty not frozen)");
        check(a.blocks()[12].header.difficulty_target != a.blocks()[2].header.difficulty_target,
              "per-block targets actually change");

        // Replay on a fresh node: deterministic acceptance.
        Chain b(cfg);
        bool replay_ok = true;
        for (std::size_t i = 1; i < a.blocks().size(); ++i) {
            std::string e;
            if (!b.add_block(a.blocks()[i], e)) {
                replay_ok = false;
                std::cerr << "replay error at " << i << ": " << e << '\n';
                break;
            }
        }
        check(replay_ok && b.current_difficulty_target() == a.current_difficulty_target(), "replay deterministic");

        // A block claiming the wrong (easier) target is rejected.
        Chain c(cfg);
        for (std::size_t i = 1; i < 6; ++i) {
            std::string e;
            c.add_block(a.blocks()[i], e);
        }
        auto bad = a.blocks()[6];
        bad.header.difficulty_target = cfg.max_difficulty_target;
        std::string e;
        check(!c.add_block(bad, e), "wrong difficulty_target rejected");

        // Far-future timestamp rejected post-fork.
        auto fut = a.blocks()[6];
        fut.header.timestamp += 100000;
        std::string e2;
        check(!c.add_block(fut, e2), "far-future timestamp rejected");
    }

    // Mainnet profile: retarget actually enabled.
    {
        const auto m = mainnet_chain_config();
        check(m.min_difficulty_target < m.max_difficulty_target, "mainnet bounds allow retarget");
        check(m.max_difficulty_target == kMainnetMaxDifficultyTarget, "mainnet easiest bound unchanged");
        check(m.difficulty_fork_height == kMainnetDifficultyForkHeight, "mainnet fork height set");
        check(m.min_fee > 0, "mainnet min_fee nonzero");
    }

    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "test_consensus_rules: ok\n";
    return 0;
}
