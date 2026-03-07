#pragma once

#include "addition/block.hpp"

#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

namespace addition {

class Mempool {
public:
    bool submit(const Transaction& tx);
    std::vector<Transaction> fetch_for_block(std::size_t max_count);
    std::vector<Transaction> snapshot() const;
    void replace(const std::vector<Transaction>& txs);
    std::size_t size() const;

private:
    std::string signer_nonce_key(const Transaction& tx) const;

    mutable std::mutex mu_;
    std::vector<Transaction> pending_;
    std::unordered_set<std::string> txids_;
    std::unordered_set<std::string> signer_nonces_;
};

} // namespace addition
