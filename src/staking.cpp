#include "addition/staking.hpp"

#include <algorithm>
#include <limits>

namespace addition {

bool StakingEngine::stake(const std::string& address, std::uint64_t amount, std::string& error) {
    if (address.empty()) {
        error = "address empty";
        return false;
    }
    if (amount == 0) {
        error = "amount must be > 0";
        return false;
    }

    stakes_[address] += amount;
    if (total_staked_ > (std::numeric_limits<std::uint64_t>::max() - amount)) {
        error = "staking overflow";
        stakes_[address] -= amount;
        if (stakes_[address] == 0) {
            stakes_.erase(address);
        }
        return false;
    }
    total_staked_ += amount;
    return true;
}

bool StakingEngine::unstake(const std::string& address, std::uint64_t amount, std::string& error) {
    if (address.empty()) {
        error = "address empty";
        return false;
    }
    if (amount == 0) {
        error = "amount must be > 0";
        return false;
    }

    auto it = stakes_.find(address);
    if (it == stakes_.end() || it->second < amount) {
        error = "insufficient staked balance";
        return false;
    }

    it->second -= amount;
    total_staked_ -= amount;
    return true;
}

std::uint64_t StakingEngine::staked_of(const std::string& address) const {
    auto it = stakes_.find(address);
    return it == stakes_.end() ? 0ULL : it->second;
}

std::uint64_t StakingEngine::total_staked() const { return total_staked_; }

void StakingEngine::distribute_epoch_rewards(std::uint64_t reward_pool) {
    if (reward_pool == 0 || total_staked_ == 0) {
        return;
    }

    const std::uint64_t max_pool = (total_staked_ * reward_cap_bps_) / 10000;
    const std::uint64_t bounded_pool = std::min(reward_pool, max_pool);
    if (bounded_pool == 0) {
        return;
    }

    std::uint64_t distributed = 0;
    for (const auto& [addr, staked] : stakes_) {
        const auto reward = (bounded_pool * staked) / total_staked_;
        if (reward == 0) {
            continue;
        }
        claimable_[addr] += reward;
        distributed += reward;
    }

    const std::uint64_t remainder = bounded_pool - distributed;
    if (remainder > 0) {
        auto best = stakes_.begin();
        for (auto it = stakes_.begin(); it != stakes_.end(); ++it) {
            if (it->second > best->second) {
                best = it;
            }
        }
        if (best != stakes_.end()) {
            claimable_[best->first] += remainder;
        }
    }
}

void StakingEngine::set_reward_cap_bps(std::uint64_t cap_bps) {
    if (cap_bps < 1) {
        reward_cap_bps_ = 1;
        return;
    }
    if (cap_bps > 2000) {
        reward_cap_bps_ = 2000;
        return;
    }
    reward_cap_bps_ = cap_bps;
}

std::uint64_t StakingEngine::reward_cap_bps() const {
    return reward_cap_bps_;
}

std::uint64_t StakingEngine::claimable_of(const std::string& address) const {
    auto it = claimable_.find(address);
    return it == claimable_.end() ? 0ULL : it->second;
}

std::uint64_t StakingEngine::claim(const std::string& address) {
    auto it = claimable_.find(address);
    if (it == claimable_.end()) {
        return 0;
    }
    const auto value = it->second;
    it->second = 0;
    return value;
}

bool StakingEngine::consume_staked_credit(const std::string& address, std::uint64_t amount, std::string& error) {
    if (address.empty()) {
        error = "address empty";
        return false;
    }
    if (amount == 0) {
        return true;
    }
    auto it = stakes_.find(address);
    if (it == stakes_.end() || it->second < amount) {
        error = "insufficient staked credit";
        return false;
    }
    it->second -= amount;
    if (it->second == 0) {
        stakes_.erase(it);
    }
    if (total_staked_ < amount) {
        error = "staking accounting underflow";
        return false;
    }
    total_staked_ -= amount;
    return true;
}

const std::unordered_map<std::string, std::uint64_t>& StakingEngine::stakes_map() const {
    return stakes_;
}

const std::unordered_map<std::string, std::uint64_t>& StakingEngine::claimable_map() const {
    return claimable_;
}

void StakingEngine::replace_state(const std::unordered_map<std::string, std::uint64_t>& stakes,
                                  const std::unordered_map<std::string, std::uint64_t>& claimable,
                                  std::uint64_t total_staked) {
    stakes_ = stakes;
    claimable_ = claimable;
    std::uint64_t recomputed_total = 0;
    for (const auto& [_, amount] : stakes_) {
        if (recomputed_total > (std::numeric_limits<std::uint64_t>::max() - amount)) {
            total_staked_ = total_staked;
            return;
        }
        recomputed_total += amount;
    }
    total_staked_ = recomputed_total;
}

} // namespace addition
