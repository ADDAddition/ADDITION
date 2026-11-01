#include "addition/pq_session.hpp"

#include "addition/crypto.hpp"
#include "addition/pm_relay.hpp"

#include <oqs/oqs.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace addition::pqmsg {
namespace {

constexpr const char* kSuite = "MLKEM1024+AES256GCM+HMAC-SHA3-512/MLDSA87";
constexpr std::size_t kMaxSkip = 512;
constexpr std::size_t kMaxStoredSkipped = 256;

std::uint64_t now_sec() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string h32(const Bytes& b) { return to_hex(sha3_512_bytes(b)).substr(0, 32); }
std::string h32s(const std::string& s) { return to_hex(sha3_512_bytes(s)).substr(0, 32); }

bool safe_name(const std::string& w) {
    if (w.empty() || w.size() > 64 || w[0] == '.') return false;
    return std::all_of(w.begin(), w.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
    });
}

Bytes rand_bytes(std::size_t n) {
    Bytes b(n);
    RAND_bytes(b.data(), static_cast<int>(n));
    return b;
}

void wipe(Bytes& b) { if (!b.empty()) OPENSSL_cleanse(b.data(), b.size()); b.clear(); }

Bytes hmac(const Bytes& key, const std::string& label, const Bytes& data = {}) {
    Bytes msg(label.begin(), label.end());
    msg.insert(msg.end(), data.begin(), data.end());
    Bytes out(EVP_MAX_MD_SIZE);
    unsigned int len = 0;
    HMAC(EVP_sha3_512(), key.data(), static_cast<int>(key.size()), msg.data(), msg.size(), out.data(), &len);
    out.resize(len);
    OPENSSL_cleanse(msg.data(), msg.size());
    return out;
}

void kdf_rk(Bytes& rk, const Bytes& ss, Bytes& ck) {
    auto o = hmac(rk, "AMLS1 rk", ss);
    rk.assign(o.begin(), o.begin() + 32);
    ck.assign(o.begin() + 32, o.begin() + 64);
    wipe(o);
}

Bytes kdf_ck(Bytes& ck) {
    auto m = hmac(ck, "\x01");
    auto c = hmac(ck, "\x02");
    Bytes mk(m.begin(), m.begin() + 32);
    ck.assign(c.begin(), c.begin() + 32);
    wipe(m); wipe(c);
    return mk;
}

bool aead(bool enc, const Bytes& key, const Bytes& iv, const std::string& aad, const Bytes& in, Bytes& out) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    Bytes body = in;
    unsigned char tag[16];
    if (!enc) {
        if (body.size() < 16) { EVP_CIPHER_CTX_free(ctx); return false; }
        std::copy(body.end() - 16, body.end(), tag);
        body.resize(body.size() - 16);
    }
    out.assign(body.size() + (enc ? 16 : 0), 0);
    int len = 0;
    bool ok = EVP_CipherInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr, enc ? 1 : 0) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) == 1 &&
              EVP_CipherInit_ex(ctx, nullptr, nullptr, key.data(), iv.data(), enc ? 1 : 0) == 1 &&
              EVP_CipherUpdate(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(aad.data()),
                               static_cast<int>(aad.size())) == 1 &&
              EVP_CipherUpdate(ctx, out.data(), &len, body.data(), static_cast<int>(body.size())) == 1;
    if (ok && !enc) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1;
    int fl = 0;
    if (ok) ok = EVP_CipherFinal_ex(ctx, out.data() + len, &fl) == 1;
    if (ok && enc) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out.data() + body.size()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(body.data(), body.size());
    if (!ok) out.clear();
    return ok;
}

bool kem_keypair(Bytes& pk, Bytes& sk) {
    OQS_KEM* k = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!k) return false;
    pk.assign(k->length_public_key, 0);
    sk.assign(k->length_secret_key, 0);
    const bool ok = OQS_KEM_keypair(k, pk.data(), sk.data()) == OQS_SUCCESS;
    OQS_KEM_free(k);
    return ok;
}

bool kem_encaps(const Bytes& pk, Bytes& ct, Bytes& ss) {
    OQS_KEM* k = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!k) return false;
    if (pk.size() != k->length_public_key) { OQS_KEM_free(k); return false; }
    ct.assign(k->length_ciphertext, 0);
    ss.assign(k->length_shared_secret, 0);
    const bool ok = OQS_KEM_encaps(k, ct.data(), ss.data(), pk.data()) == OQS_SUCCESS;
    OQS_KEM_free(k);
    return ok;
}

bool kem_decaps(const Bytes& sk, const Bytes& ct, Bytes& ss) {
    OQS_KEM* k = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!k) return false;
    if (ct.size() != k->length_ciphertext || sk.size() != k->length_secret_key) { OQS_KEM_free(k); return false; }
    ss.assign(k->length_shared_secret, 0);
    const bool ok = OQS_KEM_decaps(k, ss.data(), ct.data(), sk.data()) == OQS_SUCCESS;
    OQS_KEM_free(k);
    return ok;
}

// Length-prefixed binary encoding.
void put(Bytes& o, const Bytes& b) {
    const auto n = static_cast<std::uint32_t>(b.size());
    for (int i = 3; i >= 0; --i) o.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
    o.insert(o.end(), b.begin(), b.end());
}
void put(Bytes& o, const std::string& s) { put(o, Bytes(s.begin(), s.end())); }
void put64(Bytes& o, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
struct Reader {
    const Bytes& b;
    std::size_t p = 0;
    bool ok = true;
    bool magic(const char* m) {
        const std::string s(m);
        if (b.size() < p + s.size() || std::string(b.begin() + p, b.begin() + p + s.size()) != s) return ok = false;
        p += s.size();
        return true;
    }
    Bytes get(std::size_t max = 65536) {
        if (!ok || b.size() < p + 4) { ok = false; return {}; }
        std::uint32_t n = 0;
        for (int i = 0; i < 4; ++i) n = (n << 8) | b[p++];
        if (n > max || b.size() < p + n) { ok = false; return {}; }
        Bytes r(b.begin() + p, b.begin() + p + n);
        p += n;
        return r;
    }
    std::string gets(std::size_t max = 256) { auto r = get(max); return std::string(r.begin(), r.end()); }
    std::uint64_t get64() {
        if (!ok || b.size() < p + 8) { ok = false; return 0; }
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | b[p++];
        return v;
    }
    bool done() const { return ok && p == b.size(); }
};

bool write_secret(const std::filesystem::path& path, const std::string& content, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    ::chmod(path.parent_path().c_str(), 0700);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        ::chmod(tmp.c_str(), 0600);
        out << content;
        if (!out) { error = "cannot write " + path.filename().string(); return false; }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { error = "cannot write " + path.filename().string(); return false; }
    return true;
}

bool unhex(const std::string& h, Bytes& out) {
    if (h == "-") { out.clear(); return true; }
    std::string e;
    return hex_to_bytes(h, out, e);
}
std::string hx(const Bytes& b) { return b.empty() ? "-" : bytes_to_hex(b); }

} // namespace

std::string handle_index(const std::string& handle) {
    std::string h;
    for (char c : handle) if (!std::isspace(static_cast<unsigned char>(c))) h.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return h32s("ADDITION-KP-dir|v1|" + h);
}

std::string fingerprint(const Bytes& identity_pub) {
    return to_hex(sha3_512_bytes(identity_pub)).substr(0, 40);
}

bool sig_keypair(Bytes& pk, Bytes& sk) {
    OQS_SIG* s = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87);
    if (!s) return false;
    pk.assign(s->length_public_key, 0);
    sk.assign(s->length_secret_key, 0);
    const bool ok = OQS_SIG_keypair(s, pk.data(), sk.data()) == OQS_SUCCESS;
    OQS_SIG_free(s);
    return ok;
}

bool sig_sign(const Bytes& sk, const std::string& msg, Bytes& sig) {
    OQS_SIG* s = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87);
    if (!s) return false;
    sig.assign(s->length_signature, 0);
    std::size_t len = 0;
    const bool ok = sk.size() == s->length_secret_key &&
                    OQS_SIG_sign(s, sig.data(), &len, reinterpret_cast<const std::uint8_t*>(msg.data()),
                                 msg.size(), sk.data()) == OQS_SUCCESS;
    OQS_SIG_free(s);
    sig.resize(ok ? len : 0);
    return ok;
}

bool sig_verify(const Bytes& pk, const std::string& msg, const Bytes& sig) {
    OQS_SIG* s = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87);
    if (!s) return false;
    const bool ok = pk.size() == s->length_public_key && !sig.empty() && sig.size() <= s->length_signature &&
                    OQS_SIG_verify(s, reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size(), sig.data(),
                                   sig.size(), pk.data()) == OQS_SUCCESS;
    OQS_SIG_free(s);
    return ok;
}

// ---------------------------------------------------------------- KeyPackage

std::string KeyPackage::tbs() const {
    return std::string("AKP1|") + kSuite + "|" + index + "|" + bytes_to_hex(identity_pub) + "|" +
           bytes_to_hex(init_pub) + "|" + bytes_to_hex(relay_pub) + "|" + std::to_string(created) + "|" +
           std::to_string(expires);
}

Bytes KeyPackage::encode() const {
    Bytes o{'A', 'K', 'P', '1'};
    put(o, index);
    put(o, identity_pub);
    put(o, init_pub);
    put(o, relay_pub);
    put64(o, created);
    put64(o, expires);
    put(o, sig);
    return o;
}

std::string KeyPackage::hex() const { return bytes_to_hex(encode()); }
std::string KeyPackage::hash() const { return h32(encode()); }

bool KeyPackage::decode(const Bytes& in, KeyPackage& out, std::string& error) {
    Reader r{in};
    KeyPackage k;
    r.magic("AKP1");
    k.index = r.gets(64);
    k.identity_pub = r.get(4096);
    k.init_pub = r.get(4096);
    k.relay_pub = r.get(4096);
    k.created = r.get64();
    k.expires = r.get64();
    k.sig = r.get(8192);
    if (!r.done()) { error = "malformed key package"; return false; }
    out = std::move(k);
    return true;
}

bool KeyPackage::from_hex(const std::string& hex, KeyPackage& out, std::string& error) {
    std::string h = hex;
    if (h.rfind("addc1:", 0) == 0) h = h.substr(6);
    Bytes b;
    if (h.size() > 64000 || !hex_to_bytes(h, b, error)) { error = "malformed key package"; return false; }
    return decode(b, out, error);
}

bool KeyPackage::verify(std::uint64_t now, std::string& error) const {
    if (identity_pub.size() != 2592 || init_pub.size() != 1568 || relay_pub.size() != 1568) {
        error = "key package: bad key sizes";
        return false;
    }
    if (index != "-" && (index.size() != 32 || !std::all_of(index.begin(), index.end(), [](char c) {
                             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))) {
        error = "key package: bad index";
        return false;
    }
    if (expires <= created || expires - created > kMaxPackageLifetime || created > now + 600) {
        error = "key package: bad lifetime";
        return false;
    }
    if (expires <= now) { error = "key package expired"; return false; }
    if (!sig_verify(identity_pub, tbs(), sig)) { error = "key package: bad ML-DSA-87 signature"; return false; }
    return true;
}

// ------------------------------------------------------------------ Session

struct Messenger::Session {
    std::string sid, peer_fp, my_fp;
    Bytes peer_identity, peer_relay;
    Bytes rk, cks, ckr;
    std::uint32_t ns = 0, nr = 0, pn = 0;
    Bytes my_pub, my_sk, prev_pub, prev_sk, peer_rk, step_ct;
    std::string step_to = "-";
    bool need_step = false;
    std::map<std::string, Bytes> skipped;  // h32(rk_pub):n -> mk

    std::string ser() const {
        std::ostringstream o;
        o << "sid " << sid << "\npeer_fp " << peer_fp << "\nmy_fp " << my_fp << "\npeer_id " << hx(peer_identity)
          << "\npeer_relay " << hx(peer_relay) << "\nrk " << hx(rk) << "\ncks " << hx(cks) << "\nckr " << hx(ckr)
          << "\nns " << ns << "\nnr " << nr << "\npn " << pn << "\nmy_pub " << hx(my_pub) << "\nmy_sk "
          << hx(my_sk) << "\nprev_pub " << hx(prev_pub) << "\nprev_sk " << hx(prev_sk) << "\npeer_rk "
          << hx(peer_rk) << "\nstep_ct " << hx(step_ct) << "\nstep_to " << step_to << "\nneed_step "
          << (need_step ? 1 : 0) << "\n";
        for (const auto& [k, v] : skipped) o << "skip " << k << " " << hx(v) << "\n";
        return o.str();
    }
    bool parse(std::istream& in) {
        std::string k, v;
        while (in >> k) {
            if (k == "skip") {
                std::string id;
                Bytes mk;
                if (!(in >> id >> v) || !unhex(v, mk)) return false;
                skipped[id] = mk;
                continue;
            }
            if (!(in >> v)) return false;
            bool ok = true;
            if (k == "sid") sid = v;
            else if (k == "peer_fp") peer_fp = v;
            else if (k == "my_fp") my_fp = v;
            else if (k == "peer_id") ok = unhex(v, peer_identity);
            else if (k == "peer_relay") ok = unhex(v, peer_relay);
            else if (k == "rk") ok = unhex(v, rk);
            else if (k == "cks") ok = unhex(v, cks);
            else if (k == "ckr") ok = unhex(v, ckr);
            else if (k == "ns") ns = static_cast<std::uint32_t>(std::stoul(v));
            else if (k == "nr") nr = static_cast<std::uint32_t>(std::stoul(v));
            else if (k == "pn") pn = static_cast<std::uint32_t>(std::stoul(v));
            else if (k == "my_pub") ok = unhex(v, my_pub);
            else if (k == "my_sk") ok = unhex(v, my_sk);
            else if (k == "prev_pub") ok = unhex(v, prev_pub);
            else if (k == "prev_sk") ok = unhex(v, prev_sk);
            else if (k == "peer_rk") ok = unhex(v, peer_rk);
            else if (k == "step_ct") ok = unhex(v, step_ct);
            else if (k == "step_to") step_to = v;
            else if (k == "need_step") need_step = v == "1";
            if (!ok) return false;
        }
        return !sid.empty() && rk.size() == 32;
    }
    void wipe_all() {
        wipe(rk); wipe(cks); wipe(ckr); wipe(my_sk); wipe(prev_sk);
        for (auto& [k, v] : skipped) wipe(v);
    }
};

Messenger::Messenger(std::string dir, PmRelay* relay) : dir_(std::move(dir)), relay_(relay) {}

bool Messenger::load_identity(const std::string& wallet, Bytes& pk, Bytes& sk, bool create, std::string& error) {
    if (!safe_name(wallet)) { error = "invalid wallet name"; return false; }
    const auto path = std::filesystem::path(dir_) / (wallet + ".idkey");
    std::ifstream in(path);
    if (in) {
        std::string a, b;
        in >> a >> b;
        if (!unhex(a, pk) || !unhex(b, sk) || pk.size() != 2592) { error = "corrupt identity key"; return false; }
        return true;
    }
    if (!create) { error = "no messaging identity (run msg_kp_publish)"; return false; }
    if (!sig_keypair(pk, sk)) { error = "ML-DSA-87 keygen failed"; return false; }
    return write_secret(path, bytes_to_hex(pk) + "\n" + bytes_to_hex(sk) + "\n", error);
}

bool Messenger::load_package(const std::string& wallet, KeyPackage& kp, std::string& error) {
    std::ifstream in(std::filesystem::path(dir_) / (wallet + ".kp"));
    std::string tag, v;
    bool found = false;
    while (in >> tag) {
        if (tag == "kp" && (in >> v)) found = KeyPackage::from_hex(v, kp, error);
        else std::getline(in, v);
    }
    if (!found) { error = "no key package for wallet (run msg_kp_publish)"; return false; }
    return kp.verify(now_sec(), error);
}

bool Messenger::find_init_sk(const std::string& wallet, const std::string& kp_hash, Bytes& sk, std::string& error) {
    std::ifstream in(std::filesystem::path(dir_) / (wallet + ".kp"));
    std::string tag, h, exp, skh;
    while (in >> tag) {
        if (tag == "init" && (in >> h >> exp >> skh) && h == kp_hash) {
            if (std::stoull(exp) <= now_sec()) break;
            if (unhex(skh, sk)) return true;
        } else if (tag == "kp") {
            std::getline(in, h);
        }
    }
    error = "unknown or expired key package";
    return false;
}

bool Messenger::publish(const std::string& wallet, const std::string& handle, std::uint64_t days,
                        std::string& contact, std::string& fp, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    if (relay_ == nullptr) { error = "pm relay disabled"; return false; }
    Bytes id_pk, id_sk;
    if (!load_identity(wallet, id_pk, id_sk, true, error)) return false;
    std::string relay_hex, mailbox;
    if (!relay_->ensure_key(wallet, relay_hex, mailbox, error)) { wipe(id_sk); return false; }
    KeyPackage kp;
    Bytes init_sk;
    if (!kem_keypair(kp.init_pub, init_sk)) { wipe(id_sk); error = "ML-KEM-1024 keygen failed"; return false; }
    unhex(relay_hex, kp.relay_pub);
    kp.identity_pub = id_pk;
    kp.index = (handle.empty() || handle == "-") ? "-" : handle_index(handle);
    kp.created = now_sec();
    kp.expires = kp.created + std::clamp<std::uint64_t>(days, 1, 30) * 24 * 3600;
    const bool signed_ok = sig_sign(id_sk, kp.tbs(), kp.sig);
    wipe(id_sk);
    if (!signed_ok) { wipe(init_sk); error = "ML-DSA-87 signing failed"; return false; }
    // Keep unexpired init keys (last-resort semantics) so in-flight INITs still open.
    std::ostringstream o;
    o << "kp " << kp.hex() << "\n";
    {
        std::ifstream in(std::filesystem::path(dir_) / (wallet + ".kp"));
        std::string tag, h, exp, skh;
        while (in >> tag) {
            if (tag == "init" && (in >> h >> exp >> skh)) {
                if (std::stoull(exp) > now_sec()) o << "init " << h << " " << exp << " " << skh << "\n";
            } else {
                std::getline(in, h);
            }
        }
    }
    o << "init " << kp.hash() << " " << kp.expires << " " << bytes_to_hex(init_sk) << "\n";
    wipe(init_sk);
    if (!write_secret(std::filesystem::path(dir_) / (wallet + ".kp"), o.str(), error)) return false;
    if (kp.index != "-" && !relay_->publish_package(kp.hex(), error)) return false;
    contact = "addc1:" + kp.hex();
    fp = fingerprint(id_pk);
    return true;
}

bool Messenger::lookup(const std::string& x, KeyPackage& kp, std::string& error) {
    if (x.rfind("addc1:", 0) == 0) {
        if (!KeyPackage::from_hex(x, kp, error)) return false;
    } else {
        if (relay_ == nullptr) { error = "pm relay disabled"; return false; }
        std::string hex;
        const auto idx = handle_index(x);
        if (!relay_->find_package(idx, hex, error)) return false;
        if (!KeyPackage::from_hex(hex, kp, error)) return false;
        if (kp.index != idx) { error = "directory entry index mismatch"; return false; }
    }
    return kp.verify(now_sec(), error);
}

bool Messenger::load_session(const std::string& wallet, const std::string& sid, Session& s, std::string& error) {
    if (!safe_name(wallet) || !safe_name(sid)) { error = "invalid name"; return false; }
    std::ifstream in(std::filesystem::path(dir_) / "sessions" / wallet / (sid + ".s"));
    if (!in || !s.parse(in)) { error = "unknown session"; return false; }
    return true;
}

bool Messenger::save_session(const std::string& wallet, const Session& s, std::string& error) {
    return write_secret(std::filesystem::path(dir_) / "sessions" / wallet / (s.sid + ".s"), s.ser(), error);
}

bool Messenger::encrypt_locked(Session& s, const std::string& text, Bytes& body, std::string& error) {
    if (s.need_step) {
        Bytes ss, npub, nsk;
        if (!kem_encaps(s.peer_rk, s.step_ct, ss) || !kem_keypair(npub, nsk)) { error = "ratchet step failed"; return false; }
        s.step_to = h32(s.peer_rk);
        wipe(s.prev_sk);
        s.prev_pub = s.my_pub; s.prev_sk = s.my_sk;
        s.my_pub = npub; s.my_sk = nsk;
        kdf_rk(s.rk, ss, s.cks);
        wipe(ss);
        s.pn = s.ns;
        s.ns = 0;
        s.need_step = false;
    }
    if (s.cks.size() != 32) { error = "session not ready to send"; return false; }
    const auto n = s.ns++;
    auto mk = kdf_ck(s.cks);
    const auto iv = rand_bytes(12);
    const std::string aad = "AMLS1|M|" + s.sid + "|" + h32(s.my_pub) + "|" + h32(s.step_ct) + "|" + s.step_to +
                            "|" + std::to_string(s.pn) + "|" + std::to_string(n);
    Bytes ct;
    const bool ok = aead(true, mk, iv, aad, Bytes(text.begin(), text.end()), ct);
    wipe(mk);
    if (!ok) { error = "encryption failed"; return false; }
    body = {'A', 'M', 'M', '1'};
    put(body, s.sid);
    put(body, s.my_pub);
    put(body, s.step_ct);
    put(body, s.step_to);
    put64(body, s.pn);
    put64(body, n);
    put(body, iv);
    put(body, ct);
    return true;
}

bool Messenger::decrypt_locked(Session& s0, const Bytes& rk_pub, const Bytes& step_ct, const std::string& to,
                               std::uint32_t pn, std::uint32_t n, const Bytes& iv, const Bytes& ct,
                               const std::string& aad, std::string& text, std::string& error) {
    Session s = s0;  // commit only on successful authentication
    auto skip_to = [&](std::uint32_t until) -> bool {
        if (s.ckr.size() != 32) return true;
        if (until > s.nr + kMaxSkip) return false;
        while (s.nr < until) {
            s.skipped[h32(s.peer_rk) + ":" + std::to_string(s.nr)] = kdf_ck(s.ckr);
            ++s.nr;
        }
        while (s.skipped.size() > kMaxStoredSkipped) s.skipped.erase(s.skipped.begin());
        return true;
    };
    Bytes mk;
    const auto key = h32(rk_pub) + ":" + std::to_string(n);
    if (auto it = s.skipped.find(key); it != s.skipped.end()) {
        mk = it->second;
        s.skipped.erase(it);
    } else {
        if (rk_pub != s.peer_rk) {
            // New receiving chain: the peer performed a KEM ratchet step.
            if (step_ct.empty()) { error = "out-of-order frame (no ratchet ciphertext)"; return false; }
            if (!skip_to(pn)) { error = "too many skipped messages"; return false; }
            const Bytes* sk = nullptr;
            if (!s.my_pub.empty() && to == h32(s.my_pub)) sk = &s.my_sk;
            else if (!s.prev_pub.empty() && to == h32(s.prev_pub)) sk = &s.prev_sk;
            Bytes ss;
            if (sk == nullptr || !kem_decaps(*sk, step_ct, ss)) { error = "ratchet key unknown"; return false; }
            kdf_rk(s.rk, ss, s.ckr);
            wipe(ss);
            s.peer_rk = rk_pub;
            s.nr = 0;
            s.need_step = true;
        }
        if (n < s.nr) { error = "duplicate or replayed message"; return false; }
        if (!skip_to(n)) { error = "too many skipped messages"; return false; }
        mk = kdf_ck(s.ckr);
        ++s.nr;
    }
    Bytes pt;
    const bool ok = aead(false, mk, iv, aad, ct, pt);
    wipe(mk);
    if (!ok) { s.wipe_all(); error = "authentication failed"; return false; }
    text.assign(pt.begin(), pt.end());
    wipe(pt);
    s0.wipe_all();
    s0 = std::move(s);
    return true;
}

bool Messenger::start_frame(const std::string& wallet, const KeyPackage& peer, const std::string& text,
                            std::string& sid, Bytes& frame, std::string& error) {
    if (!peer.verify(now_sec(), error)) return false;
    Bytes id_pk, id_sk;
    if (!load_identity(wallet, id_pk, id_sk, false, error)) return false;
    KeyPackage mine;
    if (!load_package(wallet, mine, error)) { wipe(id_sk); return false; }
    Session s;
    s.sid = bytes_to_hex(rand_bytes(16));
    s.my_fp = fingerprint(id_pk);
    s.peer_fp = fingerprint(peer.identity_pub);
    s.peer_identity = peer.identity_pub;
    s.peer_relay = peer.relay_pub;
    Bytes ct1, ss1;
    if (!kem_encaps(peer.init_pub, ct1, ss1) || !kem_keypair(s.my_pub, s.my_sk)) {
        wipe(id_sk);
        error = "handshake keygen failed";
        return false;
    }
    const std::string transcript = "AMLS1-INIT|" + s.sid + "|" + peer.hash() + "|" + mine.hash() + "|" + h32(ct1) +
                                   "|" + h32(s.my_pub);
    Bytes sig;
    const bool sok = sig_sign(id_sk, transcript, sig);
    wipe(id_sk);
    if (!sok) { wipe(ss1); error = "ML-DSA-87 signing failed"; return false; }
    const auto th = sha3_512_bytes(transcript);
    s.rk = hmac(Bytes(th.begin(), th.end()), "AMLS1 init", ss1);
    s.rk.resize(32);
    wipe(ss1);
    s.cks = hmac(s.rk, "AMLS1 chain-i");
    s.cks.resize(32);
    auto mk = kdf_ck(s.cks);
    s.ns = 1;
    const auto iv = rand_bytes(12);
    Bytes ct;
    const bool ok = aead(true, mk, iv, "AMLS1|I|" + h32s(transcript), Bytes(text.begin(), text.end()), ct);
    wipe(mk);
    if (!ok) { error = "encryption failed"; return false; }
    frame = {'A', 'M', 'I', '1'};
    put(frame, s.sid);
    put(frame, peer.hash());
    put(frame, mine.encode());
    put(frame, ct1);
    put(frame, s.my_pub);
    put(frame, sig);
    put(frame, iv);
    put(frame, ct);
    sid = s.sid;
    const bool saved = save_session(wallet, s, error);
    s.wipe_all();
    return saved;
}

bool Messenger::send_frame(const std::string& wallet, const std::string& sid, const std::string& text,
                           Bytes& frame, std::string& error) {
    Session s;
    if (!load_session(wallet, sid, s, error)) return false;
    bool ok = encrypt_locked(s, text, frame, error) && save_session(wallet, s, error);
    s.wipe_all();
    return ok;
}

bool Messenger::open_frame(const std::string& wallet, const Bytes& frame, std::string& sid, std::string& from_fp,
                           std::string& text, std::string& error) {
    Reader r{frame};
    if (frame.size() >= 4 && std::string(frame.begin(), frame.begin() + 4) == "AMI1") {
        r.magic("AMI1");
        sid = r.gets(64);
        const auto my_kp_hash = r.gets(64);
        const auto sender_kp_bytes = r.get(16384);
        const auto ct1 = r.get(4096);
        const auto rk_pub = r.get(4096);
        const auto sig = r.get(8192);
        const auto iv = r.get(64);
        const auto ct = r.get(40000);
        if (!r.done() || iv.size() != 12 || !safe_name(sid)) { error = "malformed init frame"; return false; }
        Session existing;
        std::string e2;
        if (load_session(wallet, sid, existing, e2)) { existing.wipe_all(); error = "duplicate or replayed message"; return false; }
        KeyPackage sender;
        if (!KeyPackage::decode(sender_kp_bytes, sender, error) || !sender.verify(now_sec(), error)) return false;
        const std::string transcript = "AMLS1-INIT|" + sid + "|" + my_kp_hash + "|" + sender.hash() + "|" + h32(ct1) +
                                       "|" + h32(rk_pub);
        if (!sig_verify(sender.identity_pub, transcript, sig)) { error = "init: bad ML-DSA-87 signature"; return false; }
        Bytes init_sk, ss1;
        if (!find_init_sk(wallet, my_kp_hash, init_sk, error)) return false;
        const bool dok = kem_decaps(init_sk, ct1, ss1);
        wipe(init_sk);
        if (!dok) { error = "decapsulation failed"; return false; }
        Bytes id_pk, id_sk;
        if (!load_identity(wallet, id_pk, id_sk, false, error)) return false;
        wipe(id_sk);
        Session s;
        s.sid = sid;
        s.my_fp = fingerprint(id_pk);
        s.peer_fp = fingerprint(sender.identity_pub);
        s.peer_identity = sender.identity_pub;
        s.peer_relay = sender.relay_pub;
        const auto th = sha3_512_bytes(transcript);
        s.rk = hmac(Bytes(th.begin(), th.end()), "AMLS1 init", ss1);
        s.rk.resize(32);
        wipe(ss1);
        s.ckr = hmac(s.rk, "AMLS1 chain-i");
        s.ckr.resize(32);
        auto mk = kdf_ck(s.ckr);
        s.nr = 1;
        s.peer_rk = rk_pub;
        s.need_step = true;
        Bytes pt;
        const bool ok = aead(false, mk, iv, "AMLS1|I|" + h32s(transcript), ct, pt);
        wipe(mk);
        if (!ok) { s.wipe_all(); error = "authentication failed"; return false; }
        text.assign(pt.begin(), pt.end());
        wipe(pt);
        from_fp = s.peer_fp;
        const bool saved = save_session(wallet, s, error);
        s.wipe_all();
        return saved;
    }
    r.magic("AMM1");
    sid = r.gets(64);
    const auto rk_pub = r.get(4096);
    const auto step_ct = r.get(4096);
    const auto to = r.gets(64);
    const auto pn = r.get64();
    const auto n = r.get64();
    const auto iv = r.get(64);
    const auto ct = r.get(40000);
    if (!r.done() || iv.size() != 12 || pn > 0xffffffffULL || n > 0xffffffffULL) {
        error = "malformed frame";
        return false;
    }
    Session s;
    if (!load_session(wallet, sid, s, error)) return false;
    const std::string aad = "AMLS1|M|" + sid + "|" + h32(rk_pub) + "|" + h32(step_ct) + "|" + to + "|" +
                            std::to_string(pn) + "|" + std::to_string(n);
    bool ok = decrypt_locked(s, rk_pub, step_ct, to, static_cast<std::uint32_t>(pn), static_cast<std::uint32_t>(n), iv,
                             ct, aad, text, error);
    if (ok) {
        from_fp = s.peer_fp;
        ok = save_session(wallet, s, error);
    }
    s.wipe_all();
    return ok;
}

// ------------------------------------------------------------ relay-backed

bool Messenger::start(const std::string& wallet, const std::string& peer, const std::string& text,
                      std::uint64_t ttl, std::string& sid, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    if (relay_ == nullptr) { error = "pm relay disabled"; return false; }
    KeyPackage kp;
    if (!lookup(peer, kp, error)) return false;
    Bytes frame;
    if (!start_frame(wallet, kp, text, sid, frame, error)) return false;
    std::string id;
    return relay_->send_frame(bytes_to_hex(kp.relay_pub), std::string(frame.begin(), frame.end()), ttl, id, error);
}

bool Messenger::send(const std::string& wallet, const std::string& sid, const std::string& text,
                     std::uint64_t ttl, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    if (relay_ == nullptr) { error = "pm relay disabled"; return false; }
    Session s;
    if (!load_session(wallet, sid, s, error)) return false;
    const auto peer_relay = bytes_to_hex(s.peer_relay);
    s.wipe_all();
    Bytes frame;
    if (!send_frame(wallet, sid, text, frame, error)) return false;
    std::string id;
    return relay_->send_frame(peer_relay, std::string(frame.begin(), frame.end()), ttl, id, error);
}

bool Messenger::receive(const std::string& wallet, std::string& out, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    if (relay_ == nullptr) { error = "pm relay disabled"; return false; }
    std::vector<std::pair<std::string, std::string>> items;
    if (!relay_->read_all(wallet, items, error)) return false;
    // INIT frames first so follow-up frames of a new session can open.
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        return (a.second.rfind("AMI1", 0) == 0) > (b.second.rfind("AMI1", 0) == 0);
    });
    std::ostringstream o;
    std::size_t got = 0, pending = 0;
    for (int pass = 0; pass < 3; ++pass) {
        for (auto& [id, plain] : items) {
            if (id.empty()) continue;
            if (plain.rfind("AMI1", 0) != 0 && plain.rfind("AMM1", 0) != 0) { id.clear(); continue; }
            std::string sid, fp, text, err;
            const Bytes frame(plain.begin(), plain.end());
            if (open_frame(wallet, frame, sid, fp, text, err)) {
                o << "\nsession=" << sid << " from=" << fp << " text=" << text;
                ++got;
            } else if (err == "unknown session" || err.rfind("out-of-order", 0) == 0) {
                continue;  // retried next pass / next call
            } else {
                o << "\nrejected msg_id=" << id << " (" << err << ")";
            }
            std::string derr;
            relay_->remove(wallet, id, derr);
            id.clear();
        }
    }
    for (auto& [id, plain] : items) {
        if (!id.empty()) ++pending;
        OPENSSL_cleanse(plain.data(), plain.size());
    }
    out = "received=" + std::to_string(got) + " deferred=" + std::to_string(pending) + o.str();
    return true;
}

bool Messenger::sessions(const std::string& wallet, std::string& out, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    if (!safe_name(wallet)) { error = "invalid wallet name"; return false; }
    std::ostringstream o;
    std::size_t n = 0;
    std::error_code ec;
    const auto d = std::filesystem::path(dir_) / "sessions" / wallet;
    if (std::filesystem::exists(d, ec)) {
        for (const auto& ent : std::filesystem::directory_iterator(d, ec)) {
            if (ent.path().extension() != ".s") continue;
            Session s;
            std::string err;
            if (!load_session(wallet, ent.path().stem().string(), s, err)) continue;
            const auto a = std::min(s.my_fp, s.peer_fp), b = std::max(s.my_fp, s.peer_fp);
            o << "\n" << s.sid << " peer=" << s.peer_fp << " safety=" << h32s("AMLS1-safety|" + a + "|" + b)
              << " sent=" << s.ns << " recv=" << s.nr << " next_send_ratchets=" << (s.need_step ? 1 : 0);
            s.wipe_all();
            ++n;
        }
    }
    out = "sessions=" + std::to_string(n) + o.str();
    return true;
}

} // namespace addition::pqmsg
