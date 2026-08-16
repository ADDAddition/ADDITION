#include "addition/mempool.hpp"

#include <algorithm>

namespace addition {
namespace {

std::string outpoint_key(const TxInput& in) {
    return in.previous_txid + ":" + std::to_string(in.output_index);
}

} // namespace

std::string Mempool::signer_nonce_key(const Transaction& tx) const {
    if (tx.inputs.empty() || tx.signer.empty()) {
        return std::string();
    }
    return tx.signer + "#" + std::to_string(tx.nonce);
}

bool Mempool::looks_spendable(const Transaction& tx) const {
    if (tx.inputs.empty() || tx.outputs.empty()) {
        return false;
    }
    if (tx.signer.empty() || tx.signer_pubkey.empty()) {
        return false;
    }
    if (tx.signature.rfind("pq=", 0) != 0) {
        return false;
    }
    return true;
}

void Mempool::index_inputs(const Transaction& tx) {
    for (const auto& in : tx.inputs) {
        reserved_outpoints_.insert(outpoint_key(in));
    }
}

void Mempool::unindex_inputs(const Transaction& tx) {
    for (const auto& in : tx.inputs) {
        reserved_outpoints_.erase(outpoint_key(in));
    }
}

bool Mempool::submit(const Transaction& tx) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!looks_spendable(tx)) {
        return false;
    }
    const auto txid = hash_transaction(tx);
    if (!txids_.insert(txid).second) {
        return false;
    }
    const auto sn = signer_nonce_key(tx);
    if (!sn.empty() && !signer_nonces_.insert(sn).second) {
        txids_.erase(txid);
        return false;
    }
    for (const auto& in : tx.inputs) {
        if (reserved_outpoints_.count(outpoint_key(in)) != 0) {
            txids_.erase(txid);
            if (!sn.empty()) {
                signer_nonces_.erase(sn);
            }
            return false;
        }
    }
    index_inputs(tx);
    pending_.push_back(tx);
    return true;
}

std::vector<Transaction> Mempool::fetch_for_block(std::size_t max_count) {
    std::lock_guard<std::mutex> lk(mu_);

    std::sort(pending_.begin(), pending_.end(), [](const Transaction& a, const Transaction& b) {
        if (a.fee != b.fee) {
            return a.fee > b.fee;
        }
        if (a.outputs.size() != b.outputs.size()) {
            return a.outputs.size() < b.outputs.size();
        }
        return a.nonce < b.nonce;
    });

    const auto n = (max_count < pending_.size()) ? max_count : pending_.size();
    std::vector<Transaction> out;
    out.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        txids_.erase(hash_transaction(pending_[i]));
        const auto sn = signer_nonce_key(pending_[i]);
        if (!sn.empty()) {
            signer_nonces_.erase(sn);
        }
        unindex_inputs(pending_[i]);
        out.push_back(pending_[i]);
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(n));
    return out;
}

std::vector<Transaction> Mempool::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return pending_;
}

void Mempool::replace(const std::vector<Transaction>& txs) {
    std::lock_guard<std::mutex> lk(mu_);
    pending_.clear();
    txids_.clear();
    signer_nonces_.clear();
    reserved_outpoints_.clear();
    for (const auto& tx : txs) {
        if (!looks_spendable(tx)) {
            continue;
        }
        const auto txid = hash_transaction(tx);
        if (!txids_.insert(txid).second) {
            continue;
        }
        bool conflict = false;
        for (const auto& in : tx.inputs) {
            if (reserved_outpoints_.count(outpoint_key(in)) != 0) {
                conflict = true;
                break;
            }
        }
        if (conflict) {
            txids_.erase(txid);
            continue;
        }
        const auto sn = signer_nonce_key(tx);
        if (!sn.empty() && !signer_nonces_.insert(sn).second) {
            txids_.erase(txid);
            continue;
        }
        index_inputs(tx);
        pending_.push_back(tx);
    }
}

std::size_t Mempool::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return pending_.size();
}

} // namespace addition
