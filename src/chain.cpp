#include "addition/chain.hpp"

#include "addition/crypto.hpp"
#include "addition/miner.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace addition {
namespace {

bool signer_binds_pubkey(const Transaction& tx, std::string& error) {
    const auto scheme = infer_sig_scheme_from_pubkey_hex(tx.signer_pubkey);
    if (scheme == SigScheme::Unknown || !sig_scheme_allowed_strict(scheme)) {
        error = "unknown scheme rejected in strict mode";
        return false;
    }
    return address_binds_pubkey(tx.signer, scheme, tx.signer_pubkey, error);
}

std::uint64_t now_seconds() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

std::uint64_t work_for_target(std::uint64_t target) {
    if (target == 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return std::numeric_limits<std::uint64_t>::max() / target;
}

} // namespace

Chain::Chain(ChainConfig cfg) : cfg_(std::move(cfg)) {
    difficulty_target_ = cfg_.initial_difficulty_target;
    auto g = make_genesis();
    blocks_.push_back(g);
    cumulative_work_ = work_for_target(g.header.difficulty_target);
}

void Chain::set_on_commit(std::function<void()> hook) {
    on_commit_ = std::move(hook);
}

void Chain::notify_commit() {
    if (suppress_commit_ != 0 || !on_commit_) {
        return;
    }
    try {
        on_commit_();
    } catch (...) {
    }
}

void Chain::reset() {
    blocks_.clear();
    utxo_set_.clear();
    address_index_.clear();
    signer_last_nonce_.clear();
    seen_transactions_.clear();
    difficulty_target_ = cfg_.initial_difficulty_target;
    total_emitted_ = 0;
    total_fees_last_block_ = 0;
    cumulative_work_ = 0;

    auto g = make_genesis();
    blocks_.push_back(g);
    cumulative_work_ += work_for_target(g.header.difficulty_target);
}

Block Chain::make_genesis() const {
    Block g{};
    g.header.height = 0;
    // Testnet keeps previous_hash=0 so the live ADDITION_TESTNET_V1 genesis is unchanged.
    // Mainnet and --regtest bind the genesis header to network_id so hashes cannot match.
    if (cfg_.network_id == kMainnetNetworkId || cfg_.network_id == kRegtestNetworkId) {
        g.header.previous_hash = std::string("genesis:") + cfg_.network_id;
    } else {
        g.header.previous_hash = "0";
    }
    g.header.timestamp = cfg_.genesis_timestamp;
    g.header.nonce = 0;
    g.header.difficulty_target = difficulty_target_;
    g.header.merkle_root = compute_merkle_root(g.transactions);
    return g;
}

const Block& Chain::genesis_block() const { return blocks_.front(); }
const Block& Chain::tip() const { return blocks_.back(); }
std::uint64_t Chain::height() const { return blocks_.back().header.height; }
const std::vector<Block>& Chain::blocks() const { return blocks_; }
std::optional<Block> Chain::block_at(std::uint64_t h) const {
    if (h >= blocks_.size()) {
        return std::nullopt;
    }
    return blocks_[static_cast<std::size_t>(h)];
}

std::optional<Block> Chain::block_by_hash(const std::string& hash) const {
    for (const auto& b : blocks_) {
        if (hash_block_header(b.header) == hash) {
            return b;
        }
    }
    return std::nullopt;
}

bool Chain::has_block_hash(const std::string& hash) const {
    return block_by_hash(hash).has_value();
}

std::uint64_t Chain::cumulative_work() const { return cumulative_work_; }

std::string Chain::genesis_hash() const {
    return hash_block_header(genesis_block().header);
}

std::uint64_t Chain::tx_confirmations(const std::string& tx_hash) const {
    if (tx_hash.empty()) {
        return 0;
    }
    for (const auto& b : blocks_) {
        for (const auto& tx : b.transactions) {
            if (hash_transaction(tx) == tx_hash) {
                return height() >= b.header.height ? (height() - b.header.height + 1) : 0;
            }
        }
    }
    return 0;
}

bool Chain::tx_in_best_chain(const std::string& tx_hash) const {
    return tx_confirmations(tx_hash) > 0;
}
std::uint64_t Chain::current_difficulty_target() const { return difficulty_target_; }
std::uint64_t Chain::total_fees_last_block() const { return total_fees_last_block_; }

std::uint64_t Chain::current_block_reward() const { return compute_block_reward(height() + 1); }
std::uint64_t Chain::max_supply() const { return cfg_.max_supply; }
std::uint64_t Chain::total_emitted() const { return total_emitted_; }
std::uint64_t Chain::remaining_supply() const {
    return (cfg_.max_supply > total_emitted_) ? (cfg_.max_supply - total_emitted_) : 0ULL;
}
std::uint64_t Chain::next_halving_height() const {
    const auto h = height() + 1;
    const auto k = h / cfg_.halving_interval;
    return static_cast<std::uint64_t>(k + 1) * cfg_.halving_interval;
}

std::string Chain::outpoint_key(const std::string& txid, std::uint32_t output_index) const {
    return txid + ":" + std::to_string(output_index);
}

bool Chain::hash_meets_target(const std::string& hex_hash, std::uint64_t target) const {
    switch (cfg_.pow_algorithm) {
    case PowAlgorithm::Sha3_512:
        return hash_head64(hex_hash) <= target;
    case PowAlgorithm::MemoryHard:
        return memory_hard_head64(hex_hash) <= target;
    }
    const PowAlgorithm missing = cfg_.pow_algorithm;
    switch (missing) {
    case PowAlgorithm::Sha3_512:
    case PowAlgorithm::MemoryHard:
        break;
    }
    // Fail closed: never accept PoW via plain SHA3 when algorithm is unknown.
    return false;
}

std::uint64_t Chain::compute_block_reward(std::uint64_t h) const {
    const auto halvings = h / cfg_.halving_interval;
    if (halvings >= 63) {
        return cfg_.tail_emission_reward;
    }
    const auto shifted = cfg_.block_reward >> halvings;
    if (shifted == 0) {
        return cfg_.tail_emission_reward;
    }
    return shifted;
}

std::uint64_t Chain::clamp_difficulty_target(std::uint64_t target) const {
    if (target < cfg_.min_difficulty_target) {
        return cfg_.min_difficulty_target;
    }
    if (target > cfg_.max_difficulty_target) {
        return cfg_.max_difficulty_target;
    }
    return target;
}

std::uint64_t Chain::compute_next_difficulty_target() const {
    const auto window = static_cast<std::size_t>(cfg_.retarget_window);
    // Need genesis + at least `window` mined blocks. Retarget only on window boundaries
    // so a 2-block CI mine stays at the easy bound.
    if (window == 0 || blocks_.size() < window + 1) {
        return clamp_difficulty_target(difficulty_target_);
    }
    if ((blocks_.back().header.height % static_cast<std::uint64_t>(window)) != 0) {
        return clamp_difficulty_target(difficulty_target_);
    }

    // Skip genesis as the oldest bound. Its frozen timestamp (Nov 2025) vs "now"
    // made the first window look months-slow and ratcheted the target to 2^56-1.
    std::size_t oldest_index = blocks_.size() - 1 - window;
    if (oldest_index == 0) {
        oldest_index = 1;
    }
    if (oldest_index >= blocks_.size() - 1) {
        return clamp_difficulty_target(difficulty_target_);
    }

    const auto& newest = blocks_.back();
    const auto& oldest = blocks_[oldest_index];
    const auto observed = (newest.header.timestamp > oldest.header.timestamp)
                              ? (newest.header.timestamp - oldest.header.timestamp)
                              : 1ULL;
    const auto expected = static_cast<std::uint64_t>(window) * cfg_.target_block_time_sec;
    if (expected == 0) {
        return clamp_difficulty_target(difficulty_target_);
    }

    const std::uint64_t floor_span = std::max<std::uint64_t>(1ULL, expected / 4ULL);
    const std::uint64_t observed_capped =
        std::min<std::uint64_t>(std::max<std::uint64_t>(observed, floor_span), expected * 4ULL);

    std::uint64_t next = difficulty_target_;
    if (difficulty_target_ > (std::numeric_limits<std::uint64_t>::max() / observed_capped)) {
        next = cfg_.max_difficulty_target;
    } else {
        next = (difficulty_target_ * observed_capped) / expected;
    }
    return clamp_difficulty_target(next);
}

std::uint64_t Chain::balance_of(const std::string& address) const {
    const auto it = address_index_.find(address);
    return it == address_index_.end() ? 0ULL : it->second;
}

std::uint64_t Chain::last_nonce(const std::string& address) const {
    const auto it = signer_last_nonce_.find(address);
    return it == signer_last_nonce_.end() ? 0ULL : it->second;
}

std::uint64_t Chain::next_nonce(const std::string& address) const {
    return last_nonce(address) + 1ULL;
}

bool Chain::credit_balance(const std::string& address,
                          std::uint64_t amount,
                          const std::string& reason,
                          std::string& error) {
    if (address.empty()) {
        error = "address empty";
        return false;
    }
    if (amount == 0) {
        error = "amount must be > 0";
        return false;
    }
    if (reason.empty()) {
        error = "reason empty";
        return false;
    }

    const auto txid = to_hex(sha3_512_bytes("credit|" + reason + "|" + address + "|" + std::to_string(amount) + "|" + std::to_string(height() + 1)));
    if (seen_transactions_.count(txid) > 0) {
        error = "duplicate credit tx";
        return false;
    }

    const auto key = outpoint_key(txid, 0);
    if (utxo_set_.count(key) > 0) {
        error = "duplicate credit outpoint";
        return false;
    }

    utxo_set_[key] = UTXO{address, amount, false};
    if (address_index_[address] > (std::numeric_limits<std::uint64_t>::max() - amount)) {
        utxo_set_.erase(key);
        error = "balance overflow";
        return false;
    }
    address_index_[address] += amount;
    seen_transactions_.insert(txid);
    return true;
}

bool Chain::build_transaction(const std::string& from,
                              const std::string& to,
                              std::uint64_t amount,
                              std::uint64_t fee,
                              std::uint64_t nonce,
                              Transaction& out_tx,
                              std::string& error) const {
    if (from.empty() || to.empty()) {
        error = "from/to empty";
        return false;
    }
    if (amount == 0) {
        error = "amount must be > 0";
        return false;
    }
    if (fee > (std::numeric_limits<std::uint64_t>::max() - amount)) {
        error = "amount+fee overflow";
        return false;
    }

    const std::uint64_t required = amount + fee;
    std::uint64_t gathered = 0;
    std::vector<TxInput> selected;

    for (const auto& [key, utxo] : utxo_set_) {
        if (utxo.spent || utxo.owner != from) {
            continue;
        }

        const auto sep = key.rfind(':');
        if (sep == std::string::npos) {
            continue;
        }

        TxInput in{};
        in.previous_txid = key.substr(0, sep);
        in.output_index = static_cast<std::uint32_t>(std::stoul(key.substr(sep + 1)));
        selected.push_back(std::move(in));

        gathered += utxo.amount;
        if (gathered >= required) {
            break;
        }
    }

    if (gathered < required) {
        error = "insufficient balance";
        return false;
    }

    out_tx = Transaction{};
    out_tx.inputs = std::move(selected);
    out_tx.outputs.push_back(TxOutput{to, amount});
    const auto change = gathered - required;
    if (change > 0) {
        out_tx.outputs.push_back(TxOutput{from, change});
    }
    out_tx.fee = fee;
    out_tx.nonce = nonce;
    return true;
}

bool Chain::validate_transaction(const Transaction& tx, std::string& error) const {
    return validate_transaction(tx, error, true);
}

bool Chain::validate_transaction(const Transaction& tx, std::string& error, bool check_signature) const {
    if (tx.outputs.empty()) {
        error = "transaction has no outputs";
        return false;
    }

    std::uint64_t outputs_total = 0;
    for (const auto& out : tx.outputs) {
        if (out.recipient.empty()) {
            error = "output recipient empty";
            return false;
        }
        if (out.amount == 0) {
            error = "output amount zero";
            return false;
        }
        if (outputs_total > (std::numeric_limits<std::uint64_t>::max() - out.amount)) {
            error = "outputs overflow";
            return false;
        }
        outputs_total += out.amount;
    }

    if (tx.inputs.empty()) {
        if (!tx.signer.empty() || !tx.signature.empty()) {
            error = "coinbase must not be signed";
            return false;
        }
        if (outputs_total > cfg_.block_reward + tx.fee) {
            error = "coinbase exceeds allowed emission";
            return false;
        }
        return true;
    }

    if (cfg_.min_fee > 0 && tx.fee < cfg_.min_fee) {
        error = "fee below network minimum";
        return false;
    }

    if (!validate_transaction_signature(tx, error, check_signature)) {
        return false;
    }

    {
        const auto it = signer_last_nonce_.find(tx.signer);
        if (it != signer_last_nonce_.end() && tx.nonce <= it->second) {
            error = "nonce replay or out-of-order";
            return false;
        }
    }

    std::uint64_t inputs_total = 0;
    std::unordered_set<std::string> seen_inputs;
    for (const auto& in : tx.inputs) {
        const auto key = outpoint_key(in.previous_txid, in.output_index);
        if (!seen_inputs.insert(key).second) {
            error = "duplicate input in transaction";
            return false;
        }
        const auto it = utxo_set_.find(key);
        if (it == utxo_set_.end() || it->second.spent) {
            error = "input utxo not found or spent";
            return false;
        }
        if (inputs_total > (std::numeric_limits<std::uint64_t>::max() - it->second.amount)) {
            error = "inputs overflow";
            return false;
        }
        inputs_total += it->second.amount;
    }

    if (outputs_total > (std::numeric_limits<std::uint64_t>::max() - tx.fee)) {
        error = "outputs+fee overflow";
        return false;
    }

    if (inputs_total < outputs_total + tx.fee) {
        error = "inputs < outputs + fee";
        return false;
    }

    return true;
}

Block Chain::make_block_template(const std::string& reward_address,
                                 std::vector<Transaction> txs,
                                 std::uint64_t reward) const {
    Block b{};
    b.header.height = height() + 1;
    b.header.previous_hash = hash_block_header(tip().header);
    b.header.timestamp = now_seconds();
    b.header.difficulty_target = difficulty_target_;

    Transaction coinbase{};
    coinbase.outputs.push_back(TxOutput{reward_address, reward});
    coinbase.nonce = b.header.height;

    b.transactions.push_back(std::move(coinbase));
    for (auto& tx : txs) {
        b.transactions.push_back(std::move(tx));
    }
    b.header.merkle_root = compute_merkle_root(b.transactions);
    return b;
}

bool Chain::mine_and_add_block(const std::string& reward_address,
                               std::vector<Transaction> txs,
                               std::size_t threads,
                               std::string& mined_hash,
                               std::string& error,
                               std::atomic<bool>* stop) {
    const auto emission_left = (cfg_.max_supply > total_emitted_) ? (cfg_.max_supply - total_emitted_) : 0ULL;
    const auto reward = std::min<std::uint64_t>(current_block_reward(), emission_left);
    auto b = make_block_template(reward_address, std::move(txs), reward);

    PowSearchConfig search_cfg{};
    search_cfg.algorithm = cfg_.pow_algorithm;
    search_cfg.target = b.header.difficulty_target;
    search_cfg.threads = threads;
    search_cfg.deadline_sec = mine_deadline_seconds(cfg_);
    search_cfg.stop = stop;

    const auto found = search_block_pow(b.header, search_cfg);
    if (found.found) {
        b.header.nonce = found.nonce;
        if (!add_block(b, error)) {
            return false;
        }
        mined_hash = found.header_hash;
        return true;
    }

    if (found.stopped) {
        error = "mining stopped";
        return false;
    }

    if (found.deadline_hit) {
        if (search_cfg.deadline_sec == kTestnetMineDeadlineSec) {
            error = "mining deadline exceeded (30s); testnet uses sha3_512 header PoW";
        } else {
            error = "mining deadline exceeded (" + std::to_string(search_cfg.deadline_sec) + "s)";
        }
        return false;
    }

    error = "mining search exhausted";
    return false;
}

bool Chain::validate_transaction_signature(const Transaction& tx, std::string& error) const {
    return validate_transaction_signature(tx, error, true);
}

bool Chain::validate_transaction_signature(const Transaction& tx, std::string& error, bool check_crypto) const {
    if (cfg_.require_pq_signatures && tx.signature.rfind("pq=", 0) != 0) {
        error = "non-PQ signature rejected in strict mode";
        return false;
    }

    if (tx.signer.empty()) {
        error = "missing signer";
        return false;
    }
    if (tx.signer_pubkey.empty()) {
        error = "missing signer public key";
        return false;
    }
    if (tx.signature.empty()) {
        error = "missing signature";
        return false;
    }

    if (!signer_binds_pubkey(tx, error)) {
        return false;
    }

    bool owns_any_input = false;
    for (const auto& in : tx.inputs) {
        const auto key = outpoint_key(in.previous_txid, in.output_index);
        const auto it = utxo_set_.find(key);
        if (it != utxo_set_.end() && !it->second.spent && it->second.owner == tx.signer) {
            owns_any_input = true;
            break;
        }
    }

    if (!owns_any_input) {
        error = "signer does not control inputs";
        return false;
    }

    if (!check_crypto) {
        return true;
    }

    Transaction unsigned_tx = tx;
    unsigned_tx.signature.clear();
    const auto msg = hash_transaction(unsigned_tx);
    if (!verify_message_signature_hybrid(tx.signer_pubkey, msg, tx.signature)) {
        error = "invalid signature";
        return false;
    }
    return true;
}

bool Chain::apply_transaction(const Transaction& tx, const std::string& txid, std::string& error) {
    return apply_transaction(tx, txid, error, true);
}

bool Chain::apply_transaction(const Transaction& tx,
                              const std::string& txid,
                              std::string& error,
                              bool check_signature) {
    if (!validate_transaction(tx, error, check_signature)) {
        return false;
    }

    for (const auto& in : tx.inputs) {
        const auto key = outpoint_key(in.previous_txid, in.output_index);
        auto it = utxo_set_.find(key);
        if (it == utxo_set_.end() || it->second.spent) {
            error = "cannot spend missing utxo";
            return false;
        }
        it->second.spent = true;
        if (address_index_[it->second.owner] >= it->second.amount) {
            address_index_[it->second.owner] -= it->second.amount;
        } else {
            address_index_[it->second.owner] = 0;
        }
    }

    for (std::uint32_t i = 0; i < tx.outputs.size(); ++i) {
        const auto key = outpoint_key(txid, i);
        UTXO out{tx.outputs[i].recipient, tx.outputs[i].amount, false};
        utxo_set_[key] = out;
        if (address_index_[out.owner] > (std::numeric_limits<std::uint64_t>::max() - out.amount)) {
            error = "balance index overflow";
            return false;
        }
        address_index_[out.owner] += out.amount;
    }

    if (!tx.inputs.empty()) {
        signer_last_nonce_[tx.signer] = tx.nonce;
    }

    seen_transactions_.insert(txid);
    return true;
}

bool Chain::validate_block_header(const Block& candidate, std::string& error) const {
    const auto& tip_block = tip();

    if (candidate.header.height != tip_block.header.height + 1) {
        error = "invalid height";
        return false;
    }

    const auto expected_prev = hash_block_header(tip_block.header);
    if (candidate.header.previous_hash != expected_prev) {
        error = "invalid previous hash";
        return false;
    }

    if (candidate.header.merkle_root != compute_merkle_root(candidate.transactions)) {
        error = "invalid merkle root";
        return false;
    }

    if (candidate.header.timestamp < tip_block.header.timestamp) {
        error = "timestamp regression";
        return false;
    }

    if (candidate.header.difficulty_target > cfg_.max_difficulty_target) {
        error = "min-diff: toy difficulty rejected";
        return false;
    }
    if (candidate.header.difficulty_target < cfg_.min_difficulty_target) {
        error = "invalid difficulty target (below min-diff floor)";
        return false;
    }
    if (candidate.header.difficulty_target != difficulty_target_) {
        error = "invalid difficulty target";
        return false;
    }

    const auto pow_hash = hash_block_header(candidate.header);
    if (!hash_meets_target(pow_hash, candidate.header.difficulty_target)) {
        error = "invalid proof of work";
        return false;
    }

    return true;
}

bool Chain::validate_block_transactions(const Block& candidate, std::string& error) const {
    if (candidate.transactions.empty()) {
        error = "block must include coinbase";
        return false;
    }

    const auto& coinbase = candidate.transactions.front();
    if (!coinbase.inputs.empty()) {
        error = "coinbase must not have inputs";
        return false;
    }
    if (coinbase.outputs.empty()) {
        if (!cfg_.allow_zero_reward_blocks) {
            error = "coinbase must have output";
            return false;
        }
    }
    std::uint64_t coinbase_total = 0;
    for (const auto& out : coinbase.outputs) {
        if (out.recipient.empty() || out.amount == 0) {
            error = "invalid coinbase output";
            return false;
        }
        if (coinbase_total > (std::numeric_limits<std::uint64_t>::max() - out.amount)) {
            error = "coinbase overflow";
            return false;
        }
        coinbase_total += out.amount;
    }
    std::uint64_t fees = 0;
    for (std::size_t i = 1; i < candidate.transactions.size(); ++i) {
        if (candidate.transactions[i].inputs.empty()) {
            error = "non-coinbase tx must have inputs";
            return false;
        }
        if (fees > (std::numeric_limits<std::uint64_t>::max() - candidate.transactions[i].fee)) {
            error = "fees overflow";
            return false;
        }
        fees += candidate.transactions[i].fee;
    }
    const auto base_reward = compute_block_reward(candidate.header.height);
    if (base_reward > (std::numeric_limits<std::uint64_t>::max() - fees)) {
        error = "allowed reward overflow";
        return false;
    }
    const auto allowed_reward = base_reward + fees;
    if (coinbase_total > allowed_reward) {
        error = "coinbase exceeds allowed reward+fees";
        return false;
    }

    const auto minted = (coinbase_total > fees) ? (coinbase_total - fees) : 0ULL;
    if (minted > (cfg_.max_supply - total_emitted_)) {
        error = "max supply exceeded";
        return false;
    }

    auto snapshot_utxo = utxo_set_;
    auto snapshot_index = address_index_;
    for (const auto& tx : candidate.transactions) {
        if (tx.outputs.empty()) {
            error = "tx outputs empty";
            return false;
        }

        std::uint64_t outputs_total = 0;
        for (const auto& out : tx.outputs) {
            if (out.recipient.empty() || out.amount == 0) {
                error = "invalid tx output";
                return false;
            }
            if (outputs_total > (std::numeric_limits<std::uint64_t>::max() - out.amount)) {
                error = "block tx outputs overflow";
                return false;
            }
            outputs_total += out.amount;
        }

        std::uint64_t inputs_total = 0;
        for (const auto& in : tx.inputs) {
            const auto key = outpoint_key(in.previous_txid, in.output_index);
            auto it = snapshot_utxo.find(key);
            if (it == snapshot_utxo.end() || it->second.spent) {
                error = "invalid or spent input in block";
                return false;
            }
            if (inputs_total > (std::numeric_limits<std::uint64_t>::max() - it->second.amount)) {
                error = "inputs overflow";
                return false;
            }
            inputs_total += it->second.amount;
            it->second.spent = true;
            if (snapshot_index[it->second.owner] >= it->second.amount) {
                snapshot_index[it->second.owner] -= it->second.amount;
            } else {
                snapshot_index[it->second.owner] = 0;
            }
        }

        if (!tx.inputs.empty() && outputs_total > (std::numeric_limits<std::uint64_t>::max() - tx.fee)) {
            error = "block tx outputs+fee overflow";
            return false;
        }
        if (!tx.inputs.empty() && inputs_total < outputs_total + tx.fee) {
            error = "block tx value mismatch";
            return false;
        }

        const auto txid = hash_transaction(tx);
        for (std::uint32_t i = 0; i < tx.outputs.size(); ++i) {
            const auto key = outpoint_key(txid, i);
            snapshot_utxo[key] = UTXO{tx.outputs[i].recipient, tx.outputs[i].amount, false};
            if (snapshot_index[tx.outputs[i].recipient] >
                (std::numeric_limits<std::uint64_t>::max() - tx.outputs[i].amount)) {
                error = "balance index overflow";
                return false;
            }
            snapshot_index[tx.outputs[i].recipient] += tx.outputs[i].amount;
        }
    }

    std::vector<PqVerifyItem> jobs;
    jobs.reserve(candidate.transactions.size() > 0 ? candidate.transactions.size() - 1 : 0);
    for (std::size_t i = 1; i < candidate.transactions.size(); ++i) {
        const auto& tx = candidate.transactions[i];
        if (tx.signer.empty() || tx.signer_pubkey.empty() || tx.signature.rfind("pq=", 0) != 0) {
            error = "non-coinbase tx missing PQ signature";
            return false;
        }
        if (!signer_binds_pubkey(tx, error)) {
            return false;
        }
        Transaction unsigned_tx = tx;
        unsigned_tx.signature.clear();
        jobs.push_back(PqVerifyItem{tx.signer_pubkey, hash_transaction(unsigned_tx), tx.signature});
    }
    if (!jobs.empty()) {
        std::size_t accepted = 0;
        std::uint64_t verify_ms = 0;
        std::string verr;
        const auto hw = std::thread::hardware_concurrency();
        const std::size_t threads = hw > 0 ? static_cast<std::size_t>(hw) : 1;
        if (!pq_verify_messages_parallel(jobs, threads, accepted, verify_ms, verr)) {
            error = verr.empty() ? "batch pq verify failed" : verr;
            return false;
        }
        last_batch_verify_ms_ = verify_ms;
        last_batch_verify_count_ = accepted;
    }

    return true;
}

std::uint64_t Chain::last_batch_verify_ms() const {
    return last_batch_verify_ms_;
}

std::size_t Chain::last_batch_verify_count() const {
    return last_batch_verify_count_;
}

double Chain::last_batch_verify_per_sec() const {
    if (last_batch_verify_count_ == 0) {
        return 0.0;
    }
    const double sec = static_cast<double>(last_batch_verify_ms_ > 0 ? last_batch_verify_ms_ : 1) / 1000.0;
    return static_cast<double>(last_batch_verify_count_) / sec;
}

bool Chain::add_block(const Block& block, std::string& error) {
    if (!validate_block_header(block, error)) {
        return false;
    }
    if (!validate_block_transactions(block, error)) {
        return false;
    }

    auto utxo_snapshot = utxo_set_;
    auto index_snapshot = address_index_;
    auto seen_snapshot = seen_transactions_;
    auto nonce_snapshot = signer_last_nonce_;
    const auto emitted_snapshot = total_emitted_;
    const auto fees_snapshot = total_fees_last_block_;
    const auto blocks_snapshot_size = blocks_.size();
    const auto work_snapshot = cumulative_work_;
    const auto diff_snapshot = difficulty_target_;

    for (const auto& tx : block.transactions) {
        const auto txid = hash_transaction(tx);
        if (!apply_transaction(tx, txid, error, false)) {
            utxo_set_ = std::move(utxo_snapshot);
            address_index_ = std::move(index_snapshot);
            seen_transactions_ = std::move(seen_snapshot);
            signer_last_nonce_ = std::move(nonce_snapshot);
            total_emitted_ = emitted_snapshot;
            total_fees_last_block_ = fees_snapshot;
            if (blocks_.size() > blocks_snapshot_size) {
                blocks_.resize(blocks_snapshot_size);
            }
            cumulative_work_ = work_snapshot;
            difficulty_target_ = diff_snapshot;
            return false;
        }
    }

    std::uint64_t fees = 0;
    for (std::size_t i = 1; i < block.transactions.size(); ++i) {
        if (fees > (std::numeric_limits<std::uint64_t>::max() - block.transactions[i].fee)) {
            error = "fees overflow";
            return false;
        }
        fees += block.transactions[i].fee;
    }
    total_fees_last_block_ = fees;

    if (!block.transactions.empty()) {
        std::uint64_t coinbase_total = 0;
        for (const auto& out : block.transactions.front().outputs) {
            if (coinbase_total > (std::numeric_limits<std::uint64_t>::max() - out.amount)) {
                error = "coinbase overflow";
                return false;
            }
            coinbase_total += out.amount;
        }
        const auto minted = (coinbase_total > fees) ? (coinbase_total - fees) : 0ULL;
        if (minted > (cfg_.max_supply - total_emitted_)) {
            error = "max supply exceeded";
            return false;
        }
        total_emitted_ += minted;
    }

    blocks_.push_back(block);
    cumulative_work_ += work_for_target(block.header.difficulty_target);
    difficulty_target_ = compute_next_difficulty_target();
    notify_commit();
    return true;
}

bool Chain::replace_with_chain(const std::vector<Block>& candidate,
                               std::string& error) {
    if (candidate.empty()) {
        error = "candidate chain empty";
        return false;
    }

    const auto genesis_hash = hash_block_header(blocks_.front().header);
    const auto candidate_genesis_hash = hash_block_header(candidate.front().header);
    if (candidate_genesis_hash != genesis_hash) {
        error = "genesis mismatch";
        return false;
    }

    auto old_blocks = blocks_;
    auto old_utxo = utxo_set_;
    auto old_index = address_index_;
    auto old_seen = seen_transactions_;
    auto old_nonce = signer_last_nonce_;
    auto old_difficulty = difficulty_target_;
    auto old_emitted = total_emitted_;
    auto old_fees = total_fees_last_block_;
    auto old_work = cumulative_work_;

    bool accepted = false;
    {
        ++suppress_commit_;
        reset();
        std::uint64_t candidate_work = cumulative_work_;

        for (std::size_t i = 1; i < candidate.size(); ++i) {
            std::string add_err;
            if (!add_block(candidate[i], add_err)) {
                blocks_ = std::move(old_blocks);
                utxo_set_ = std::move(old_utxo);
                address_index_ = std::move(old_index);
                seen_transactions_ = std::move(old_seen);
                signer_last_nonce_ = std::move(old_nonce);
                difficulty_target_ = old_difficulty;
                total_emitted_ = old_emitted;
                total_fees_last_block_ = old_fees;
                cumulative_work_ = old_work;
                error = "invalid candidate at height " + std::to_string(i) + ": " + add_err;
                --suppress_commit_;
                return false;
            }
            candidate_work = cumulative_work_;
        }

        if (candidate_work <= old_work) {
            blocks_ = std::move(old_blocks);
            utxo_set_ = std::move(old_utxo);
            address_index_ = std::move(old_index);
            seen_transactions_ = std::move(old_seen);
            signer_last_nonce_ = std::move(old_nonce);
            difficulty_target_ = old_difficulty;
            total_emitted_ = old_emitted;
            total_fees_last_block_ = old_fees;
            cumulative_work_ = old_work;
            error = "candidate work not higher";
            --suppress_commit_;
            return false;
        }
        accepted = true;
        --suppress_commit_;
    }

    if (accepted) {
        notify_commit();
    }
    return true;
}

} // namespace addition
