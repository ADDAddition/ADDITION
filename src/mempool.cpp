#include "addition/mempool.hpp"
#include "addition/shielded.hpp"

#include <algorithm>
#include <unordered_map>

namespace addition {
namespace {

std::string outpoint_key(const TxInput& in) {
    return in.previous_txid + ":" + std::to_string(in.output_index);
}

// Fully shielded tx (no transparent inputs): its double-spend keys are the
// bundle nullifiers. Returns false if the bundle does not parse.
bool shielded_nullifier_keys(const Transaction& tx, std::vector<std::string>& out) {
    out.clear();
    if (tx.shielded.empty()) return true;
    shielded::Bundle b;
    std::string err;
    if (!shielded::from_tx_field(tx.shielded, b, err)) return false;
    for (const auto& nf : b.nullifiers) out.push_back("nf:" + shielded::hex32(nf));
    return true;
}

} // namespace

std::string Mempool::signer_nonce_key(const Transaction& tx) const {
    if (tx.inputs.empty() || tx.signer.empty()) {
        return std::string();
    }
    return tx.signer + "#" + std::to_string(tx.nonce);
}

bool Mempool::looks_spendable(const Transaction& tx) const {
    if (tx.inputs.empty() && !tx.shielded.empty()) {
        // Fully shielded (z->z or z->t): no transparent inputs or signer. The
        // STARK proof and nullifiers are verified by Chain; here we only require
        // a parseable bundle that spends at least one nullifier.
        if (!tx.signer.empty() || !tx.signature.empty()) return false;
        std::vector<std::string> nfs;
        return shielded_nullifier_keys(tx, nfs) && !nfs.empty();
    }
    if (tx.inputs.empty() || tx.outputs.empty()) {
        return false;
    }
    if (tx.signer.empty() && tx.signer_pubkey.empty() && tx.signature.empty() && !tx.inputs.empty()) {
        // Script-v1 spend: every input must carry a witness (verified by Chain).
        for (const auto& in : tx.inputs) {
            if (in.witness.empty()) return false;
        }
        return true;
    }
    if (tx.signer.empty()) {  // empty pubkey => on-chain registry (chain validates)
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
    std::vector<std::string> nfs;
    if (shielded_nullifier_keys(tx, nfs)) {
        for (const auto& k : nfs) reserved_outpoints_.insert(k);
    }
}

void Mempool::unindex_inputs(const Transaction& tx) {
    for (const auto& in : tx.inputs) {
        reserved_outpoints_.erase(outpoint_key(in));
    }
    std::vector<std::string> nfs;
    if (shielded_nullifier_keys(tx, nfs)) {
        for (const auto& k : nfs) reserved_outpoints_.erase(k);
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
    std::vector<std::string> keys;
    for (const auto& in : tx.inputs) keys.push_back(outpoint_key(in));
    {
        std::vector<std::string> nfs;
        shielded_nullifier_keys(tx, nfs);
        keys.insert(keys.end(), nfs.begin(), nfs.end());
    }
    for (const auto& key : keys) {
        if (reserved_outpoints_.count(key) != 0) {
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

    // Nonce chaining: several pending txs per signer are allowed, but within
    // a block each signer's txs must apply in ascending nonce order. Keep the
    // fee-ordered slots, and reassign each signer's txs to its slots by nonce.
    {
        std::unordered_map<std::string, std::vector<std::size_t>> slots;
        for (std::size_t i = 0; i < pending_.size(); ++i) {
            if (!pending_[i].signer.empty()) slots[pending_[i].signer].push_back(i);
        }
        for (auto& [signer, idx] : slots) {
            if (idx.size() < 2) continue;
            std::vector<Transaction> txs;
            for (auto i : idx) txs.push_back(pending_[i]);
            std::stable_sort(txs.begin(), txs.end(),
                             [](const Transaction& a, const Transaction& b) { return a.nonce < b.nonce; });
            for (std::size_t k = 0; k < idx.size(); ++k) pending_[idx[k]] = std::move(txs[k]);
        }
    }
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

std::size_t Mempool::remove_confirmed(const std::vector<Transaction>& block_txs) {
    std::lock_guard<std::mutex> lk(mu_);
    if (pending_.empty() || block_txs.empty()) {
        return 0;
    }
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> spent;
    std::unordered_set<std::string> nonces;
    for (const auto& tx : block_txs) {
        ids.insert(hash_transaction(tx));
        for (const auto& in : tx.inputs) spent.insert(outpoint_key(in));
        std::vector<std::string> nfs;
        if (shielded_nullifier_keys(tx, nfs)) spent.insert(nfs.begin(), nfs.end());
        const auto sn = signer_nonce_key(tx);
        if (!sn.empty()) nonces.insert(sn);
    }
    std::vector<Transaction> keep;
    keep.reserve(pending_.size());
    std::size_t removed = 0;
    for (auto& tx : pending_) {
        bool drop = ids.count(hash_transaction(tx)) != 0;
        if (!drop) {
            const auto sn = signer_nonce_key(tx);
            drop = !sn.empty() && nonces.count(sn) != 0;
        }
        if (!drop) {
            for (const auto& in : tx.inputs) {
                if (spent.count(outpoint_key(in)) != 0) { drop = true; break; }
            }
        }
        if (!drop) {
            std::vector<std::string> nfs;
            if (shielded_nullifier_keys(tx, nfs)) {
                for (const auto& k : nfs) {
                    if (spent.count(k) != 0) { drop = true; break; }
                }
            }
        }
        if (drop) {
            txids_.erase(hash_transaction(tx));
            const auto sn = signer_nonce_key(tx);
            if (!sn.empty()) signer_nonces_.erase(sn);
            unindex_inputs(tx);
            ++removed;
        } else {
            keep.push_back(std::move(tx));
        }
    }
    pending_ = std::move(keep);
    return removed;
}

std::uint64_t Mempool::max_pending_nonce(const std::string& signer) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::uint64_t m = 0;
    for (const auto& tx : pending_) {
        if (tx.signer == signer && tx.nonce > m) m = tx.nonce;
    }
    return m;
}

bool Mempool::outpoint_reserved(const std::string& key) const {
    std::lock_guard<std::mutex> lk(mu_);
    return reserved_outpoints_.count(key) != 0;
}

std::size_t Mempool::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return pending_.size();
}

} // namespace addition
