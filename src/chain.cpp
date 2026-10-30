#include "addition/chain.hpp"
#include "addition/script.hpp"

#include "addition/crypto.hpp"
#include "addition/miner.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace addition {
namespace {

bool signer_binds_pubkey(const std::string& signer, const std::string& pubkey_hex, std::string& error) {
    const auto scheme = infer_sig_scheme_from_pubkey_hex(pubkey_hex);
    if (scheme == SigScheme::Unknown || !sig_scheme_allowed_strict(scheme)) {
        error = "unknown scheme rejected in strict mode";
        return false;
    }
    return address_binds_pubkey(signer, scheme, pubkey_hex, error);
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
    pubkey_registry_.clear();
    difficulty_target_ = cfg_.initial_difficulty_target;
    total_emitted_ = 0;
    total_fees_last_block_ = 0;
    cumulative_work_ = 0;
    tickets_.clear();
    total_slashed_ = 0;
    applying_height_ = 0;
    shielded_ = shielded::State{};

    auto g = make_genesis();
    blocks_.push_back(g);
    cumulative_work_ += work_for_target(g.header.difficulty_target);
}

Block Chain::make_genesis() const {
    Block g{};
    g.header.height = 0;
    // Testnet keeps previous_hash=0 so the live ADDITION_TESTNET_V1 genesis is unchanged.
    // Mainnet, regtest, and fast bind the genesis header to network_id so hashes cannot match.
    if (cfg_.network_id == kMainnetNetworkId || cfg_.network_id == kRegtestNetworkId ||
        cfg_.network_id == kFastNetworkId) {
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
    return scheduled_block_subsidy(h, cfg_.block_reward, cfg_.halving_interval, cfg_.subsidy_floor);
}

std::uint64_t Chain::max_mintable_at(std::uint64_t h) const {
    return max_mintable_subsidy(h, total_emitted_, cfg_.max_supply, cfg_.block_reward,
                                cfg_.halving_interval, cfg_.subsidy_floor);
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
    const std::uint64_t next_height = blocks_.empty() ? 0ULL : blocks_.back().header.height + 1;
    if (next_height < cfg_.difficulty_fork_height) {
        return compute_legacy_difficulty_target();
    }
    // LWMA over the last `lwma_window` solvetimes. Genesis is excluded: its
    // timestamp is a fixed constant, not a mined solvetime.
    const std::size_t window = std::max<std::size_t>(2, cfg_.lwma_window);
    if (blocks_.size() < 3) {
        return clamp_difficulty_target(cfg_.initial_difficulty_target);
    }
    const std::size_t first = std::max<std::size_t>(1, blocks_.size() - std::min(blocks_.size(), window + 1));
    std::vector<std::uint64_t> ts;
    std::vector<std::uint64_t> tg;
    ts.reserve(blocks_.size() - first);
    tg.reserve(blocks_.size() - first);
    for (std::size_t i = first; i < blocks_.size(); ++i) {
        ts.push_back(blocks_[i].header.timestamp);
        tg.push_back(blocks_[i].header.difficulty_target);
    }
    return lwma_next_target(ts, tg, cfg_.target_block_time_sec, cfg_.min_difficulty_target,
                            cfg_.max_difficulty_target, difficulty_target_);
}

std::uint64_t Chain::compute_legacy_difficulty_target() const {
    const std::uint64_t legacy_min = cfg_.legacy_min_difficulty_target != 0
                                         ? std::max(cfg_.legacy_min_difficulty_target, cfg_.min_difficulty_target)
                                         : cfg_.min_difficulty_target;
    const auto clamp_legacy = [&](std::uint64_t t) {
        return std::min(std::max(t, legacy_min), cfg_.max_difficulty_target);
    };
    const auto window = static_cast<std::size_t>(cfg_.retarget_window);
    // Need genesis + at least `window` mined blocks. Retarget only on window boundaries
    // so a 2-block CI mine stays at the easy bound.
    if (window == 0 || blocks_.size() < window + 1) {
        return clamp_legacy(difficulty_target_);
    }
    if ((blocks_.back().header.height % static_cast<std::uint64_t>(window)) != 0) {
        return clamp_legacy(difficulty_target_);
    }

    // Skip genesis as the oldest bound. Its frozen timestamp (Nov 2025) vs "now"
    // made the first window look months-slow and ratcheted the target to 2^56-1.
    std::size_t oldest_index = blocks_.size() - 1 - window;
    if (oldest_index == 0) {
        oldest_index = 1;
    }
    if (oldest_index >= blocks_.size() - 1) {
        return clamp_legacy(difficulty_target_);
    }

    const auto& newest = blocks_.back();
    const auto& oldest = blocks_[oldest_index];
    const auto observed = (newest.header.timestamp > oldest.header.timestamp)
                              ? (newest.header.timestamp - oldest.header.timestamp)
                              : 1ULL;
    const auto expected = static_cast<std::uint64_t>(window) * cfg_.target_block_time_sec;
    if (expected == 0) {
        return clamp_legacy(difficulty_target_);
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
    return clamp_legacy(next);
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
        if (utxo.spent || utxo.owner != from || utxo.spendable_height > height() + 1) {
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

bool Chain::validate_shielded_part(const Transaction& tx,
                                   shielded::Bundle& bundle,
                                   const std::unordered_set<std::string>& pending_nfs,
                                   bool check_proof,
                                   std::string& error) const {
    if (!cfg_.shielded_enabled) {
        error = "shielded transactions not enabled on this network";
        return false;
    }
    if (!shielded::from_tx_field(tx.shielded, bundle, error)) {
        return false;
    }
    if (tx.inputs.empty()) {
        if (!tx.signer.empty() || !tx.signature.empty() || !tx.signer_pubkey.empty()) {
            error = "fully shielded tx must not carry a transparent signer";
            return false;
        }
        if (bundle.vpub_in != 0) {
            error = "vpub_in requires transparent inputs";
            return false;
        }
        if (bundle.nullifiers.empty()) {
            error = "fully shielded tx must spend at least one note";
            return false;
        }
    }
    if (!shielded_.check(bundle, pending_nfs, error)) {
        return false;
    }
    if (check_proof && !shielded::verify_bundle_proof(tx, bundle, error)) {
        return false;
    }
    return true;
}

bool Chain::validate_transaction(const Transaction& tx, std::string& error, bool check_signature) const {
    const bool is_shielded = !tx.shielded.empty();
    shielded::Bundle bundle;
    if (is_shielded && !validate_shielded_part(tx, bundle, {}, check_signature, error)) {
        return false;
    }
    if (tx.outputs.empty() && !is_shielded && !(tx.inputs.empty() && applying_height_ != 0)) {
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

    if (tx.inputs.empty() && is_shielded) {
        if (cfg_.min_fee > 0 && tx.fee < cfg_.min_fee) {
            error = "fee below network minimum";
            return false;
        }
        if (static_cast<unsigned __int128>(bundle.vpub_out) !=
            static_cast<unsigned __int128>(outputs_total) + tx.fee) {
            error = "shielded value balance mismatch";
            return false;
        }
        return true;
    }

    if (tx.inputs.empty()) {
        if (!tx.signer.empty() || !tx.signature.empty()) {
            error = "coinbase must not be signed";
            return false;
        }
        for (const auto& out : tx.outputs) {
            if (out.recipient == kStakeAddress) {
                error = "coinbase cannot lock stake";
                return false;
            }
        }
        // Inside a block the coinbase total (subsidy + fees) is enforced by
        // validate_block_transactions; standalone it may not exceed the subsidy.
        if (applying_height_ == 0 && outputs_total > cfg_.block_reward + tx.fee) {
            error = "coinbase exceeds allowed emission";
            return false;
        }
        return true;
    }

    if (cfg_.min_fee > 0 && tx.fee < cfg_.min_fee) {
        error = "fee below network minimum";
        return false;
    }

    const bool script_tx = tx.signer.empty();
    if (script_tx) {
        // Script spend: no account signer/nonce; every input is authorized by its witness.
        if (!tx.signature.empty() || !tx.signer_pubkey.empty()) {
            error = "script tx must not carry a signer signature";
            return false;
        }
        if (!validate_script_inputs(tx, error)) {
            return false;
        }
    } else {
        for (const auto& in : tx.inputs) {
            if (!in.witness.empty()) {
                error = "witness on signer-authorized input";
                return false;
            }
        }
    }

    if (!script_tx && !validate_transaction_signature(tx, error, check_signature)) {
        return false;
    }

    if (!script_tx) {
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
        if (it->second.spendable_height > context_height()) {
            error = "input still unbonding (spendable at height " +
                    std::to_string(it->second.spendable_height) + ")";
            return false;
        }
        if (inputs_total > (std::numeric_limits<std::uint64_t>::max() - it->second.amount)) {
            error = "inputs overflow";
            return false;
        }
        inputs_total += it->second.amount;
    }

    {
        bool spends_ticket = false;
        for (const auto& in : tx.inputs) {
            if (tickets_.count(outpoint_key(in.previous_txid, in.output_index)) > 0) {
                spends_ticket = true;
            }
        }
        for (const auto& out : tx.outputs) {
            if (out.recipient != kStakeAddress) {
                continue;
            }
            if (spends_ticket) {
                error = "stake unlock cannot create new stake";
                return false;
            }
            if (!staking_active_at(context_height())) {
                error = "staking not active";
                return false;
            }
            if (out.amount < cfg_.stake_min_amount) {
                error = "stake below minimum (" + std::to_string(cfg_.stake_min_amount) + ")";
                return false;
            }
        }
    }

    if (outputs_total > (std::numeric_limits<std::uint64_t>::max() - tx.fee)) {
        error = "outputs+fee overflow";
        return false;
    }

    if (is_shielded) {
        const auto lhs = static_cast<unsigned __int128>(inputs_total) + bundle.vpub_out;
        const auto rhs = static_cast<unsigned __int128>(outputs_total) + tx.fee + bundle.vpub_in;
        if (lhs != rhs) {
            error = "shielded value balance mismatch";
            return false;
        }
        return true;
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
    if (reward > 0) {
        coinbase.outputs.push_back(TxOutput{reward_address, reward});
    }
    coinbase.nonce = b.header.height;

    b.transactions.push_back(std::move(coinbase));
    for (auto& tx : txs) {
        b.transactions.push_back(std::move(tx));
    }
    b.header.merkle_root = compute_block_commitment(b);
    return b;
}

bool Chain::mine_and_add_block(const std::string& reward_address,
                               std::vector<Transaction> txs,
                               std::size_t threads,
                               std::string& mined_hash,
                               std::string& error,
                               std::atomic<bool>* stop) {
    return mine_and_add_block_with_votes(reward_address, std::move(txs), {}, {}, threads,
                                         mined_hash, error, stop);
}

bool Chain::mine_and_add_block_with_votes(const std::string& reward_address,
                                          std::vector<Transaction> txs,
                                          std::vector<StakeVote> votes,
                                          std::vector<SlashEvidence> slashes,
                                          std::size_t threads,
                                          std::string& mined_hash,
                                          std::string& error,
                                          std::atomic<bool>* stop) {
    const auto h = height() + 1;
    if (!staking_active_at(h)) {
        votes.clear();
        slashes.clear();
    }
    std::uint64_t fees = 0;
    for (const auto& tx : txs) {
        fees += tx.fee;
    }
    // Miners are paid subsidy + fees (fees were previously never collected).
    const auto pot = max_mintable_at(h) + fees;
    std::vector<const StakeVote*> approvals;
    for (const auto& v : votes) {
        if (v.approve) {
            approvals.push_back(&v);
        }
    }
    if (stake_votes_required_for_next_block() && approvals.size() < cfg_.stake_vote_quorum) {
        error = "insufficient stake votes (" + std::to_string(approvals.size()) + "/" +
                std::to_string(cfg_.stake_vote_quorum) + ")";
        return false;
    }
    const auto per_vote = approvals.empty()
                              ? 0ULL
                              : stake_reward_per_vote(pot, cfg_.stake_reward_bps, approvals.size());
    const auto stakers_total = per_vote * approvals.size();
    auto b = make_block_template(reward_address, std::move(txs), pot - stakers_total);
    if (pot - stakers_total == 0) {
        b.transactions.front().outputs.clear();
    }
    if (per_vote > 0) {
        for (const auto* v : approvals) {
            const auto t = tickets_.find(v->ticket_id);
            if (t == tickets_.end()) {
                error = "vote for unknown ticket";
                return false;
            }
            b.transactions.front().outputs.push_back(TxOutput{t->second.owner, per_vote});
        }
    }
    b.votes = std::move(votes);
    b.slashes = std::move(slashes);
    b.header.merkle_root = compute_block_commitment(b);
    return mine_block(std::move(b), threads, mined_hash, error, stop);
}

bool Chain::mine_block(Block b, std::size_t threads, std::string& mined_hash, std::string& error,
                       std::atomic<bool>* stop) {
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

bool Chain::validate_script_inputs(const Transaction& tx, std::string& error) const {
    script::SpendContext ctx;
    ctx.sighash = script::sighash(tx);
    ctx.spend_height = height() + 1;  // tx is valid for inclusion in the next block
    for (std::size_t i = 0; i < tx.inputs.size(); ++i) {
        const auto& in = tx.inputs[i];
        const auto it = utxo_set_.find(outpoint_key(in.previous_txid, in.output_index));
        if (it == utxo_set_.end() || it->second.spent) {
            error = "input utxo not found or spent";
            return false;
        }
        ctx.utxo_height = it->second.height;
        std::string e;
        if (!script::verify_input(it->second.owner, in.witness, ctx, e)) {
            error = "input " + std::to_string(i) + ": " + e;
            return false;
        }
    }
    return true;
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
    if (tx.signature.empty()) {
        error = "missing signature";
        return false;
    }
    // Shielded part (if any) is verified by validate_shielded_part (gated by
    // cfg_.shielded_enabled, fail-closed without the STARK backend); the PQ
    // signature below covers it via hash_transaction's |sh= commitment.

    std::string pubkey;
    if (!resolve_signer_pubkey(tx, nullptr, pubkey, error)) {
        return false;
    }

    // Every input must be controlled by the signer (a stake ticket by its owner).
    bool owns_all_inputs = !tx.inputs.empty();
    for (const auto& in : tx.inputs) {
        const auto key = outpoint_key(in.previous_txid, in.output_index);
        const auto it = utxo_set_.find(key);
        if (it == utxo_set_.end() || it->second.spent) {
            owns_all_inputs = false;
            break;
        }
        if (it->second.owner == kStakeAddress) {
            const auto t = tickets_.find(key);
            if (t == tickets_.end() || t->second.owner != tx.signer) {
                owns_all_inputs = false;
                break;
            }
        } else if (it->second.owner != tx.signer) {
            owns_all_inputs = false;
            break;
        }
    }

    if (!owns_all_inputs) {
        error = "signer does not control inputs";
        return false;
    }

    if (!check_crypto) {
        return true;
    }

    Transaction unsigned_tx = tx;
    unsigned_tx.signature.clear();
    const auto msg = hash_transaction(unsigned_tx);
    if (!verify_message_signature_hybrid(pubkey, msg, tx.signature)) {
        error = "invalid signature";
        return false;
    }
    return true;
}

bool Chain::registered_pubkey(const std::string& address, std::string& pubkey_hex) const {
    const auto it = pubkey_registry_.find(address);
    if (it == pubkey_registry_.end()) {
        return false;
    }
    pubkey_hex = it->second;
    return true;
}

bool Chain::resolve_signer_pubkey(const Transaction& tx,
                                  const std::unordered_map<std::string, std::string>* overlay,
                                  std::string& pubkey_hex,
                                  std::string& error) const {
    if (!tx.signer_pubkey.empty()) {
        pubkey_hex = tx.signer_pubkey;
        return signer_binds_pubkey(tx.signer, pubkey_hex, error);
    }
    if (overlay != nullptr) {
        const auto it = overlay->find(tx.signer);
        if (it != overlay->end()) {
            pubkey_hex = it->second;
            return true;  // bound when registered
        }
    }
    if (registered_pubkey(tx.signer, pubkey_hex)) {
        return true;  // bound when registered
    }
    error = "missing signer public key (not in registry)";
    return false;
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
    bool unlocking = false;

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
        if (tickets_.erase(key) > 0) {
            unlocking = true;
        }
    }

    const auto h = context_height();
    for (std::uint32_t i = 0; i < tx.outputs.size(); ++i) {
        const auto key = outpoint_key(txid, i);
        UTXO out{tx.outputs[i].recipient, tx.outputs[i].amount, false, 0, height() + 1};
        if (unlocking) {
            out.spendable_height = h + cfg_.stake_unbond_delay;
        }
        if (out.owner == kStakeAddress) {
            tickets_[key] = StakeTicket{key, tx.signer, tx.signer_pubkey, out.amount, h};
        }
        utxo_set_[key] = out;
        if (address_index_[out.owner] > (std::numeric_limits<std::uint64_t>::max() - out.amount)) {
            error = "balance index overflow";
            return false;
        }
        address_index_[out.owner] += out.amount;
    }

    if (!tx.inputs.empty() && !tx.signer.empty()) {
        signer_last_nonce_[tx.signer] = tx.nonce;
        // Registry: first confirmed full key wins (validated + bound above).
        if (!tx.signer_pubkey.empty()) {
            pubkey_registry_.emplace(tx.signer, tx.signer_pubkey);
        }
    }
    if (!tx.shielded.empty()) {
        shielded::Bundle bundle;
        if (!shielded::from_tx_field(tx.shielded, bundle, error)) {
            return false;
        }
        shielded_.apply(bundle);
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

    if (candidate.header.merkle_root != compute_block_commitment(candidate)) {
        error = "invalid merkle root";
        return false;
    }

    if (candidate.header.timestamp < tip_block.header.timestamp) {
        error = "timestamp regression";
        return false;
    }
    if (candidate.header.height >= cfg_.difficulty_fork_height) {
        // Future-time limit bounds LWMA timestamp manipulation. Only applied
        // post-fork so legacy history replays unchanged.
        const auto now = static_cast<std::uint64_t>(std::time(nullptr));
        const auto ftl = kFutureTimeLimitBlocks * std::max<std::uint64_t>(1, cfg_.target_block_time_sec);
        if (candidate.header.timestamp > now + ftl) {
            error = "timestamp too far in future";
            return false;
        }
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
    if (block_weight(candidate) > cfg_.max_block_weight) {
        error = "block weight exceeds max_block_weight";
        return false;
    }

    const auto& coinbase = candidate.transactions.front();
    if (!coinbase.shielded.empty()) {
        error = "coinbase must not carry a shielded bundle";
        return false;
    }
    if (!coinbase.inputs.empty()) {
        error = "coinbase must not have inputs";
        return false;
    }
    if (coinbase.outputs.empty()) {
        // Allowed once nothing is mintable (cap reached) or on zero-reward nets.
        if (!cfg_.allow_zero_reward_blocks && max_mintable_at(candidate.header.height) != 0) {
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
        if (candidate.transactions[i].inputs.empty() && candidate.transactions[i].shielded.empty()) {
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
    if (minted > max_mintable_at(candidate.header.height)) {
        error = "max supply exceeded";
        return false;
    }

    auto snapshot_utxo = utxo_set_;
    auto snapshot_index = address_index_;
    // Shielded: stateful checks against pre-block state (anchors must be from
    // earlier blocks), nullifier uniqueness across the whole block, then STARKs.
    std::unordered_set<std::string> block_nfs;
    std::unordered_set<std::string> block_cms;
    std::vector<std::pair<const Transaction*, shielded::Bundle>> shielded_jobs;
    std::vector<std::uint64_t> shielded_vpub_in(candidate.transactions.size(), 0);
    std::vector<std::uint64_t> shielded_vpub_out(candidate.transactions.size(), 0);
    for (std::size_t i = 1; i < candidate.transactions.size(); ++i) {
        const auto& tx = candidate.transactions[i];
        if (tx.shielded.empty()) {
            continue;
        }
        shielded::Bundle bundle;
        if (!validate_shielded_part(tx, bundle, block_nfs, false, error)) {
            return false;
        }
        for (const auto& n : bundle.nullifiers) {
            block_nfs.insert(shielded::hex32(n));
        }
        for (const auto& c : bundle.commitments) {
            if (!block_cms.insert(shielded::hex32(c)).second) {
                error = "shielded: duplicate commitment in block";
                return false;
            }
        }
        shielded_vpub_in[i] = bundle.vpub_in;
        shielded_vpub_out[i] = bundle.vpub_out;
        shielded_jobs.emplace_back(&tx, std::move(bundle));
    }
    for (const auto& job : shielded_jobs) {
        if (!shielded::verify_bundle_proof(*job.first, job.second, error)) {
            return false;
        }
    }

    std::size_t tx_index = 0;
    for (const auto& tx : candidate.transactions) {
        const std::size_t this_index = tx_index++;
        if (tx.outputs.empty() && tx.shielded.empty() && !(&tx == &coinbase && coinbase_total == 0)) {
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
            if (it->second.spendable_height > candidate.header.height) {
                error = "input still unbonding";
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
        if (!tx.shielded.empty()) {
            const auto lhs = static_cast<unsigned __int128>(inputs_total) + shielded_vpub_out[this_index];
            const auto rhs = static_cast<unsigned __int128>(outputs_total) + tx.fee + shielded_vpub_in[this_index];
            if (lhs != rhs) {
                error = "block shielded value mismatch";
                return false;
            }
        } else if (!tx.inputs.empty() && inputs_total < outputs_total + tx.fee) {
            error = "block tx value mismatch";
            return false;
        }

        const auto txid = hash_transaction(tx);
        for (std::uint32_t i = 0; i < tx.outputs.size(); ++i) {
            const auto key = outpoint_key(txid, i);
            snapshot_utxo[key] = UTXO{tx.outputs[i].recipient, tx.outputs[i].amount, false, 0, candidate.header.height};
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
    std::unordered_map<std::string, std::string> registry_overlay;  // same-block registrations
    for (std::size_t i = 1; i < candidate.transactions.size(); ++i) {
        const auto& tx = candidate.transactions[i];
        if (tx.inputs.empty() && !tx.shielded.empty()) {
            continue;  // authorized by the STARK (knowledge of nk), verified above
        }
        if (tx.signer.empty() && tx.signature.empty() && tx.signer_pubkey.empty()) {
            continue;  // script tx: witnesses verified in apply_transaction (always, even w/o check_signature)
        }
        if (tx.signer.empty() || tx.signature.rfind("pq=", 0) != 0) {
            error = "non-coinbase tx missing PQ signature";
            return false;
        }
        std::string pubkey;
        if (!resolve_signer_pubkey(tx, &registry_overlay, pubkey, error)) {
            return false;
        }
        if (!tx.signer_pubkey.empty() && !tx.inputs.empty() && pubkey_registry_.count(tx.signer) == 0) {
            registry_overlay.emplace(tx.signer, tx.signer_pubkey);
        }
        Transaction unsigned_tx = tx;
        unsigned_tx.signature.clear();
        jobs.push_back(PqVerifyItem{std::move(pubkey), hash_transaction(unsigned_tx), tx.signature});
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

    return validate_block_staking(candidate, error);
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
    // Cheap size rule first (before PoW / signature work).
    if (block_weight(block) > cfg_.max_block_weight) {
        error = "block weight exceeds max_block_weight";
        return false;
    }
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
    auto registry_snapshot = pubkey_registry_;
    const auto emitted_snapshot = total_emitted_;
    const auto fees_snapshot = total_fees_last_block_;
    const auto blocks_snapshot_size = blocks_.size();
    const auto work_snapshot = cumulative_work_;
    const auto diff_snapshot = difficulty_target_;
    auto tickets_snapshot = tickets_;
    const auto slashed_snapshot = total_slashed_;

    struct ApplyGuard {
        std::uint64_t& ref;
        ~ApplyGuard() { ref = 0; }
    } guard{applying_height_};
    applying_height_ = block.header.height;
    auto shielded_snapshot = shielded_;

    for (const auto& tx : block.transactions) {
        const auto txid = hash_transaction(tx);
        if (!apply_transaction(tx, txid, error, false)) {
            tickets_ = std::move(tickets_snapshot);
            total_slashed_ = slashed_snapshot;
            utxo_set_ = std::move(utxo_snapshot);
            address_index_ = std::move(index_snapshot);
            seen_transactions_ = std::move(seen_snapshot);
            signer_last_nonce_ = std::move(nonce_snapshot);
            pubkey_registry_ = std::move(registry_snapshot);
            total_emitted_ = emitted_snapshot;
            total_fees_last_block_ = fees_snapshot;
            if (blocks_.size() > blocks_snapshot_size) {
                blocks_.resize(blocks_snapshot_size);
            }
            cumulative_work_ = work_snapshot;
            difficulty_target_ = diff_snapshot;
            shielded_ = std::move(shielded_snapshot);
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

    // Equivocation: burn the ticket (coins destroyed, never re-minted).
    for (const auto& ev : block.slashes) {
        const auto t = tickets_.find(ev.a.ticket_id);
        if (t == tickets_.end()) {
            continue;
        }
        auto u = utxo_set_.find(ev.a.ticket_id);
        if (u != utxo_set_.end() && !u->second.spent) {
            u->second.spent = true;
            auto& bal = address_index_[u->second.owner];
            bal = bal >= u->second.amount ? bal - u->second.amount : 0;
            total_slashed_ += u->second.amount;
        }
        tickets_.erase(t);
    }

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
        if (minted > max_mintable_at(block.header.height)) {
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
    auto old_registry = pubkey_registry_;
    auto old_difficulty = difficulty_target_;
    auto old_emitted = total_emitted_;
    auto old_fees = total_fees_last_block_;
    auto old_work = cumulative_work_;
    auto old_tickets = tickets_;
    auto old_slashed = total_slashed_;
    auto old_shielded = shielded_;

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
                pubkey_registry_ = std::move(old_registry);
                difficulty_target_ = old_difficulty;
                total_emitted_ = old_emitted;
                total_fees_last_block_ = old_fees;
                cumulative_work_ = old_work;
                tickets_ = old_tickets;
                total_slashed_ = old_slashed;
                shielded_ = old_shielded;
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
            pubkey_registry_ = std::move(old_registry);
            difficulty_target_ = old_difficulty;
            total_emitted_ = old_emitted;
            total_fees_last_block_ = old_fees;
            cumulative_work_ = old_work;
            tickets_ = old_tickets;
            total_slashed_ = old_slashed;
            shielded_ = old_shielded;
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

// ---------------------------------------------------------------------------
// Consensus staking
// ---------------------------------------------------------------------------

std::uint64_t Chain::context_height() const {
    return applying_height_ != 0 ? applying_height_ : height() + 1;
}

bool Chain::staking_active_at(std::uint64_t h) const {
    return h >= cfg_.staking_activation_height;
}

std::vector<StakeTicket> Chain::tickets() const {
    std::vector<StakeTicket> out;
    for (const auto& [id, t] : tickets_) {
        out.push_back(t);
    }
    return out;
}

std::vector<StakeTicket> Chain::tickets_of(const std::string& owner) const {
    std::vector<StakeTicket> out;
    for (const auto& [id, t] : tickets_) {
        if (t.owner == owner) {
            out.push_back(t);
        }
    }
    return out;
}

std::optional<StakeTicket> Chain::ticket(const std::string& id) const {
    const auto it = tickets_.find(id);
    if (it == tickets_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::uint64_t Chain::staked_of(const std::string& owner) const {
    std::uint64_t sum = 0;
    for (const auto& [id, t] : tickets_) {
        if (t.owner == owner) {
            sum += t.amount;
        }
    }
    return sum;
}

std::uint64_t Chain::total_staked() const {
    std::uint64_t sum = 0;
    for (const auto& [id, t] : tickets_) {
        sum += t.amount;
    }
    return sum;
}

std::vector<StakeTicket> Chain::eligible_tickets_at(std::uint64_t voted_height) const {
    std::vector<StakeTicket> out;
    for (const auto& [id, t] : tickets_) {
        if (t.lock_height + cfg_.stake_maturity <= voted_height) {
            out.push_back(t);
        }
    }
    return out;
}

std::vector<StakeTicket> Chain::eligible_tickets_for_next_block() const {
    return eligible_tickets_at(height());
}

std::vector<std::string> Chain::selected_tickets_for_next_block() const {
    if (!staking_active_at(height() + 1)) {
        return {};
    }
    return select_tickets(eligible_tickets_for_next_block(), hash_block_header(tip().header),
                          cfg_.stake_votes_per_block);
}

bool Chain::stake_votes_required_for_next_block() const {
    return staking_active_at(height() + 1) &&
           eligible_tickets_for_next_block().size() >= cfg_.stake_min_live_tickets;
}

std::string Chain::stake_vote_message_for(std::uint64_t voted_height,
                                          const std::string& voted_hash,
                                          const std::string& ticket_id,
                                          bool approve) const {
    return stake_vote_message(cfg_.network_id, voted_height, voted_hash, ticket_id, approve);
}

namespace {
bool vote_signature_ok(const StakeTicket& t, const std::string& network_id, const StakeVote& v) {
    if (v.signature.rfind("pq=", 0) != 0) {
        return false;
    }
    const auto msg = stake_vote_message(network_id, v.voted_height, v.voted_hash, v.ticket_id, v.approve);
    return verify_message_signature_hybrid(t.owner_pubkey, msg, v.signature);
}
} // namespace

bool Chain::verify_stake_vote(const StakeVote& v, std::string& error) const {
    if (v.voted_height != height() || v.voted_hash != hash_block_header(tip().header)) {
        error = "vote is not on the current tip";
        return false;
    }
    const auto sel = selected_tickets_for_next_block();
    if (std::find(sel.begin(), sel.end(), v.ticket_id) == sel.end()) {
        error = "ticket not selected for this block";
        return false;
    }
    const auto t = tickets_.find(v.ticket_id);
    if (t == tickets_.end()) {
        error = "unknown ticket";
        return false;
    }
    if (!vote_signature_ok(t->second, cfg_.network_id, v)) {
        error = "invalid vote signature";
        return false;
    }
    return true;
}

bool Chain::verify_vote_signature_only(const StakeVote& v, std::string& error) const {
    const auto t = tickets_.find(v.ticket_id);
    if (t == tickets_.end()) {
        error = "unknown ticket";
        return false;
    }
    if (!vote_signature_ok(t->second, cfg_.network_id, v)) {
        error = "invalid vote signature";
        return false;
    }
    return true;
}

std::vector<StakeVote> Chain::usable_votes_for_next_block(const std::vector<StakeVote>& pool) const {
    std::vector<StakeVote> out;
    std::unordered_set<std::string> used;
    for (const auto& v : pool) {
        if (!v.approve || used.count(v.ticket_id) > 0) {
            continue;
        }
        std::string err;
        if (!verify_stake_vote(v, err)) {
            continue;
        }
        used.insert(v.ticket_id);
        out.push_back(v);
        if (out.size() >= cfg_.stake_votes_per_block) {
            break;
        }
    }
    return out;
}

bool Chain::validate_slash(const SlashEvidence& ev, std::string& error) const {
    const auto& a = ev.a;
    const auto& b = ev.b;
    if (a.ticket_id != b.ticket_id || a.voted_height != b.voted_height) {
        error = "slash: votes are not from the same ticket/height";
        return false;
    }
    if (a.voted_hash == b.voted_hash && a.approve == b.approve) {
        error = "slash: votes do not conflict";
        return false;
    }
    const auto t = tickets_.find(a.ticket_id);
    if (t == tickets_.end()) {
        error = "slash: unknown or spent ticket";
        return false;
    }
    if (!vote_signature_ok(t->second, cfg_.network_id, a) || !vote_signature_ok(t->second, cfg_.network_id, b)) {
        error = "slash: invalid vote signature";
        return false;
    }
    return true;
}

bool Chain::validate_block_staking(const Block& candidate, std::string& error) const {
    const auto h = candidate.header.height;
    if (!staking_active_at(h)) {
        if (!candidate.votes.empty() || !candidate.slashes.empty()) {
            error = "votes/slashes before staking activation";
            return false;
        }
        return true;
    }
    std::unordered_set<std::string> slashed;
    for (const auto& ev : candidate.slashes) {
        if (!validate_slash(ev, error)) {
            return false;
        }
        if (!slashed.insert(ev.a.ticket_id).second) {
            error = "duplicate slash evidence";
            return false;
        }
    }

    // Votes are validated against the parent (current tip) state.
    const auto voted_height = h - 1;
    const auto& voted_hash = candidate.header.previous_hash;
    const auto eligible = eligible_tickets_at(voted_height);
    const auto selected = select_tickets(eligible, voted_hash, cfg_.stake_votes_per_block);
    if (candidate.votes.size() > selected.size()) {
        error = "too many votes";
        return false;
    }
    std::unordered_set<std::string> seen;
    std::vector<std::string> approving_owners;
    for (const auto& v : candidate.votes) {
        if (v.voted_height != voted_height || v.voted_hash != voted_hash) {
            error = "vote not on parent block";
            return false;
        }
        if (std::find(selected.begin(), selected.end(), v.ticket_id) == selected.end()) {
            error = "vote from unselected ticket";
            return false;
        }
        if (!seen.insert(v.ticket_id).second) {
            error = "duplicate vote";
            return false;
        }
        const auto t = tickets_.find(v.ticket_id);
        if (t == tickets_.end() || !vote_signature_ok(t->second, cfg_.network_id, v)) {
            error = "invalid stake vote signature";
            return false;
        }
        if (v.approve) {
            approving_owners.push_back(t->second.owner);
        }
    }
    if (eligible.size() >= cfg_.stake_min_live_tickets && approving_owners.size() < cfg_.stake_vote_quorum) {
        error = "insufficient stake votes";
        return false;
    }

    // Coinbase layout: [miner?, one output per approving vote in vote order].
    if (!approving_owners.empty()) {
        std::uint64_t fees = 0;
        for (std::size_t i = 1; i < candidate.transactions.size(); ++i) {
            fees += candidate.transactions[i].fee;
        }
        const auto pot = max_mintable_at(h) + fees;
        const auto per = stake_reward_per_vote(pot, cfg_.stake_reward_bps, approving_owners.size());
        if (per > 0) {
            const auto& outs = candidate.transactions.front().outputs;
            const auto n = approving_owners.size();
            if (outs.size() < n) {
                error = "coinbase missing staker rewards";
                return false;
            }
            const auto base = outs.size() - n;
            for (std::size_t k = 0; k < n; ++k) {
                if (outs[base + k].recipient != approving_owners[k] || outs[base + k].amount != per) {
                    error = "coinbase staker reward mismatch";
                    return false;
                }
            }
        }
    }
    return true;
}

bool Chain::build_stake_lock(const std::string& from,
                             std::uint64_t amount,
                             std::uint64_t fee,
                             std::uint64_t nonce,
                             Transaction& out_tx,
                             std::string& error) const {
    if (!staking_active_at(height() + 1)) {
        error = "staking not active";
        return false;
    }
    if (amount < cfg_.stake_min_amount) {
        error = "stake below minimum (" + std::to_string(cfg_.stake_min_amount) + ")";
        return false;
    }
    Transaction tx;
    if (!build_transaction(from, kStakeAddress, amount, fee, nonce, tx, error)) {
        return false;
    }
    out_tx = std::move(tx);
    return true;
}

bool Chain::build_stake_unlock(const std::string& ticket_id,
                               std::uint64_t fee,
                               std::uint64_t nonce,
                               Transaction& out_tx,
                               std::string& error) const {
    const auto t = tickets_.find(ticket_id);
    if (t == tickets_.end()) {
        error = "unknown ticket";
        return false;
    }
    if (t->second.amount <= fee) {
        error = "fee exceeds ticket amount";
        return false;
    }
    const auto sep = ticket_id.rfind(':');
    TxInput in{};
    in.previous_txid = ticket_id.substr(0, sep);
    in.output_index = static_cast<std::uint32_t>(std::stoul(ticket_id.substr(sep + 1)));
    out_tx = Transaction{};
    out_tx.inputs.push_back(in);
    out_tx.outputs.push_back(TxOutput{t->second.owner, t->second.amount - fee});
    out_tx.fee = fee;
    out_tx.nonce = nonce;
    return true;
}

std::uint64_t Chain::spendable_height_of(const std::string& outpoint) const {
    const auto it = utxo_set_.find(outpoint);
    return it == utxo_set_.end() ? 0 : it->second.spendable_height;
}

} // namespace addition
