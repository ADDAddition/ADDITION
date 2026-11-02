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
    // Drop pending txs confirmed by (or conflicting with) a connected block:
    // same txid, a spent outpoint/nullifier, or the same signer nonce.
    // Returns how many pending txs were removed.
    std::size_t remove_confirmed(const std::vector<Transaction>& block_txs);
    std::size_t size() const;
    // Highest pending nonce for an account signer (0 if none).
    std::uint64_t max_pending_nonce(const std::string& signer) const;
    // "txid:index" reserved by a pending tx.
    bool outpoint_reserved(const std::string& key) const;

private:
    std::string signer_nonce_key(const Transaction& tx) const;
    bool looks_spendable(const Transaction& tx) const;
    void index_inputs(const Transaction& tx);
    void unindex_inputs(const Transaction& tx);

    mutable std::mutex mu_;
    std::vector<Transaction> pending_;
    std::unordered_set<std::string> txids_;
    std::unordered_set<std::string> signer_nonces_;
    std::unordered_set<std::string> reserved_outpoints_;
};

} // namespace addition
