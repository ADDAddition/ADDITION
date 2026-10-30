#include "addition/pm_relay.hpp"

#include "addition/crypto.hpp"
#include "addition/pq_session.hpp"

#include <oqs/oqs.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace addition {
namespace {

std::uint64_t now_sec() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::vector<std::uint8_t> derive(const std::vector<std::uint8_t>& ss, const char* label) {
    std::string pre = std::string("ADDITION-PM-v1|") + label + "|";
    pre.append(reinterpret_cast<const char*>(ss.data()), ss.size());
    const auto h = sha3_512_bytes(pre);
    return std::vector<std::uint8_t>(h.begin(), h.begin() + 32);
}

std::string mailbox_of(const std::vector<std::uint8_t>& pk) {
    return to_hex(sha3_512_bytes(pk)).substr(0, 32);
}

bool is_hex(const std::string& s, std::size_t min, std::size_t max) {
    if (s.size() < min || s.size() > max || (s.size() % 2) != 0) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool safe_name(const std::string& w) {
    if (w.empty() || w.size() > 64) return false;
    return std::all_of(w.begin(), w.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.';
    }) && w[0] != '.';
}

std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, d)) out.push_back(cur);
    return out;
}

bool aes_gcm(bool enc, const std::vector<std::uint8_t>& key, const std::vector<std::uint8_t>& iv,
             const std::string& aad, const std::vector<std::uint8_t>& in,
             std::vector<std::uint8_t>& out) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;
    bool ok = false;
    int len = 0;
    std::vector<std::uint8_t> body = in;
    std::uint8_t tag[16];
    if (!enc) {
        if (body.size() < 16) { EVP_CIPHER_CTX_free(ctx); return false; }
        std::copy(body.end() - 16, body.end(), tag);
        body.resize(body.size() - 16);
    }
    out.assign(body.size() + 16, 0);
    do {
        if (EVP_CipherInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr, enc ? 1 : 0) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1) break;
        if (EVP_CipherInit_ex(ctx, nullptr, nullptr, key.data(), iv.data(), enc ? 1 : 0) != 1) break;
        if (EVP_CipherUpdate(ctx, nullptr, &len, reinterpret_cast<const unsigned char*>(aad.data()),
                             static_cast<int>(aad.size())) != 1) break;
        if (EVP_CipherUpdate(ctx, out.data(), &len, body.data(), static_cast<int>(body.size())) != 1) break;
        int total = len;
        if (!enc && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) != 1) break;
        if (EVP_CipherFinal_ex(ctx, out.data() + total, &len) != 1) break;
        total += len;
        if (enc) {
            if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, out.data() + total) != 1) break;
            total += 16;
        }
        out.resize(static_cast<std::size_t>(total));
        ok = true;
    } while (false);
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) OPENSSL_cleanse(out.data(), out.size()), out.clear();
    return ok;
}

std::string aad_of(const std::string& mailbox, std::uint64_t exp, const std::string& del_hash,
                   const std::string& id) {
    return "PMSG|v1|" + id + "|" + mailbox + "|" + std::to_string(exp) + "|" + del_hash;
}

} // namespace

std::string PmRelay::Env::wire() const {
    return "PMSG|v1|" + id + "|" + mailbox + "|" + std::to_string(expire_at) + "|" + del_hash + "|" +
           kem_ct + "|" + iv + "|" + ct;
}

PmRelay::PmRelay(std::string key_dir) : key_dir_(std::move(key_dir)) {}

void PmRelay::set_key_dir(std::string dir) {
    std::lock_guard<std::mutex> g(mu_);
    key_dir_ = std::move(dir);
}

bool PmRelay::load_key_locked(const std::string& wallet, Keys& k, bool create, std::string& error) {
    if (!safe_name(wallet)) { error = "invalid wallet name"; return false; }
    if (key_dir_.empty()) { error = "message key dir not configured"; return false; }
    const auto path = std::filesystem::path(key_dir_) / (wallet + ".pmkey");
    std::ifstream in(path);
    if (in) {
        std::string pk_hex, sk_hex;
        in >> pk_hex >> sk_hex;
        if (!hex_to_bytes(pk_hex, k.pk, error) || !hex_to_bytes(sk_hex, k.sk, error)) {
            error = "corrupt message key";
            return false;
        }
        k.mailbox = mailbox_of(k.pk);
        return true;
    }
    if (!create) { error = "no message key for wallet (run msg_key)"; return false; }
    OQS_KEM* kem = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!kem) { error = "ML-KEM-1024 unavailable"; return false; }
    k.pk.assign(kem->length_public_key, 0);
    k.sk.assign(kem->length_secret_key, 0);
    const bool ok = OQS_KEM_keypair(kem, k.pk.data(), k.sk.data()) == OQS_SUCCESS;
    OQS_KEM_free(kem);
    if (!ok) { error = "keygen failed"; return false; }
    std::error_code ec;
    std::filesystem::create_directories(key_dir_, ec);
    ::chmod(key_dir_.c_str(), 0700);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        ::chmod(tmp.c_str(), 0600);
        out << bytes_to_hex(k.pk) << "\n" << bytes_to_hex(k.sk) << "\n";
        if (!out) { error = "cannot write message key"; return false; }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { error = "cannot write message key"; return false; }
    k.mailbox = mailbox_of(k.pk);
    return true;
}

bool PmRelay::ensure_key(const std::string& wallet, std::string& pubkey_hex, std::string& mailbox,
                         std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    Keys k;
    if (!load_key_locked(wallet, k, true, error)) return false;
    pubkey_hex = bytes_to_hex(k.pk);
    mailbox = k.mailbox;
    OPENSSL_cleanse(k.sk.data(), k.sk.size());
    return true;
}

bool PmRelay::send(const std::string& recipient_pk_hex, const std::string& plaintext,
                   std::uint64_t ttl_sec, std::string& msg_id, std::string& error) {
    return send_impl(recipient_pk_hex, plaintext, kMaxPlaintext, ttl_sec, msg_id, error);
}

bool PmRelay::send_frame(const std::string& recipient_pk_hex, const std::string& frame,
                         std::uint64_t ttl_sec, std::string& msg_id, std::string& error) {
    return send_impl(recipient_pk_hex, frame, kMaxFrame, ttl_sec, msg_id, error);
}

bool PmRelay::send_impl(const std::string& recipient_pk_hex, const std::string& plaintext, std::size_t max,
                        std::uint64_t ttl_sec, std::string& msg_id, std::string& error) {
    if (plaintext.empty() || plaintext.size() > max) { error = "message must be 1.." + std::to_string(max) + " bytes"; return false; }
    ttl_sec = std::clamp<std::uint64_t>(ttl_sec, kMinTtl, kMaxTtl);
    std::vector<std::uint8_t> pk;
    if (!hex_to_bytes(recipient_pk_hex, pk, error)) { error = "invalid recipient key"; return false; }
    OQS_KEM* kem = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!kem) { error = "ML-KEM-1024 unavailable"; return false; }
    if (pk.size() != kem->length_public_key) { OQS_KEM_free(kem); error = "invalid recipient key size"; return false; }
    std::vector<std::uint8_t> ct(kem->length_ciphertext), ss(kem->length_shared_secret);
    const bool ok = OQS_KEM_encaps(kem, ct.data(), ss.data(), pk.data()) == OQS_SUCCESS;
    OQS_KEM_free(kem);
    if (!ok) { error = "encapsulation failed"; return false; }

    Env e;
    std::vector<std::uint8_t> idb(16), iv(12);
    RAND_bytes(idb.data(), 16);
    RAND_bytes(iv.data(), 12);
    e.id = bytes_to_hex(idb);
    e.mailbox = mailbox_of(pk);
    e.expire_at = now_sec() + ttl_sec;
    const auto key = derive(ss, "key");
    const auto tok = derive(ss, "del");
    OPENSSL_cleanse(ss.data(), ss.size());
    e.del_hash = to_hex(sha3_512_bytes(tok)).substr(0, 64);
    // Length-hiding framing: u16 length || text || zero pad to 256-byte multiple.
    std::vector<std::uint8_t> framed;
    framed.push_back(static_cast<std::uint8_t>(plaintext.size() >> 8));
    framed.push_back(static_cast<std::uint8_t>(plaintext.size() & 0xff));
    framed.insert(framed.end(), plaintext.begin(), plaintext.end());
    framed.resize(((framed.size() + 255) / 256) * 256, 0);
    std::vector<std::uint8_t> sealed;
    if (!aes_gcm(true, key, iv, aad_of(e.mailbox, e.expire_at, e.del_hash, e.id), framed, sealed)) {
        error = "encryption failed";
        return false;
    }
    OPENSSL_cleanse(framed.data(), framed.size());
    e.kem_ct = bytes_to_hex(ct);
    e.iv = bytes_to_hex(iv);
    e.ct = bytes_to_hex(sealed);
    msg_id = e.id;
    std::lock_guard<std::mutex> g(mu_);
    bool dup = false;
    return store_locked(std::move(e), dup, error);
}

void PmRelay::purge_locked(std::uint64_t now) {
    for (auto it = pkgs_.begin(); it != pkgs_.end();) {
        if (it->second.expires <= now) it = pkgs_.erase(it); else ++it;
    }
    for (auto it = envs_.begin(); it != envs_.end();) {
        if (it->second.expire_at <= now) {
            bytes_ -= it->second.ct.size() + it->second.kem_ct.size();
            it = envs_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = tombstones_.begin(); it != tombstones_.end();) {
        if (it->second <= now) {
            del_lines_.erase(it->first);
            it = tombstones_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& [peer, ids] : delivered_) {
        for (auto it = ids.begin(); it != ids.end();) {
            const auto raw = it->size() > 2 ? it->substr(2) : *it;
            if (!envs_.count(raw) && !tombstones_.count(raw)) it = ids.erase(it); else ++it;
        }
    }
}

bool PmRelay::store_locked(Env e, bool& duplicate, std::string& error) {
    const auto now = now_sec();
    purge_locked(now);
    duplicate = envs_.count(e.id) != 0 || tombstones_.count(e.id) != 0;
    if (duplicate) return true;
    if (e.expire_at <= now) { error = "expired"; return false; }
    if (e.expire_at > now + kMaxTtl + 300) { error = "ttl too long"; return false; }
    const auto sz = e.ct.size() + e.kem_ct.size();
    if (envs_.size() >= kMaxMessages || bytes_ + sz > kMaxStoreBytes) { error = "relay store full"; return false; }
    bytes_ += sz;
    envs_.emplace(e.id, std::move(e));
    return true;
}

bool PmRelay::rate_ok_locked(const std::string& peer) {
    const auto now = now_sec();
    auto& q = rate_[peer];
    while (!q.empty() && q.front() + 60 <= now) q.pop_front();
    if (q.size() >= kPeerRatePerMin) return false;
    q.push_back(now);
    if (rate_.size() > 1024) rate_.clear();
    return true;
}

bool PmRelay::accept_wire(const std::string& peer, const std::string& body, bool& duplicate,
                          std::string& error) {
    duplicate = false;
    const auto f = split(body, '|');
    // v1|id|mailbox|exp|del_hash|kem_ct|iv|ct
    if (f.size() != 8 || f[0] != "v1") { error = "bad pm envelope"; return false; }
    Env e;
    e.id = f[1]; e.mailbox = f[2]; e.del_hash = f[4]; e.kem_ct = f[5]; e.iv = f[6]; e.ct = f[7];
    try { e.expire_at = std::stoull(f[3]); } catch (...) { error = "bad pm envelope"; return false; }
    if (!is_hex(e.id, 32, 32) || !is_hex(e.mailbox, 32, 32) || !is_hex(e.del_hash, 64, 64) ||
        !is_hex(e.kem_ct, 3136, 3136) || !is_hex(e.iv, 24, 24) ||
        !is_hex(e.ct, 2 * (256 + 16), 2 * (((kMaxFrame + 2 + 255) / 256) * 256 + 16)) || ((e.ct.size() / 2 - 16) % 256) != 0) {
        error = "bad pm envelope";
        return false;
    }
    std::lock_guard<std::mutex> g(mu_);
    if (envs_.count(e.id) || tombstones_.count(e.id)) { duplicate = true; return true; }
    if (!rate_ok_locked(peer)) { error = "pm rate limit"; return false; }
    const bool ok = store_locked(std::move(e), duplicate, error);
    if (ok) delivered_[peer].insert("m:" + f[1]);  // never echo back to the sender
    return ok;
}

bool PmRelay::accept_delete(const std::string& peer, const std::string& body, bool& known,
                            std::string& error) {
    known = false;
    const auto f = split(body, '|');
    if (f.size() != 2 || !is_hex(f[0], 32, 32) || !is_hex(f[1], 64, 64)) { error = "bad pm delete"; return false; }
    std::lock_guard<std::mutex> g(mu_);
    if (tombstones_.count(f[0])) { known = true; return true; }
    auto it = envs_.find(f[0]);
    if (it == envs_.end()) { known = true; return true; }  // unknown id: nothing to do, don't relay
    std::vector<std::uint8_t> tok;
    if (!hex_to_bytes(f[1], tok, error) || to_hex(sha3_512_bytes(tok)).substr(0, 64) != it->second.del_hash) {
        error = "pm delete token mismatch";
        return false;
    }
    tombstones_[f[0]] = it->second.expire_at;
    bytes_ -= it->second.ct.size() + it->second.kem_ct.size();
    envs_.erase(it);
    del_lines_[f[0]] = "PMDEL|" + f[0] + "|" + f[1];
    delivered_[peer].insert("d:" + f[0]);
    return true;
}

bool PmRelay::decrypt_locked(const Env& e, const Keys& k, std::string& plain, std::string& token_hex,
                             std::string& error) const {
    OQS_KEM* kem = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!kem) { error = "ML-KEM-1024 unavailable"; return false; }
    std::vector<std::uint8_t> ct, ss(kem->length_shared_secret), iv, sealed, framed;
    bool ok = hex_to_bytes(e.kem_ct, ct, error) && ct.size() == kem->length_ciphertext &&
              OQS_KEM_decaps(kem, ss.data(), ct.data(), k.sk.data()) == OQS_SUCCESS;
    OQS_KEM_free(kem);
    if (!ok) { error = "decapsulation failed"; return false; }
    const auto key = derive(ss, "key");
    const auto tok = derive(ss, "del");
    OPENSSL_cleanse(ss.data(), ss.size());
    if (!hex_to_bytes(e.iv, iv, error) || !hex_to_bytes(e.ct, sealed, error) ||
        !aes_gcm(false, key, iv, aad_of(e.mailbox, e.expire_at, e.del_hash, e.id), sealed, framed) ||
        framed.size() < 2) {
        error = "decryption failed";
        return false;
    }
    const std::size_t n = (static_cast<std::size_t>(framed[0]) << 8) | framed[1];
    if (n + 2 > framed.size()) { error = "decryption failed"; return false; }
    plain.assign(reinterpret_cast<const char*>(framed.data() + 2), n);
    OPENSSL_cleanse(framed.data(), framed.size());
    token_hex = bytes_to_hex(tok);
    return true;
}

bool PmRelay::inbox(const std::string& wallet, std::string& out, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    Keys k;
    if (!load_key_locked(wallet, k, false, error)) return false;
    purge_locked(now_sec());
    std::ostringstream os;
    std::size_t n = 0;
    for (const auto& [id, e] : envs_) {
        if (e.mailbox != k.mailbox) continue;
        std::string plain, tok, err;
        if (!decrypt_locked(e, k, plain, tok, err)) continue;
        os << "\n" << id << " expires=" << e.expire_at << " bytes=" << plain.size();
        ++n;
    }
    OPENSSL_cleanse(k.sk.data(), k.sk.size());
    out = "messages=" + std::to_string(n) + " mailbox=" + k.mailbox + os.str();
    return true;
}

bool PmRelay::read(const std::string& wallet, const std::string& msg_id, std::string& out,
                   std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    Keys k;
    if (!load_key_locked(wallet, k, false, error)) return false;
    auto it = envs_.find(msg_id);
    if (it == envs_.end() || it->second.mailbox != k.mailbox || it->second.expire_at <= now_sec()) {
        error = "message not found";
        return false;
    }
    std::string tok;
    const bool ok = decrypt_locked(it->second, k, out, tok, error);
    OPENSSL_cleanse(k.sk.data(), k.sk.size());
    return ok;
}

bool PmRelay::remove(const std::string& wallet, const std::string& msg_id, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    Keys k;
    if (!load_key_locked(wallet, k, false, error)) return false;
    auto it = envs_.find(msg_id);
    if (it == envs_.end() || it->second.mailbox != k.mailbox) { error = "message not found"; return false; }
    std::string plain, tok;
    const bool ok = decrypt_locked(it->second, k, plain, tok, error);
    OPENSSL_cleanse(k.sk.data(), k.sk.size());
    OPENSSL_cleanse(plain.data(), plain.size());
    if (!ok) return false;
    tombstones_[msg_id] = it->second.expire_at;
    bytes_ -= it->second.ct.size() + it->second.kem_ct.size();
    envs_.erase(it);
    del_lines_[msg_id] = "PMDEL|" + msg_id + "|" + tok;
    return true;
}

std::vector<std::string> PmRelay::undelivered_for(const std::string& peer, std::size_t max) {
    std::lock_guard<std::mutex> g(mu_);
    purge_locked(now_sec());
    std::vector<std::string> out;
    const auto& done = delivered_[peer];
    for (const auto& [id, line] : del_lines_) {
        if (out.size() >= max) break;
        if (!done.count("d:" + id)) out.push_back(line);
    }
    for (const auto& [id, e] : envs_) {
        if (out.size() >= max) break;
        if (!done.count("m:" + id)) out.push_back(e.wire());
    }
    for (const auto& [idx, p] : pkgs_) {
        if (out.size() >= max) break;
        if (!done.count(p.line_id)) out.push_back("KPKG|v1|" + p.hex);
    }
    return out;
}

std::vector<std::string> PmRelay::fetch_mailbox(const std::string& mailbox, std::size_t max) {
    std::lock_guard<std::mutex> g(mu_);
    purge_locked(now_sec());
    std::vector<std::pair<std::uint64_t, std::string>> v;
    for (const auto& [id, e] : envs_)
        if (e.mailbox == mailbox) v.emplace_back(e.expire_at, e.wire());
    std::sort(v.begin(), v.end());
    std::vector<std::string> out;
    for (auto& x : v) { if (out.size() >= max) break; out.push_back(std::move(x.second)); }
    return out;
}

bool PmRelay::has_undelivered(const std::string& peer) {
    return !undelivered_for(peer, 1).empty();
}

void PmRelay::mark_delivered(const std::string& peer, const std::string& line) {
    std::string id;
    if (line.rfind("PMSG|v1|", 0) == 0) id = "m:" + line.substr(8, 32);
    else if (line.rfind("PMDEL|", 0) == 0) id = "d:" + line.substr(6, 32);
    else if (line.rfind("KPKG|v1|", 0) == 0) id = "k:" + to_hex(sha3_512_bytes(line.substr(8))).substr(0, 32);
    if (id.empty()) return;
    std::lock_guard<std::mutex> g(mu_);
    delivered_[peer].insert(id);
}

bool PmRelay::put_package_locked(const std::string& pkg_hex, bool& duplicate, std::string& error) {
    duplicate = false;
    pqmsg::KeyPackage kp;
    if (!pqmsg::KeyPackage::from_hex(pkg_hex, kp, error) || !kp.verify(now_sec(), error)) return false;
    if (kp.index == "-") { error = "unlisted key package"; return false; }
    const auto ident = pqmsg::fingerprint(kp.identity_pub);
    auto it = pkgs_.find(kp.index);
    if (it != pkgs_.end() && it->second.expires > now_sec()) {
        if (it->second.identity != ident) { error = "index owned by another identity"; return false; }
        if (kp.created <= it->second.created) { duplicate = true; return true; }
    } else if (it == pkgs_.end() && pkgs_.size() >= kMaxPackages) {
        error = "key-package directory full";
        return false;
    }
    Pkg p;
    p.hex = pkg_hex;
    p.line_id = "k:" + to_hex(sha3_512_bytes(pkg_hex)).substr(0, 32);
    p.identity = ident;
    p.created = kp.created;
    p.expires = kp.expires;
    pkgs_[kp.index] = std::move(p);
    return true;
}

bool PmRelay::publish_package(const std::string& pkg_hex, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    bool dup = false;
    return put_package_locked(pkg_hex, dup, error);
}

bool PmRelay::accept_package(const std::string& peer, const std::string& body, bool& duplicate,
                             std::string& error) {
    if (body.rfind("v1|", 0) != 0 || body.size() > 64000) { error = "bad key package line"; return false; }
    const auto hex = body.substr(3);
    std::lock_guard<std::mutex> g(mu_);
    if (!rate_ok_locked(peer)) { error = "pm rate limit"; return false; }
    const bool ok = put_package_locked(hex, duplicate, error);
    if (ok) delivered_[peer].insert("k:" + to_hex(sha3_512_bytes(hex)).substr(0, 32));
    return ok;
}

bool PmRelay::find_package(const std::string& index, std::string& pkg_hex, std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    auto it = pkgs_.find(index);
    if (it == pkgs_.end() || it->second.expires <= now_sec()) { error = "no key package for that handle"; return false; }
    pkg_hex = it->second.hex;
    return true;
}

bool PmRelay::read_all(const std::string& wallet, std::vector<std::pair<std::string, std::string>>& out,
                       std::string& error) {
    std::lock_guard<std::mutex> g(mu_);
    Keys k;
    if (!load_key_locked(wallet, k, false, error)) return false;
    purge_locked(now_sec());
    for (const auto& [id, e] : envs_) {
        if (e.mailbox != k.mailbox) continue;
        std::string plain, tok, err;
        if (decrypt_locked(e, k, plain, tok, err)) out.emplace_back(id, std::move(plain));
    }
    OPENSSL_cleanse(k.sk.data(), k.sk.size());
    return true;
}

std::size_t PmRelay::purge_expired() {
    std::lock_guard<std::mutex> g(mu_);
    const auto before = envs_.size();
    purge_locked(now_sec());
    return before - envs_.size();
}

std::string PmRelay::status() {
    std::lock_guard<std::mutex> g(mu_);
    purge_locked(now_sec());
    return "pm_relay stored=" + std::to_string(envs_.size()) + " bytes=" + std::to_string(bytes_) +
           " tombstones=" + std::to_string(tombstones_.size()) + " key_packages=" + std::to_string(pkgs_.size()) + " max_messages=" +
           std::to_string(kMaxMessages) + " kem=ML-KEM-1024 aead=AES-256-GCM";
}

} // namespace addition
