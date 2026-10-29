// EXPERIMENTAL, NOT AUDITED. See include/addition/shielded.hpp for the design.
#include "addition/shielded.hpp"

#include "addition/crypto.hpp"

#include <oqs/oqs.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <chrono>
#include <cstring>

#ifdef ADDITION_HAVE_SHIELDED_STARK
#include "addition_shielded.h"
#endif

namespace addition::shielded {
namespace {

constexpr const char* kKem = OQS_KEM_alg_ml_kem_1024;
constexpr std::size_t kPlainSize = 8 + 32 + 2 + kMemoSize;

void put64(Bytes& o, std::uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
void put32(Bytes& o, std::uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(static_cast<std::uint8_t>(v >> (8 * i))); }
void putb(Bytes& o, const std::uint8_t* p, std::size_t n) { o.insert(o.end(), p, p + n); }
void puth(Bytes& o, const H32& h) { putb(o, h.data(), 32); }
void putv(Bytes& o, const Bytes& v) { put32(o, static_cast<std::uint32_t>(v.size())); putb(o, v.data(), v.size()); }

struct Rd {
    const Bytes& b; std::size_t i{0};
    bool take(std::size_t n, const std::uint8_t*& p) { if (n > b.size() - i) return false; p = b.data() + i; i += n; return true; }
    bool u64(std::uint64_t& v) { const std::uint8_t* p; if (!take(8, p)) return false; v = 0; for (int k = 7; k >= 0; --k) v = (v << 8) | p[k]; return true; }
    bool u32(std::uint32_t& v) { const std::uint8_t* p; if (!take(4, p)) return false; v = 0; for (int k = 3; k >= 0; --k) v = (v << 8) | p[k]; return true; }
    bool h(H32& out) { const std::uint8_t* p; if (!take(32, p)) return false; std::memcpy(out.data(), p, 32); return true; }
    bool vec(Bytes& out, std::size_t max) { std::uint32_t n; const std::uint8_t* p; if (!u32(n) || n > max || !take(n, p)) return false; out.assign(p, p + n); return true; }
};

H32 sha256_raw(const Bytes& data) {
    H32 out{};
    unsigned int len = 0;
    EVP_Digest(data.data(), data.size(), out.data(), &len, EVP_sha256(), nullptr);
    return out;
}

Bytes hb(const H32& h) { return Bytes(h.begin(), h.end()); }

bool aead(bool enc, const H32& key, const std::uint8_t nonce[12], const Bytes& aad, const Bytes& in, Bytes& out) {
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    if (!c) return false;
    bool ok = false;
    int len = 0;
    out.assign(in.size() + 16, 0);
    std::size_t body = enc ? in.size() : (in.size() >= 16 ? in.size() - 16 : 0);
    if (!enc && in.size() < 16) { EVP_CIPHER_CTX_free(c); return false; }
    if ((enc ? EVP_EncryptInit_ex(c, EVP_aes_256_gcm(), nullptr, key.data(), nonce)
             : EVP_DecryptInit_ex(c, EVP_aes_256_gcm(), nullptr, key.data(), nonce)) == 1) {
        int dummy = 0;
        bool step = aad.empty() || (enc ? EVP_EncryptUpdate(c, nullptr, &dummy, aad.data(), (int)aad.size())
                                        : EVP_DecryptUpdate(c, nullptr, &dummy, aad.data(), (int)aad.size())) == 1;
        if (step && enc) {
            if (EVP_EncryptUpdate(c, out.data(), &len, in.data(), (int)body) == 1 &&
                EVP_EncryptFinal_ex(c, out.data() + len, &dummy) == 1 &&
                EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, out.data() + body) == 1) { ok = true; out.resize(body + 16); }
        } else if (step) {
            Bytes tag(in.end() - 16, in.end());
            if (EVP_DecryptUpdate(c, out.data(), &len, in.data(), (int)body) == 1 &&
                EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag.data()) == 1 &&
                EVP_DecryptFinal_ex(c, out.data() + len, &dummy) == 1) { ok = true; out.resize(body); }
        }
    }
    EVP_CIPHER_CTX_free(c);
    return ok;
}

H32 note_key(const Bytes& ss, const Bytes& kem_ct) {
    // KDF: SHA3-512 truncated, domain separated, binds the KEM ciphertext.
    Bytes m{'A', 'D', 'D', '/', 'n', 'o', 't', 'e', 'k', 'e', 'y', '/', 'v', '1'};
    m.insert(m.end(), ss.begin(), ss.end());
    m.insert(m.end(), kem_ct.begin(), kem_ct.end());
    const auto d = sha3_512_bytes(m);
    H32 k{};
    std::memcpy(k.data(), d.data(), 32);
    return k;
}

std::string hexs(const Bytes& b) { return bytes_to_hex(b); }

} // namespace

H32 sha256_dom(const std::string& dom, const std::vector<const Bytes*>& parts) {
    Bytes m(dom.begin(), dom.end());
    for (const auto* p : parts) m.insert(m.end(), p->begin(), p->end());
    return sha256_raw(m);
}
H32 owner_tag(const H32& nk) { auto a = hb(nk); return sha256_dom("ADD/owner/v1", {&a}); }
H32 note_commitment(std::uint64_t value, const H32& owner, const H32& rho, const H32& r) {
    Bytes v; put64(v, value); auto o = hb(owner), p = hb(rho), q = hb(r);
    return sha256_dom("ADD/cm/v1", {&v, &o, &p, &q});
}
H32 nullifier(const H32& nk, const H32& rho) { auto a = hb(nk), b = hb(rho); return sha256_dom("ADD/nf/v1", {&a, &b}); }
H32 merkle_node(const H32& l, const H32& r) { auto a = hb(l), b = hb(r); return sha256_dom("ADD/mk/v1", {&a, &b}); }
H32 output_rho(const H32& bind, const std::vector<H32>& nfs, std::uint32_t j) {
    Bytes m{'A', 'D', 'D', '/', 'r', 'h', 'o', '/', 'v', '1'};
    puth(m, bind);
    for (const auto& n : nfs) puth(m, n);
    put32(m, j);
    return sha256_raw(m);
}
std::string hex32(const H32& h) { return hexs(hb(h)); }

bool proof_system_available() {
#ifdef ADDITION_HAVE_SHIELDED_STARK
    return true;
#else
    return false;
#endif
}
H32 image_id() {
    H32 id{};
#ifdef ADDITION_HAVE_SHIELDED_STARK
    addition_shielded_image_id(id.data());
#endif
    return id;
}

// ---- keys ------------------------------------------------------------------
std::string Address::encode() const { Bytes b = hb(owner); b.insert(b.end(), kem_pk.begin(), kem_pk.end()); return "addz1" + hexs(b); }
bool Address::decode(const std::string& s, Address& out, std::string& error) {
    if (s.rfind("addz1", 0) != 0) { error = "not a shielded address"; return false; }
    std::vector<std::uint8_t> b;
    if (!hex_to_bytes(s.substr(5), b, error)) return false;
    if (b.size() != 32 + OQS_KEM_ml_kem_1024_length_public_key) { error = "bad shielded address length"; return false; }
    std::memcpy(out.owner.data(), b.data(), 32);
    out.kem_pk.assign(b.begin() + 32, b.end());
    return true;
}
Address SpendingKey::address() const { return Address{owner_tag(nk), kem_pk}; }
bool generate_spending_key(SpendingKey& out, std::string& error) {
    OQS_KEM* kem = OQS_KEM_new(kKem);
    if (!kem) { error = "ML-KEM-1024 unavailable in liboqs"; return false; }
    out.kem_pk.assign(kem->length_public_key, 0);
    out.kem_sk.assign(kem->length_secret_key, 0);
    const bool ok = OQS_KEM_keypair(kem, out.kem_pk.data(), out.kem_sk.data()) == OQS_SUCCESS &&
                    RAND_bytes(out.nk.data(), 32) == 1;
    OQS_KEM_free(kem);
    if (!ok) error = "key generation failed";
    return ok;
}

// ---- serialization -----------------------------------------------------------
Bytes serialize(const Bundle& b) {
    Bytes o{'Z', '1'};
    put64(o, b.vpub_in); put64(o, b.vpub_out); puth(o, b.anchor);
    put32(o, (std::uint32_t)b.nullifiers.size()); for (auto& n : b.nullifiers) puth(o, n);
    put32(o, (std::uint32_t)b.commitments.size()); for (auto& c : b.commitments) puth(o, c);
    put32(o, (std::uint32_t)b.enc.size()); for (auto& e : b.enc) { putv(o, e.kem_ct); putv(o, e.aead); }
    putv(o, b.proof);
    return o;
}
bool deserialize(const Bytes& in, Bundle& out, std::string& error) {
    out = Bundle{};
    Rd r{in};
    const std::uint8_t* p;
    std::uint32_t n;
    error = "malformed shielded bundle";
    if (!r.take(2, p) || p[0] != 'Z' || p[1] != '1') return false;
    if (!r.u64(out.vpub_in) || !r.u64(out.vpub_out) || !r.h(out.anchor)) return false;
    if (!r.u32(n) || n > kMaxIn) return false;
    out.nullifiers.resize(n); for (auto& x : out.nullifiers) if (!r.h(x)) return false;
    if (!r.u32(n) || n > kMaxOut) return false;
    out.commitments.resize(n); for (auto& x : out.commitments) if (!r.h(x)) return false;
    if (!r.u32(n) || n != out.commitments.size()) return false;
    out.enc.resize(n);
    for (auto& e : out.enc) if (!r.vec(e.kem_ct, 4096) || !r.vec(e.aead, 4096)) return false;
    if (!r.vec(out.proof, kMaxProofBytes)) return false;
    if (r.i != in.size()) return false;
    error.clear();
    return true;
}
std::string to_tx_field(const Bundle& b) { return hexs(serialize(b)); }
bool from_tx_field(const std::string& hex, Bundle& out, std::string& error) {
    // Own bounded decoder: generic hex_to_bytes caps at 128 KiB, STARK receipts are larger.
    constexpr std::size_t kMaxHex = 2 * (kMaxProofBytes + 64 * 1024);
    if (hex.empty() || hex.size() > kMaxHex || (hex.size() % 2) != 0) {
        error = "malformed shielded bundle hex";
        return false;
    }
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;  // lowercase only: one canonical encoding
    };
    std::vector<std::uint8_t> raw(hex.size() / 2);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) { error = "malformed shielded bundle hex"; return false; }
        raw[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return deserialize(raw, out, error);
}

H32 bind_hash(const Transaction& tx, const Bundle& b) {
    Bytes m{'A', 'D', 'D', '/', 'b', 'i', 'n', 'd', '/', 'v', '1'};
    auto str = [&](const std::string& s) { put32(m, (std::uint32_t)s.size()); m.insert(m.end(), s.begin(), s.end()); };
    put32(m, (std::uint32_t)tx.inputs.size());
    for (auto& i : tx.inputs) { str(i.previous_txid); put32(m, i.output_index); }
    put32(m, (std::uint32_t)tx.outputs.size());
    for (auto& o : tx.outputs) { str(o.recipient); put64(m, o.amount); }
    str(tx.signer); str(tx.signer_pubkey); put64(m, tx.fee); put64(m, tx.nonce);
    put64(m, b.vpub_in); put64(m, b.vpub_out);
    put32(m, (std::uint32_t)b.enc.size());
    for (auto& e : b.enc) { putv(m, e.kem_ct); putv(m, e.aead); }
    const auto id = image_id();
    puth(m, id);
    return sha256_raw(m);
}

Bytes expected_journal(const H32& bind, const Bundle& b) {
    Bytes j{'A', 'D', 'D', 'S', 'H', 'L', 'D', '1'};
    puth(j, bind); puth(j, b.anchor); put64(j, b.vpub_in); put64(j, b.vpub_out);
    put32(j, (std::uint32_t)b.nullifiers.size()); for (auto& n : b.nullifiers) puth(j, n);
    put32(j, (std::uint32_t)b.commitments.size()); for (auto& c : b.commitments) puth(j, c);
    return j;
}

// ---- note encryption -----------------------------------------------------------
bool encrypt_note(const Address& to, std::uint64_t value, const H32& r, const std::string& memo,
                  EncryptedNote& out, std::string& error) {
    if (memo.size() > kMemoSize) { error = "memo too long (max 512 bytes)"; return false; }
    if (to.kem_pk.size() != OQS_KEM_ml_kem_1024_length_public_key) { error = "bad recipient key"; return false; }
    OQS_KEM* kem = OQS_KEM_new(kKem);
    if (!kem) { error = "ML-KEM-1024 unavailable"; return false; }
    Bytes ss(kem->length_shared_secret);
    out.kem_ct.assign(kem->length_ciphertext, 0);
    const bool ok = OQS_KEM_encaps(kem, out.kem_ct.data(), ss.data(), to.kem_pk.data()) == OQS_SUCCESS;
    OQS_KEM_free(kem);
    if (!ok) { error = "ML-KEM encapsulation failed"; return false; }
    Bytes pt; put64(pt, value); puth(pt, r);
    pt.push_back(static_cast<std::uint8_t>(memo.size())); pt.push_back(static_cast<std::uint8_t>(memo.size() >> 8));
    pt.insert(pt.end(), memo.begin(), memo.end());
    pt.resize(kPlainSize, 0);  // fixed size: memo length does not leak
    std::uint8_t nonce[12];
    if (RAND_bytes(nonce, 12) != 1) { error = "rng failure"; return false; }
    Bytes ct;
    if (!aead(true, note_key(ss, out.kem_ct), nonce, out.kem_ct, pt, ct)) { error = "aead failure"; return false; }
    OPENSSL_cleanse(ss.data(), ss.size());
    out.aead.assign(nonce, nonce + 12);
    out.aead.insert(out.aead.end(), ct.begin(), ct.end());
    return true;
}

std::optional<Note> try_decrypt(const SpendingKey& sk, const Transaction& tx, const Bundle& b, std::uint32_t j) {
    if (j >= b.enc.size() || j >= b.commitments.size()) return std::nullopt;
    const auto& e = b.enc[j];
    if (e.aead.size() != 12 + kPlainSize + 16) return std::nullopt;
    OQS_KEM* kem = OQS_KEM_new(kKem);
    if (!kem || e.kem_ct.size() != kem->length_ciphertext || sk.kem_sk.size() != kem->length_secret_key) { OQS_KEM_free(kem); return std::nullopt; }
    Bytes ss(kem->length_shared_secret);
    const bool ok = OQS_KEM_decaps(kem, ss.data(), e.kem_ct.data(), sk.kem_sk.data()) == OQS_SUCCESS;
    OQS_KEM_free(kem);
    if (!ok) return std::nullopt;
    Bytes body(e.aead.begin() + 12, e.aead.end()), pt;
    const bool dec = aead(false, note_key(ss, e.kem_ct), e.aead.data(), e.kem_ct, body, pt);
    OPENSSL_cleanse(ss.data(), ss.size());
    if (!dec || pt.size() != kPlainSize) return std::nullopt;
    Note n;
    for (int k = 7; k >= 0; --k) n.value = (n.value << 8) | pt[k];
    std::memcpy(n.r.data(), pt.data() + 8, 32);
    const std::size_t ml = pt[40] | (pt[41] << 8);
    if (ml > kMemoSize) return std::nullopt;
    n.memo.assign(reinterpret_cast<const char*>(pt.data() + 42), ml);
    n.owner = owner_tag(sk.nk);
    n.rho = output_rho(bind_hash(tx, b), b.nullifiers, j);
    if (note_commitment(n.value, n.owner, n.rho, n.r) != b.commitments[j]) return std::nullopt;  // not ours / malformed
    return n;
}

// ---- tree --------------------------------------------------------------------
CommitmentTree::CommitmentTree() {
    empty_[0] = H32{};
    for (std::size_t d = 0; d < kTreeDepth; ++d) empty_[d + 1] = merkle_node(empty_[d], empty_[d]);
    root_ = empty_[kTreeDepth];
}
void CommitmentTree::append(const H32& cm) {
    std::uint64_t pos = leaves_.size();
    leaves_.push_back(cm);
    H32 cur = cm;
    for (std::size_t d = 0; d < kTreeDepth; ++d) {
        if (((pos >> d) & 1) == 0) { frontier_[d] = cur; cur = merkle_node(cur, empty_[d]); }
        else { cur = merkle_node(frontier_[d], cur); }
    }
    root_ = cur;
}
H32 CommitmentTree::root() const { return root_; }
bool CommitmentTree::path(std::uint64_t pos, std::array<H32, kTreeDepth>& sib) const {
    if (pos >= leaves_.size()) return false;
    std::vector<H32> layer = leaves_;
    for (std::size_t d = 0; d < kTreeDepth; ++d) {
        const std::uint64_t s = pos ^ 1;
        sib[d] = s < layer.size() ? layer[s] : empty_[d];
        std::vector<H32> next((layer.size() + 1) / 2);
        for (std::size_t i = 0; i < next.size(); ++i)
            next[i] = merkle_node(layer[2 * i], 2 * i + 1 < layer.size() ? layer[2 * i + 1] : empty_[d]);
        layer = std::move(next);
        pos >>= 1;
    }
    return true;
}

// ---- state -------------------------------------------------------------------
State::State() { remember_root(); }
void State::remember_root() {
    const auto r = hex32(tree_.root());
    if (recent_set_.insert(r).second) recent_.push_back(tree_.root());
    while (recent_.size() > kRecentAnchors) { recent_set_.erase(hex32(recent_.front())); recent_.pop_front(); }
}
bool State::nullifier_spent(const H32& nf) const { return nfs_.count(hex32(nf)) != 0; }
bool State::check(const Bundle& b, const std::unordered_set<std::string>& pending, std::string& error) const {
    if (b.commitments.size() != b.enc.size()) { error = "shielded: enc/cm count mismatch"; return false; }
    if (b.nullifiers.empty() && b.commitments.empty()) { error = "shielded: empty bundle"; return false; }
    if (!b.nullifiers.empty() && !recent_set_.count(hex32(b.anchor))) { error = "shielded: unknown or stale anchor"; return false; }
    if (b.nullifiers.empty() && b.anchor != H32{}) { error = "shielded: anchor must be zero without spends"; return false; }
    if (tree_.size() + b.commitments.size() > (1ULL << kTreeDepth)) { error = "shielded: tree full"; return false; }
    std::unordered_set<std::string> local;
    for (const auto& n : b.nullifiers) {
        const auto k = hex32(n);
        if (nfs_.count(k) || pending.count(k) || !local.insert(k).second) { error = "shielded: nullifier already spent"; return false; }
    }
    for (const auto& c : b.commitments)
        if (cms_.count(hex32(c))) { error = "shielded: duplicate commitment"; return false; }
    return true;
}
void State::apply(const Bundle& b) {
    for (const auto& n : b.nullifiers) nfs_.insert(hex32(n));
    for (const auto& c : b.commitments) { tree_.append(c); cms_.insert(hex32(c)); }
    remember_root();
}

bool verify_bundle_proof(const Transaction& tx, const Bundle& b, std::string& error) {
#ifdef ADDITION_HAVE_SHIELDED_STARK
    const auto journal = expected_journal(bind_hash(tx, b), b);
    const int rc = addition_shielded_verify(b.proof.data(), b.proof.size(), journal.data(), journal.size());
    if (rc != ADDSH_OK) { error = "shielded: STARK verification failed (code " + std::to_string(rc) + ")"; return false; }
    return true;
#else
    (void)tx; (void)b;
    error = "shielded: node built without STARK verifier; rejecting (fail-closed)";
    return false;
#endif
}

bool build_bundle(const Transaction& tx, const SpendingKey* spender, const std::vector<Note>& inputs,
                  const CommitmentTree& tree, const std::vector<OutputSpec>& outputs, std::uint64_t vpub_in,
                  std::uint64_t vpub_out, Bundle& out, std::string& error, double* prove_seconds) {
    out = Bundle{};
    if (inputs.size() > kMaxIn || outputs.size() > kMaxOut) { error = "too many inputs/outputs"; return false; }
    if (!inputs.empty() && !spender) { error = "spending key required"; return false; }
    out.vpub_in = vpub_in; out.vpub_out = vpub_out;
    std::vector<H32> rs(outputs.size());
    for (std::size_t j = 0; j < outputs.size(); ++j) {
        if (RAND_bytes(rs[j].data(), 32) != 1) { error = "rng failure"; return false; }
        EncryptedNote e;
        if (!encrypt_note(outputs[j].to, outputs[j].value, rs[j], outputs[j].memo, e, error)) return false;
        out.enc.push_back(std::move(e));
    }
    const H32 bind = bind_hash(tx, out);
    Bytes w; puth(w, bind); put64(w, vpub_in); put64(w, vpub_out);
    put32(w, (std::uint32_t)inputs.size());
    for (const auto& n : inputs) {
        std::array<H32, kTreeDepth> sib{};
        if (!tree.path(n.position, sib)) { error = "note position not in tree"; return false; }
        puth(w, spender->nk); put64(w, n.value); puth(w, n.rho); puth(w, n.r); put64(w, n.position);
        for (auto& s : sib) puth(w, s);
        out.nullifiers.push_back(nullifier(spender->nk, n.rho));
    }
    if (!inputs.empty()) out.anchor = tree.root();
    put32(w, (std::uint32_t)outputs.size());
    for (std::size_t j = 0; j < outputs.size(); ++j) {
        put64(w, outputs[j].value); puth(w, outputs[j].to.owner); puth(w, rs[j]);
        out.commitments.push_back(note_commitment(outputs[j].value, outputs[j].to.owner,
                                                  output_rho(bind, out.nullifiers, (std::uint32_t)j), rs[j]));
    }
#ifdef ADDITION_HAVE_SHIELDED_STARK
    std::uint8_t *rp = nullptr, *jp = nullptr;
    std::size_t rl = 0, jl = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const int rc = addition_shielded_prove(w.data(), w.size(), &rp, &rl, &jp, &jl);
    OPENSSL_cleanse(w.data(), w.size());
    if (prove_seconds) *prove_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (rc != ADDSH_OK) { error = "proving failed (code " + std::to_string(rc) + ")"; return false; }
    out.proof.assign(rp, rp + rl);
    Bytes journal(jp, jp + jl);
    addition_shielded_free(rp, rl);
    addition_shielded_free(jp, jl);
    if (journal != expected_journal(bind, out)) { error = "journal mismatch (C++/Rust statement drift)"; return false; }
    return true;
#else
    (void)prove_seconds;
    error = "built without STARK prover";
    return false;
#endif
}

} // namespace addition::shielded
