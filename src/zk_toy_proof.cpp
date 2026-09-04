#include "addition/zk_toy_proof.hpp"

#include "addition/crypto.hpp"

#include <openssl/bn.h>

#include <memory>
#include <sstream>

namespace addition {
namespace {

struct BnDeleter {
    void operator()(BIGNUM* p) const { BN_free(p); }
};
struct BnCtxDeleter {
    void operator()(BN_CTX* p) const { BN_CTX_free(p); }
};

using BnPtr = std::unique_ptr<BIGNUM, BnDeleter>;
using BnCtxPtr = std::unique_ptr<BN_CTX, BnCtxDeleter>;

BnPtr bn_from_hex(const char* hex) {
    BIGNUM* raw = nullptr;
    if (BN_hex2bn(&raw, hex) == 0 || raw == nullptr) {
        return BnPtr{};
    }
    return BnPtr{raw};
}

BnPtr bn_new() {
    return BnPtr{BN_new()};
}

std::string bn_to_hex_lower(const BIGNUM* bn) {
    char* tmp = BN_bn2hex(bn);
    if (tmp == nullptr) {
        return {};
    }
    std::string out(tmp);
    OPENSSL_free(tmp);
    for (char& c : out) {
        if (c >= 'A' && c <= 'F') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    // Strip leading zeros but keep at least one digit.
    std::size_t i = 0;
    while (i + 1 < out.size() && out[i] == '0') {
        ++i;
    }
    return out.substr(i);
}

bool parse_hex_bn(const std::string& hex, BnPtr& out, std::string& error) {
    if (hex.empty()) {
        error = "zk_toy_schnorr: empty hex";
        return false;
    }
    for (char c : hex) {
        const auto u = static_cast<unsigned char>(c);
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F'))) {
            error = "zk_toy_schnorr: invalid hex";
            return false;
        }
    }
    BIGNUM* raw = nullptr;
    if (BN_hex2bn(&raw, hex.c_str()) == 0 || raw == nullptr) {
        error = "zk_toy_schnorr: BN_hex2bn failed";
        return false;
    }
    out.reset(raw);
    return true;
}

// Challenge = SHA3-512(domain || p || g || Y || R) interpreted as integer mod q.
bool challenge_mod_q(const BIGNUM* p,
                     const BIGNUM* g,
                     const BIGNUM* Y,
                     const BIGNUM* R,
                     const BIGNUM* q,
                     BN_CTX* ctx,
                     BnPtr& c_out,
                     std::string& error) {
    std::ostringstream oss;
    oss << kZkToyProofDomain << '|'
        << bn_to_hex_lower(p) << '|'
        << bn_to_hex_lower(g) << '|'
        << bn_to_hex_lower(Y) << '|'
        << bn_to_hex_lower(R);
    const Hash512 digest = sha3_512_bytes(oss.str());
    BnPtr c = bn_new();
    if (!c) {
        error = "zk_toy_schnorr: BN_new failed";
        return false;
    }
    if (BN_bin2bn(digest.data(), static_cast<int>(digest.size()), c.get()) == nullptr) {
        error = "zk_toy_schnorr: BN_bin2bn challenge failed";
        return false;
    }
    if (BN_mod(c.get(), c.get(), q, ctx) != 1) {
        error = "zk_toy_schnorr: challenge mod q failed";
        return false;
    }
    c_out = std::move(c);
    return true;
}

bool load_group(BnPtr& p, BnPtr& q, BnPtr& g, std::string& error) {
    p = bn_from_hex(zk_toy_schnorr_p_hex());
    q = bn_from_hex(zk_toy_schnorr_q_hex());
    g = bn_from_hex(zk_toy_schnorr_g_hex());
    if (!p || !q || !g) {
        error = "zk_toy_schnorr: failed to load group params";
        return false;
    }
    return true;
}

} // namespace

const char* zk_toy_schnorr_p_hex() {
    return "c8f270e449e95baaf4eb2d659e5cdea0e0ac4b76526179c583f0b86b38a8d7ff";
}

const char* zk_toy_schnorr_q_hex() {
    return "6479387224f4add57a7596b2cf2e6f50705625bb2930bce2c1f85c359c546bff";
}

const char* zk_toy_schnorr_g_hex() {
    return "b0712cced2a4da290f7de783143b8cec14a43da04421f94588f4b98ab7906f2d";
}

bool zk_toy_schnorr_keygen(ZkToySchnorrPublic& pub_out,
                           ZkToySchnorrWitness& wit_out,
                           std::string& error) {
    pub_out = {};
    wit_out = {};
    BnPtr p;
    BnPtr q;
    BnPtr g;
    if (!load_group(p, q, g, error)) {
        return false;
    }
    BnCtxPtr ctx{BN_CTX_new()};
    BnPtr x = bn_new();
    BnPtr Y = bn_new();
    if (!ctx || !x || !Y) {
        error = "zk_toy_schnorr: alloc failed";
        return false;
    }
    // x uniform in [1, q-1]
    do {
        if (BN_rand_range(x.get(), q.get()) != 1) {
            error = "zk_toy_schnorr: BN_rand_range x failed";
            return false;
        }
    } while (BN_is_zero(x.get()));

    if (BN_mod_exp(Y.get(), g.get(), x.get(), p.get(), ctx.get()) != 1) {
        error = "zk_toy_schnorr: Y = g^x failed";
        return false;
    }
    wit_out.x_hex = bn_to_hex_lower(x.get());
    pub_out.Y_hex = bn_to_hex_lower(Y.get());
    error.clear();
    return true;
}

bool zk_toy_schnorr_prove(const ZkToySchnorrPublic& pub,
                          const ZkToySchnorrWitness& wit,
                          ZkToySchnorrProof& proof_out,
                          std::string& error) {
    proof_out = {};
    BnPtr p;
    BnPtr q;
    BnPtr g;
    if (!load_group(p, q, g, error)) {
        return false;
    }
    BnPtr Y;
    BnPtr x;
    if (!parse_hex_bn(pub.Y_hex, Y, error) || !parse_hex_bn(wit.x_hex, x, error)) {
        return false;
    }
    BnCtxPtr ctx{BN_CTX_new()};
    BnPtr k = bn_new();
    BnPtr R = bn_new();
    BnPtr s = bn_new();
    BnPtr tmp = bn_new();
    if (!ctx || !k || !R || !s || !tmp) {
        error = "zk_toy_schnorr: alloc failed";
        return false;
    }
    // Check Y == g^x
    if (BN_mod_exp(tmp.get(), g.get(), x.get(), p.get(), ctx.get()) != 1) {
        error = "zk_toy_schnorr: recompute Y failed";
        return false;
    }
    if (BN_cmp(tmp.get(), Y.get()) != 0) {
        error = "zk_toy_schnorr: witness does not match public Y";
        return false;
    }
    do {
        if (BN_rand_range(k.get(), q.get()) != 1) {
            error = "zk_toy_schnorr: BN_rand_range k failed";
            return false;
        }
    } while (BN_is_zero(k.get()));

    if (BN_mod_exp(R.get(), g.get(), k.get(), p.get(), ctx.get()) != 1) {
        error = "zk_toy_schnorr: R = g^k failed";
        return false;
    }
    BnPtr c;
    if (!challenge_mod_q(p.get(), g.get(), Y.get(), R.get(), q.get(), ctx.get(), c, error)) {
        return false;
    }
    // s = k + c*x mod q
    if (BN_mod_mul(tmp.get(), c.get(), x.get(), q.get(), ctx.get()) != 1) {
        error = "zk_toy_schnorr: c*x failed";
        return false;
    }
    if (BN_mod_add(s.get(), k.get(), tmp.get(), q.get(), ctx.get()) != 1) {
        error = "zk_toy_schnorr: s = k+c*x failed";
        return false;
    }
    proof_out.R_hex = bn_to_hex_lower(R.get());
    proof_out.s_hex = bn_to_hex_lower(s.get());
    error.clear();
    return true;
}

bool zk_toy_schnorr_verify(const ZkToySchnorrPublic& pub,
                           const ZkToySchnorrProof& proof,
                           std::string& error) {
    if (pub.Y_hex.empty() || proof.R_hex.empty() || proof.s_hex.empty()) {
        error = "zk_toy_schnorr: empty public/proof fields";
        return false;
    }
    BnPtr p;
    BnPtr q;
    BnPtr g;
    if (!load_group(p, q, g, error)) {
        return false;
    }
    BnPtr Y;
    BnPtr R;
    BnPtr s;
    if (!parse_hex_bn(pub.Y_hex, Y, error) || !parse_hex_bn(proof.R_hex, R, error) ||
        !parse_hex_bn(proof.s_hex, s, error)) {
        return false;
    }
    BnCtxPtr ctx{BN_CTX_new()};
    BnPtr lhs = bn_new();
    BnPtr rhs = bn_new();
    BnPtr Yc = bn_new();
    if (!ctx || !lhs || !rhs || !Yc) {
        error = "zk_toy_schnorr: alloc failed";
        return false;
    }
    // Reject s >= q or s == 0 (degenerate).
    if (BN_is_zero(s.get()) || BN_cmp(s.get(), q.get()) >= 0) {
        error = "zk_toy_schnorr: s out of range";
        return false;
    }
    if (BN_is_zero(R.get()) || BN_cmp(R.get(), p.get()) >= 0) {
        error = "zk_toy_schnorr: R out of range";
        return false;
    }
    if (BN_is_zero(Y.get()) || BN_cmp(Y.get(), p.get()) >= 0) {
        error = "zk_toy_schnorr: Y out of range";
        return false;
    }
    BnPtr c;
    if (!challenge_mod_q(p.get(), g.get(), Y.get(), R.get(), q.get(), ctx.get(), c, error)) {
        return false;
    }
    // Check g^s == R * Y^c  (mod p)
    if (BN_mod_exp(lhs.get(), g.get(), s.get(), p.get(), ctx.get()) != 1) {
        error = "zk_toy_schnorr: g^s failed";
        return false;
    }
    if (BN_mod_exp(Yc.get(), Y.get(), c.get(), p.get(), ctx.get()) != 1) {
        error = "zk_toy_schnorr: Y^c failed";
        return false;
    }
    if (BN_mod_mul(rhs.get(), R.get(), Yc.get(), p.get(), ctx.get()) != 1) {
        error = "zk_toy_schnorr: R*Y^c failed";
        return false;
    }
    if (BN_cmp(lhs.get(), rhs.get()) != 0) {
        error = "zk_toy_schnorr: verification equation failed";
        return false;
    }
    error.clear();
    return true;
}

} // namespace addition
