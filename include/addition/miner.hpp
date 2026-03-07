#pragma once

#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/mempool.hpp"

namespace addition {

class Miner {
public:
    Miner(Chain& chain, Mempool& mempool);
    bool mine_next_block(const std::string& reward_address,
                         std::size_t max_txs,
                         std::size_t threads,
                         std::string& mined_hash,
                         std::string& error);
    double last_tps() const;
    std::uint64_t last_mine_ms() const;
    std::size_t last_mined_txs() const;

private:
    Chain& chain_;
    Mempool& mempool_;
    double last_tps_{0.0};
    std::uint64_t last_mine_ms_{0};
    std::size_t last_mined_txs_{0};
};

} // namespace addition
