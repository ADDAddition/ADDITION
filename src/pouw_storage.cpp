#include "addition/pouw_storage.hpp"

#include "addition/crypto.hpp"

#include <cctype>
#include <limits>
#include <sstream>

namespace addition {

namespace {

bool is_canonical_hash_hex(const std::string& s) {
    if (s.size() != 64) {
        return false;
    }
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool PoUWStorageEngine::create_deal(const std::string& client_addr,
                                    const std::string& content_root,
                                    std::uint64_t chunk_count,
                                    std::uint64_t replication_factor,
                                    std::uint64_t start_height,
                                    std::uint64_t end_height,
                                    std::uint64_t price_per_epoch,
                                    std::string& deal_id,
                                    std::string& error) {
    deal_id.clear();
    if (client_addr.empty() || content_root.empty() || chunk_count == 0 || replication_factor == 0) {
        error = "invalid storage deal params";
        return false;
    }
    if (end_height <= start_height) {
        error = "end_height must be > start_height";
        return false;
    }

    const auto seed = client_addr + "|" + content_root + "|" + std::to_string(chunk_count) + "|" + std::to_string(start_height) + "|" + std::to_string(end_height) + "|" + std::to_string(deals_.size());
    deal_id = to_hex(sha3_512_bytes("storage_deal|" + seed));
    if (deals_.count(deal_id) > 0) {
        error = "duplicate storage deal id";
        return false;
    }

    Deal d{};
    d.deal_id = deal_id;
    d.client_addr = client_addr;
    d.content_root = content_root;
    d.chunk_count = chunk_count;
    d.replication_factor = replication_factor;
    d.start_height = start_height;
    d.end_height = end_height;
    d.price_per_epoch = price_per_epoch;
    d.status = "active";
    deals_[deal_id] = std::move(d);
    return true;
}

bool PoUWStorageEngine::register_commitment(const std::string& deal_id,
                                            const std::string& worker_addr,
                                            const std::string& sealed_commitment,
                                            std::uint64_t collateral,
                                            std::string& error) {
    auto it = deals_.find(deal_id);
    if (it == deals_.end()) {
        error = "deal not found";
        return false;
    }
    if (worker_addr.empty() || sealed_commitment.empty() || collateral == 0) {
        error = "invalid commitment params";
        return false;
    }

    auto& vec = commitments_by_deal_[deal_id];
    for (const auto& c : vec) {
        if (c.worker_addr == worker_addr) {
            error = "worker already committed";
            return false;
        }
    }

    vec.push_back(Commitment{worker_addr, sealed_commitment, collateral, 0, 0});
    return true;
}

bool PoUWStorageEngine::issue_challenge(const std::string& deal_id,
                                        const std::string& worker_addr,
                                        std::uint64_t height,
                                        std::string& challenge_id,
                                        std::string& error) {
    challenge_id.clear();
    auto it = deals_.find(deal_id);
    if (it == deals_.end()) {
        error = "deal not found";
        return false;
    }

    bool committed = false;
    auto cit = commitments_by_deal_.find(deal_id);
    if (cit != commitments_by_deal_.end()) {
        for (const auto& c : cit->second) {
            if (c.worker_addr == worker_addr) {
                committed = true;
                break;
            }
        }
    }
    if (!committed) {
        error = "worker not committed for deal";
        return false;
    }

    const auto seed_hex = to_hex(sha3_512_bytes("storage_challenge|" + deal_id + "|" + worker_addr + "|" + std::to_string(height) + "|" + std::to_string(challenges_.size())));
    const std::uint64_t random_seed = static_cast<std::uint64_t>(std::stoull(seed_hex.substr(0, 16), nullptr, 16));
    const std::uint64_t chunk_index = (it->second.chunk_count > 0) ? (random_seed % it->second.chunk_count) : 0;

    challenge_id = to_hex(sha3_512_bytes("storage_challenge_id|" + seed_hex));
    if (challenges_.count(challenge_id) > 0) {
        error = "duplicate challenge id";
        return false;
    }

    Challenge ch{};
    ch.challenge_id = challenge_id;
    ch.deal_id = deal_id;
    ch.worker_addr = worker_addr;
    ch.random_seed = random_seed;
    ch.chunk_index = chunk_index;
    ch.due_height = height + 64;
    ch.verdict = "pending";
    challenges_[challenge_id] = std::move(ch);
    return true;
}

bool PoUWStorageEngine::submit_proof(const std::string& challenge_id,
                                     const std::string& worker_addr,
                                     const std::string& proof_blob_hash,
                                     std::string& verdict,
                                     std::string& error) {
    verdict.clear();
    auto it = challenges_.find(challenge_id);
    if (it == challenges_.end()) {
        error = "challenge not found";
        return false;
    }
    auto& ch = it->second;
    if (ch.worker_addr != worker_addr) {
        error = "worker mismatch";
        return false;
    }
    if (ch.verdict != "pending") {
        error = "challenge already finalized";
        return false;
    }
    if (proof_blob_hash.empty()) {
        error = "proof hash empty";
        return false;
    }

    // Deterministic on-chain verifier rule:
    // - proof blob hash must be canonical 32-byte hex (64 hex chars)
    // - challenge binding: first nibble parity must match random_seed parity
    bool pass = false;
    if (is_canonical_hash_hex(proof_blob_hash)) {
        const char first = proof_blob_hash.front();
        const unsigned first_nibble = (first >= '0' && first <= '9')
                                          ? static_cast<unsigned>(first - '0')
                                          : static_cast<unsigned>(10 + (std::tolower(static_cast<unsigned char>(first)) - 'a'));
        pass = ((first_nibble & 1U) == static_cast<unsigned>(ch.random_seed & 1ULL));
    }
    ch.verdict = pass ? "pass" : "fail";
    verdict = ch.verdict;

    auto cit = commitments_by_deal_.find(ch.deal_id);
    if (cit != commitments_by_deal_.end()) {
        for (auto& c : cit->second) {
            if (c.worker_addr == worker_addr) {
                if (pass) {
                    ++c.proofs_pass;
                } else {
                    ++c.proofs_fail;
                }
                break;
            }
        }
    }

    return true;
}

bool PoUWStorageEngine::deal_status(const std::string& deal_id, std::string& out, std::string& error) const {
    out.clear();
    auto it = deals_.find(deal_id);
    if (it == deals_.end()) {
        error = "deal not found";
        return false;
    }

    std::size_t commitments = 0;
    auto cit = commitments_by_deal_.find(deal_id);
    if (cit != commitments_by_deal_.end()) {
        commitments = cit->second.size();
    }

    std::size_t pending = 0;
    std::size_t pass = 0;
    std::size_t fail = 0;
    for (const auto& [_, ch] : challenges_) {
        if (ch.deal_id != deal_id) {
            continue;
        }
        if (ch.verdict == "pending") ++pending;
        else if (ch.verdict == "pass") ++pass;
        else if (ch.verdict == "fail") ++fail;
    }

    std::ostringstream oss;
    oss << "deal_id=" << it->second.deal_id
        << " status=" << it->second.status
        << " chunk_count=" << it->second.chunk_count
        << " replication_factor=" << it->second.replication_factor
        << " commitments=" << commitments
        << " challenges_pending=" << pending
        << " challenges_pass=" << pass
        << " challenges_fail=" << fail;
    out = oss.str();
    return true;
}

bool PoUWStorageEngine::worker_status(const std::string& worker_addr, std::string& out, std::string& error) const {
    out.clear();
    if (worker_addr.empty()) {
        error = "worker empty";
        return false;
    }

    std::uint64_t collateral_total = 0;
    std::uint64_t proofs_pass = 0;
    std::uint64_t proofs_fail = 0;
    std::size_t deals = 0;

    for (const auto& [_, vec] : commitments_by_deal_) {
        for (const auto& c : vec) {
            if (c.worker_addr != worker_addr) {
                continue;
            }
            ++deals;
            if (collateral_total <= (std::numeric_limits<std::uint64_t>::max() - c.collateral)) {
                collateral_total += c.collateral;
            }
            if (proofs_pass <= (std::numeric_limits<std::uint64_t>::max() - c.proofs_pass)) {
                proofs_pass += c.proofs_pass;
            }
            if (proofs_fail <= (std::numeric_limits<std::uint64_t>::max() - c.proofs_fail)) {
                proofs_fail += c.proofs_fail;
            }
        }
    }

    std::ostringstream oss;
    oss << "worker=" << worker_addr
        << " deals=" << deals
        << " collateral_total=" << collateral_total
        << " proofs_pass=" << proofs_pass
        << " proofs_fail=" << proofs_fail;
    out = oss.str();
    return true;
}

std::string PoUWStorageEngine::dump_state() const {
    std::ostringstream oss;
    for (const auto& [id, d] : deals_) {
        oss << "D|" << d.deal_id << '|' << d.client_addr << '|' << d.content_root << '|' << d.chunk_count << '|' << d.replication_factor
            << '|' << d.start_height << '|' << d.end_height << '|' << d.price_per_epoch << '|' << d.status << '\n';
    }
    for (const auto& [deal_id, vec] : commitments_by_deal_) {
        for (const auto& c : vec) {
            oss << "C|" << deal_id << '|' << c.worker_addr << '|' << c.sealed_commitment << '|' << c.collateral << '|' << c.proofs_pass << '|' << c.proofs_fail << '\n';
        }
    }
    for (const auto& [id, ch] : challenges_) {
        oss << "H|" << ch.challenge_id << '|' << ch.deal_id << '|' << ch.worker_addr << '|' << ch.random_seed << '|' << ch.chunk_index << '|' << ch.due_height << '|' << ch.verdict << '\n';
    }
    return oss.str();
}

bool PoUWStorageEngine::load_state(const std::string& state, std::string& error) {
    deals_.clear();
    commitments_by_deal_.clear();
    challenges_.clear();

    std::istringstream iss(state);
    for (std::string line; std::getline(iss, line);) {
        if (line.empty() || line.size() < 2 || line[1] != '|') {
            continue;
        }
        const char tag = line[0];
        std::istringstream ls(line.substr(2));
        if (tag == 'D') {
            Deal d{};
            std::string chunk_count, replication_factor, start_height, end_height, price_per_epoch;
            std::getline(ls, d.deal_id, '|');
            std::getline(ls, d.client_addr, '|');
            std::getline(ls, d.content_root, '|');
            std::getline(ls, chunk_count, '|');
            std::getline(ls, replication_factor, '|');
            std::getline(ls, start_height, '|');
            std::getline(ls, end_height, '|');
            std::getline(ls, price_per_epoch, '|');
            std::getline(ls, d.status);
            d.chunk_count = static_cast<std::uint64_t>(std::stoull(chunk_count));
            d.replication_factor = static_cast<std::uint64_t>(std::stoull(replication_factor));
            d.start_height = static_cast<std::uint64_t>(std::stoull(start_height));
            d.end_height = static_cast<std::uint64_t>(std::stoull(end_height));
            d.price_per_epoch = static_cast<std::uint64_t>(std::stoull(price_per_epoch));
            deals_[d.deal_id] = std::move(d);
        } else if (tag == 'C') {
            std::string deal_id;
            Commitment c{};
            std::string collateral, pass, fail;
            std::getline(ls, deal_id, '|');
            std::getline(ls, c.worker_addr, '|');
            std::getline(ls, c.sealed_commitment, '|');
            std::getline(ls, collateral, '|');
            std::getline(ls, pass, '|');
            std::getline(ls, fail);
            c.collateral = static_cast<std::uint64_t>(std::stoull(collateral));
            c.proofs_pass = static_cast<std::uint64_t>(std::stoull(pass));
            c.proofs_fail = static_cast<std::uint64_t>(std::stoull(fail));
            commitments_by_deal_[deal_id].push_back(std::move(c));
        } else if (tag == 'H') {
            Challenge ch{};
            std::string random_seed, chunk_index, due_height;
            std::getline(ls, ch.challenge_id, '|');
            std::getline(ls, ch.deal_id, '|');
            std::getline(ls, ch.worker_addr, '|');
            std::getline(ls, random_seed, '|');
            std::getline(ls, chunk_index, '|');
            std::getline(ls, due_height, '|');
            std::getline(ls, ch.verdict);
            ch.random_seed = static_cast<std::uint64_t>(std::stoull(random_seed));
            ch.chunk_index = static_cast<std::uint64_t>(std::stoull(chunk_index));
            ch.due_height = static_cast<std::uint64_t>(std::stoull(due_height));
            challenges_[ch.challenge_id] = std::move(ch);
        }
    }

    return true;
}

} // namespace addition
