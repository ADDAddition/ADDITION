#include "addition/block.hpp"

#include "addition/crypto.hpp"
#include "addition/wire_v3.hpp"

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
    if (!tx.shielded.empty()) {
        oss << "|sh=" << strong_hash(tx.shielded);
    }
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

std::size_t tx_weight(const Transaction& tx) {
    return 4 + wire::encode_tx_v3(tx).size();
}

std::size_t block_weight(const Block& block) {
    // BLV3 magic + height + tagged prev hash + ts/nonce/target + tagged merkle + count + checksum.
    std::string hdr;
    wire::write_tagged(hdr, block.header.previous_hash);
    wire::write_tagged(hdr, block.header.merkle_root);
    std::size_t w = 4 + 8 + 24 + hdr.size() + 4 + 4 + 4;  // + ext count
    for (const auto& tx : block.transactions) {
        w += tx_weight(tx);
    }
    return w;
}

std::size_t tx_wire_bytes(const Transaction& tx) {
    return (tx_weight(tx) * 4 + 2) / 3 + 1;  // base64 framing
}

} // namespace addition
