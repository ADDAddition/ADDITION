#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace addition {

class StakingEngine {
public:
    bool stake(const std::string& address, std::uint64_t amount, std::string& error);
    bool unstake(const std::string& address, std::uint64_t amount, std::string& error);
    std::uint64_t staked_of(const std::string& address) const;
    std::uint64_t total_staked() const;

    void distribute_epoch_rewards(std::uint64_t reward_pool);
    void set_reward_cap_bps(std::uint64_t cap_bps);
    std::uint64_t reward_cap_bps() const;
    std::uint64_t claimable_of(const std::string& address) const;
    std::uint64_t claim(const std::string& address);
    bool consume_staked_credit(const std::string& address, std::uint64_t amount, std::string& error);

    const std::unordered_map<std::string, std::uint64_t>& stakes_map() const;
    const std::unordered_map<std::string, std::uint64_t>& claimable_map() const;
    void replace_state(const std::unordered_map<std::string, std::uint64_t>& stakes,
                       const std::unordered_map<std::string, std::uint64_t>& claimable,
                       std::uint64_t total_staked);

private:
    std::unordered_map<std::string, std::uint64_t> stakes_;
    std::unordered_map<std::string, std::uint64_t> claimable_;
    std::uint64_t total_staked_{0};
    std::uint64_t reward_cap_bps_{300};
};

} // namespace addition
