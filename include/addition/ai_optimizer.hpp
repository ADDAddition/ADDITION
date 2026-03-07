#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace addition {

class AIRoutingOptimizer {
public:
    void observe(std::size_t mempool_size, std::uint64_t fees_last_block, std::uint64_t height);
    std::uint64_t recommended_fee_floor() const;
    std::uint64_t suggested_difficulty_bias_bps() const;
    std::string status() const;

private:
    struct Sample {
        std::uint64_t height{0};
        std::size_t mempool_size{0};
        std::uint64_t fees_last_block{0};
    };

    std::deque<Sample> samples_;
    std::uint64_t fee_floor_{1};
    std::uint64_t difficulty_bias_bps_{10000};
};

} // namespace addition
