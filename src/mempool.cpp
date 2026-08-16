#include "addition/mempool.hpp"

#include <algorithm>

namespace addition {

std::string Mempool::signer_nonce_key(const Transaction& tx) const {
    if (tx.inputs.empty() || tx.signer.empty()) {
        return std::string();
    }
    return tx.signer + "#" + std::to_string(tx.nonce);
}

std::string Mempool::outpoint_key(const TxInput& in) const {
    return in.previous_txid + ":" + std::to_string(in.output_index);
}

bool Mempool::reserve_inputs_locked(const Transaction& tx) {
    std::vector<std::string> keys;
    keys.reserve(tx.inputs.size());
    for (const auto& in : tx.inputs) {
        const auto key = outpoint_key(in);
        if (!reserved_outpoints_.insert(key).second) {
            for (const auto& added : keys) {
                reserved_outpoints_.erase(added);
            }
            return false;
        }
        keys.push_back(key);
    }
    return true;
}

void Mempool::release_inputs_locked(const Transaction& tx) {
    for (const auto& in : tx.inputs) {
        reserved_outpoints_.erase(outpoint_key(in));
    }
}

bool Mempool::submit(const Transaction& tx) {
    std::lock_guard<std::mutex> lk(mu_);
    const auto txid = hash_transaction(tx);
    if (!txids_.insert(txid).second) {
        return false;
    }
    const auto sn = signer_nonce_key(tx);
    if (!sn.empty() && !signer_nonces_.insert(sn).second) {
        txids_.erase(txid);
        return false;
    }
    if (!reserve_inputs_locked(tx)) {
        if (!sn.empty()) {
            signer_nonces_.erase(sn);
        }
        txids_.erase(txid);
        return false;
    }
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
        release_inputs_locked(pending_[i]);
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
        const auto txid = hash_transaction(tx);
        if (!txids_.insert(txid).second) {
            continue;
        }
        const auto sn = signer_nonce_key(tx);
        if (!sn.empty() && !signer_nonces_.insert(sn).second) {
            txids_.erase(txid);
            continue;
        }
        if (!reserve_inputs_locked(tx)) {
            if (!sn.empty()) {
                signer_nonces_.erase(sn);
            }
            txids_.erase(txid);
            continue;
        }
        pending_.push_back(tx);
    }
}

void Mempool::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    pending_.clear();
    txids_.clear();
    signer_nonces_.clear();
    reserved_outpoints_.clear();
}

std::size_t Mempool::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return pending_.size();
}

} // namespace addition
