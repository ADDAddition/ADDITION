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

std::size_t tx_wire_bytes(const Transaction& tx) {
    std::size_t bin = 4;                                     // TXB2 magic
    bin += 4 + tx.signer.size();
    bin += 4 + tx.signer_pubkey.size();
    bin += 4 + tx.signature.size();
    bin += 8 + 8;                                            // fee, nonce
    bin += 4;
    for (const auto& in : tx.inputs) bin += 4 + in.previous_txid.size() + 4;
    bin += 4;
    for (const auto& out : tx.outputs) bin += 4 + out.recipient.size() + 8;
    bin += 4;                                                // checksum
    return 2 * (4 + bin);                                    // u32 len in block, hex x2
}

} // namespace addition
