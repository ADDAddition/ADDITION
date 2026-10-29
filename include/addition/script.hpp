#pragma once
// ADDITION spend scripts v1 ("sh1" outputs) — EXPERIMENTAL, NOT AUDITED.
//
// A tiny, non-Turing-complete policy language (miniscript-like) used for
// payment channels. Consensus "opcodes":
//   pk(<address>)   ML-DSA-87 signature by the key bound to <address> over the sighash
//   sha3(<hex128>)  reveal x with SHA3-512(x) == hex (x <= 64 bytes)      (HTLC / revocation)
//   after(<n>)      spend height >= n                                     (CLTV-equivalent)
//   older(<n>)      spend height - funding-output height >= n            (CSV-equivalent)
//   and(a,b)  or(a,b)
// An output pays to "sh1" + SHA3-512(script text) (P2SH-like). The spending input
// carries a witness = script + signatures + preimages. Witnesses are excluded from
// the txid (segwit-style), so pre-signed child transactions cannot be malleated;
// block merkle roots commit to them.
#include "addition/block.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace addition::script {

constexpr std::size_t kMaxScriptBytes = 4096;
constexpr std::size_t kMaxNodes = 64;
constexpr std::size_t kMaxDepth = 16;
constexpr std::size_t kMaxSigs = 4;
constexpr std::size_t kMaxPreimages = 4;
constexpr std::size_t kMaxPreimageBytes = 64;

struct Node {
    enum class Kind { Pk, Sha3, After, Older, And, Or } kind{Kind::Pk};
    std::string arg;          // address or hash hex
    std::uint64_t n{0};
    std::unique_ptr<Node> a, b;
};

bool parse(const std::string& text, Node& out, std::string& error);
std::string script_address(const std::string& script_text);   // "sh1" + sha3-512 hex
bool is_script_address(const std::string& addr);
std::string hash_preimage_hex(const std::string& preimage_hex); // SHA3-512 of bytes

struct Witness {
    std::string script;
    std::vector<std::pair<std::string, std::string>> sigs;  // (pubkey hex, signature)
    std::vector<std::string> preimages;                     // hex
};
std::string encode_witness(const Witness& w);
bool decode_witness(const std::string& s, Witness& out, std::string& error);

// Message every script signature commits to (all inputs/outputs; SIGHASH_ALL).
std::string sighash(const Transaction& tx);

struct SpendContext {
    std::string sighash;
    std::uint64_t spend_height{0};
    std::uint64_t utxo_height{0};
};
bool verify_input(const std::string& owner, const std::string& witness, const SpendContext& ctx, std::string& error);

// Convenience helpers for wallets.
std::string sign(const std::string& private_key_hex, const Transaction& tx);

} // namespace addition::script
