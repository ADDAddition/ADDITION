#include "addition/block.hpp"

#include "addition/crypto.hpp"

#include <sstream>

namespace addition {
namespace {

std::string strong_hash(const std::string& data) { return to_hex(sha3_512_bytes(data)); }

} // namespace

std::string hash_transaction(const Transaction& tx) {
    std::ostringstream oss;
    oss << "in[";
    for (const auto& in : tx.inputs) {
        oss << in.previous_txid << ':' << in.output_index << ';';
    }
    oss << "]out[";
    for (const auto& out : tx.outputs) {
        oss << out.recipient << ':' << out.amount << ';';
    }
    oss << "]signer=" << tx.signer << "|pub=" << tx.signer_pubkey << "|fee=" << tx.fee << "|nonce=" << tx.nonce;
    return strong_hash(oss.str());
}

std::string compute_merkle_root(const std::vector<Transaction>& txs) {
    if (txs.empty()) {
        return strong_hash("empty");
    }

    std::vector<std::string> layer;
    layer.reserve(txs.size());
    for (const auto& tx : txs) {
        layer.push_back(hash_transaction(tx));
    }

    while (layer.size() > 1) {
        std::vector<std::string> next;
        next.reserve((layer.size() + 1) / 2);
        for (std::size_t i = 0; i < layer.size(); i += 2) {
            const auto& left = layer[i];
            const auto& right = (i + 1 < layer.size()) ? layer[i + 1] : layer[i];
            next.push_back(strong_hash(left + right));
        }
        layer = std::move(next);
    }

    return layer.front();
}

std::string hash_block_header(const BlockHeader& header) {
    std::ostringstream oss;
    oss << header.height << '|'
        << header.previous_hash << '|'
        << header.timestamp << '|'
        << header.nonce << '|'
        << header.difficulty_target << '|'
        << header.merkle_root;
    return strong_hash(oss.str());
}

std::string hash_stake_vote(const StakeVote& v) {
    std::ostringstream oss;
    oss << "vote|" << v.ticket_id << '|' << v.voted_height << '|' << v.voted_hash << '|'
        << (v.approve ? 1 : 0) << '|' << v.signature;
    return strong_hash(oss.str());
}

std::string compute_block_commitment(const Block& block) {
    const auto tx_root = compute_merkle_root(block.transactions);
    if (block.votes.empty() && block.slashes.empty()) {
        return tx_root;
    }
    std::ostringstream oss;
    oss << "blockcommit_v1|" << tx_root << "|votes[";
    for (const auto& v : block.votes) {
        oss << hash_stake_vote(v) << ';';
    }
    oss << "]slashes[";
    for (const auto& s : block.slashes) {
        oss << hash_stake_vote(s.a) << ',' << hash_stake_vote(s.b) << ';';
    }
    oss << ']';
    return strong_hash(oss.str());
}

std::size_t stake_vote_weight(const StakeVote& v) {
    const auto bin = [](const std::string& h) { return 4 + (h.size() + 1) / 2; };
    // ticket "txid:idx" (hex txid + 4-byte index), height, hash, approve, signature.
    const auto colon = v.ticket_id.find(':');
    const std::size_t ticket = colon == std::string::npos ? 4 + v.ticket_id.size() : bin(v.ticket_id.substr(0, colon)) + 4;
    return ticket + 8 + bin(v.voted_hash) + 1 + bin(v.signature);
}

std::size_t stake_extension_weight(const Block& block) {
    if (block.votes.empty() && block.slashes.empty()) return 0;
    std::size_t w = 8;  // vote count + slash count
    for (const auto& v : block.votes) w += stake_vote_weight(v);
    for (const auto& s : block.slashes) w += stake_vote_weight(s.a) + stake_vote_weight(s.b);
    return w;
}

} // namespace addition
