#include "addition/staking.hpp"

#include "addition/crypto.hpp"

#include <algorithm>

namespace addition {

namespace {
constexpr const char* kDeprecated = "off-chain staking removed; use on-chain stake lock (tx output to @stake)";

std::uint64_t seed_u64(const std::string& seed, std::uint32_t round) {
    const auto digest = sha3_512_bytes("ADDITION_TICKET_DRAW_V1|" + seed + "|" + std::to_string(round));
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<std::uint8_t>(digest[static_cast<std::size_t>(i)]);
    }
    return v;
}
} // namespace

std::string stake_vote_message(const std::string& network_id,
                               std::uint64_t voted_height,
                               const std::string& voted_hash,
                               const std::string& ticket_id,
                               bool approve) {
    return "ADDITION_STAKE_VOTE_V1|" + network_id + "|" + std::to_string(voted_height) + "|" + voted_hash + "|" +
           ticket_id + "|" + (approve ? "approve" : "reject");
}

std::vector<std::string> select_tickets(std::vector<StakeTicket> eligible,
                                        const std::string& seed,
                                        std::uint32_t count) {
    std::sort(eligible.begin(), eligible.end(),
              [](const StakeTicket& a, const StakeTicket& b) { return a.id < b.id; });
    std::vector<std::string> out;
    unsigned __int128 total = 0;
    for (const auto& t : eligible) {
        total += t.amount;
    }
    for (std::uint32_t round = 0; round < count && !eligible.empty() && total > 0; ++round) {
        // 128-bit draw: two 64-bit words mod total (negligible bias for real totals).
        const unsigned __int128 r =
            ((static_cast<unsigned __int128>(seed_u64(seed, round * 2U)) << 64) | seed_u64(seed, round * 2U + 1U)) %
            total;
        unsigned __int128 acc = 0;
        for (std::size_t i = 0; i < eligible.size(); ++i) {
            acc += eligible[i].amount;
            if (r < acc) {
                out.push_back(eligible[i].id);
                total -= eligible[i].amount;
                eligible.erase(eligible.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
    }
    return out;
}

std::uint64_t stake_reward_pool(std::uint64_t subsidy_plus_fees, std::uint32_t bps) {
    const auto capped = std::min<std::uint32_t>(bps, 10000U);
    return static_cast<std::uint64_t>((static_cast<unsigned __int128>(subsidy_plus_fees) * capped) / 10000U);
}

std::uint64_t stake_reward_per_vote(std::uint64_t subsidy_plus_fees, std::uint32_t bps, std::size_t votes) {
    if (votes == 0) {
        return 0;
    }
    return stake_reward_pool(subsidy_plus_fees, bps) / votes;
}

bool StakingEngine::stake(const std::string&, std::uint64_t, std::string& error) {
    error = kDeprecated;
    return false;
}
bool StakingEngine::unstake(const std::string&, std::uint64_t, std::string& error) {
    error = kDeprecated;
    return false;
}
bool StakingEngine::consume_staked_credit(const std::string&, std::uint64_t, std::string& error) {
    error = kDeprecated;
    return false;
}

} // namespace addition
