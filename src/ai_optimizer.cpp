#include "addition/ai_optimizer.hpp"

#include <algorithm>
#include <sstream>

namespace addition {

void AIRoutingOptimizer::observe(std::size_t mempool_size, std::uint64_t fees_last_block, std::uint64_t height) {
    samples_.push_back(Sample{height, mempool_size, fees_last_block});
    while (samples_.size() > 64) {
        samples_.pop_front();
    }

    std::size_t avg_mempool = 0;
    std::uint64_t avg_fees = 0;
    for (const auto& s : samples_) {
        avg_mempool += s.mempool_size;
        avg_fees += s.fees_last_block;
    }
    if (!samples_.empty()) {
        avg_mempool /= samples_.size();
        avg_fees /= static_cast<std::uint64_t>(samples_.size());
    }

    std::uint64_t base = 1;
    if (avg_mempool > 1500) base += 25;
    else if (avg_mempool > 800) base += 14;
    else if (avg_mempool > 400) base += 8;
    else if (avg_mempool > 150) base += 4;

    base += std::min<std::uint64_t>(avg_fees / 50, 32);
    fee_floor_ = std::max<std::uint64_t>(1, base);

    if (avg_mempool > 1500) difficulty_bias_bps_ = 9800;
    else if (avg_mempool > 800) difficulty_bias_bps_ = 9900;
    else if (avg_mempool > 400) difficulty_bias_bps_ = 9950;
    else if (avg_mempool < 40) difficulty_bias_bps_ = 10100;
    else difficulty_bias_bps_ = 10000;
}

std::uint64_t AIRoutingOptimizer::recommended_fee_floor() const {
    return fee_floor_;
}

std::uint64_t AIRoutingOptimizer::suggested_difficulty_bias_bps() const {
    return difficulty_bias_bps_;
}

std::string AIRoutingOptimizer::status() const {
    std::ostringstream oss;
    oss << "samples=" << samples_.size()
        << " fee_floor=" << fee_floor_
        << " difficulty_bias_bps=" << difficulty_bias_bps_;
    return oss.str();
}

} // namespace addition
