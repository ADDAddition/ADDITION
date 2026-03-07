#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace addition {

class ShardManager {
public:
    struct Shard {
        std::string shard_id;
        std::uint64_t created_height{0};
        std::uint64_t traffic_tps{0};
        std::size_t active_accounts{0};
        bool active{true};
    };

    struct Plan {
        bool sharding_active{false};
        std::size_t shard_count{1};
        std::uint64_t threshold_tps{50000};
        std::string mode{"single"};
    };

    void configure_threshold(std::uint64_t threshold_tps);
    void update(std::uint64_t height, std::uint64_t observed_tps, const std::vector<std::string>& hot_accounts);

    Plan current_plan() const;
    std::string status() const;

    std::string dump_state() const;
    bool load_state(const std::string& state, std::string& error);

private:
    std::uint64_t threshold_tps_{50000};
    std::unordered_map<std::string, Shard> shards_;
    Plan plan_{};

    std::string pick_shard_for_account(const std::string& account) const;
};

} // namespace addition
