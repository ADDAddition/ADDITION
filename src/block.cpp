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
    oss << "]signer=" << tx.signer << "|scheme=" << tx.signer_scheme
        << "|pub=" << tx.signer_pubkey << "|note=" << tx.note
        << "|fee=" << tx.fee << "|nonce=" << tx.nonce;
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

} // namespace addition
