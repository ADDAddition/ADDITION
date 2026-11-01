#include "addition/htlc_onion.hpp"

#include "addition/crypto.hpp"

#include <oqs/oqs.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cstring>

namespace addition::onion {
namespace {

Bytes kdf(const Bytes& ss, const char* label, std::size_t n = 32) {
    Bytes out(64);
    unsigned int len = 0;
    HMAC(EVP_sha3_512(), ss.data(), static_cast<int>(ss.size()), reinterpret_cast<const unsigned char*>(label),
         std::strlen(label), out.data(), &len);
    out.resize(n);
    return out;
}

Bytes stream(const Bytes& key, std::size_t n) {
    Bytes out(n, 0), zero(n, 0);
    unsigned char iv[16] = {0};
    EVP_CIPHER_CTX* c = EVP_CIPHER_CTX_new();
    int l = 0;
    EVP_EncryptInit_ex(c, EVP_aes_256_ctr(), nullptr, key.data(), iv);
    EVP_EncryptUpdate(c, out.data(), &l, zero.data(), static_cast<int>(n));
    EVP_CIPHER_CTX_free(c);
    return out;
}

Bytes mac(const Bytes& key, const Bytes& routing, const std::string& assoc) {
    Bytes msg(routing);
    msg.insert(msg.end(), assoc.begin(), assoc.end());
    Bytes out(64);
    unsigned int len = 0;
    HMAC(EVP_sha3_512(), key.data(), static_cast<int>(key.size()), msg.data(), msg.size(), out.data(), &len);
    out.resize(kMac);
    return out;
}

void put64(std::uint8_t* p, std::uint64_t v) { for (int i = 7; i >= 0; --i) { p[i] = v & 0xff; v >>= 8; } }
std::uint64_t get64(const std::uint8_t* p) { std::uint64_t v = 0; for (int i = 0; i < 8; ++i) v = (v << 8) | p[i]; return v; }

bool encaps(const Bytes& pk, Bytes& ct, Bytes& ss) {
    OQS_KEM* k = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!k) return false;
    bool ok = pk.size() == k->length_public_key;
    if (ok) {
        ct.assign(k->length_ciphertext, 0);
        ss.assign(k->length_shared_secret, 0);
        ok = OQS_KEM_encaps(k, ct.data(), ss.data(), pk.data()) == OQS_SUCCESS;
    }
    OQS_KEM_free(k);
    return ok;
}

}  // namespace

bool keygen(Bytes& pk, Bytes& sk) {
    OQS_KEM* k = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!k) return false;
    pk.assign(k->length_public_key, 0);
    sk.assign(k->length_secret_key, 0);
    const bool ok = OQS_KEM_keypair(k, pk.data(), sk.data()) == OQS_SUCCESS;
    OQS_KEM_free(k);
    return ok;
}

bool build(const std::vector<Hop>& hops, const std::string& assoc, Bytes& packet, std::string& error) {
    const std::size_t n = hops.size();
    if (n == 0 || n > kMaxHops) { error = "onion: 1..4 hops"; return false; }
    std::vector<Bytes> ct(n), rho(n), mu(n), ks(n);
    for (std::size_t i = 0; i < n; ++i) {
        Bytes ss;
        if (!encaps(hops[i].pubkey, ct[i], ss)) { error = "onion: bad route key for hop " + std::to_string(i); return false; }
        if (hops[i].next_chan.size() > 32 || (i + 1 < n) == hops[i].next_chan.empty()) { error = "onion: bad hop channel"; return false; }
        rho[i] = kdf(ss, "ADD-ONION rho");
        mu[i] = kdf(ss, "ADD-ONION mu");
        ks[i] = stream(rho[i], kRouting + kEntry);
        OPENSSL_cleanse(ss.data(), ss.size());
    }
    // Filler: the tail bytes that earlier hops' shifts leave in front of the last hop.
    Bytes filler;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const std::size_t L = filler.size();
        filler.resize(L + kEntry, 0);
        for (std::size_t j = 0; j < L + kEntry; ++j) filler[j] ^= ks[i][kRouting - L + j];
    }
    auto header = [&](std::size_t i) {
        Bytes h(kHdr, 0);
        h[0] = (i + 1 == n) ? 1 : 0;
        std::memcpy(h.data() + 1, hops[i].next_chan.data(), hops[i].next_chan.size());
        put64(h.data() + 33, hops[i].amount);
        put64(h.data() + 41, hops[i].expiry);
        return h;
    };
    // Last hop: entry || random padding, encrypted, then the filler tail.
    Bytes r(kRouting, 0);
    {
        Bytes plain = header(n - 1);
        plain.resize(kEntry, 0);
        Bytes pad(kRouting - filler.size() - kEntry);
        RAND_bytes(pad.data(), static_cast<int>(pad.size()));
        plain.insert(plain.end(), pad.begin(), pad.end());
        for (std::size_t j = 0; j < plain.size(); ++j) r[j] = plain[j] ^ ks[n - 1][j];
        std::memcpy(r.data() + plain.size(), filler.data(), filler.size());
    }
    Bytes m = mac(mu[n - 1], r, assoc);
    for (std::size_t k = n - 1; k-- > 0;) {
        Bytes buf = header(k);
        buf.insert(buf.end(), m.begin(), m.end());
        buf.insert(buf.end(), ct[k + 1].begin(), ct[k + 1].end());
        buf.insert(buf.end(), r.begin(), r.end());  // kEntry + kRouting
        for (std::size_t j = 0; j < kRouting; ++j) r[j] = buf[j] ^ ks[k][j];
        m = mac(mu[k], r, assoc);
    }
    packet = ct[0];
    packet.insert(packet.end(), r.begin(), r.end());
    packet.insert(packet.end(), m.begin(), m.end());
    return true;
}

bool peel(const Bytes& sk, const Bytes& packet, const std::string& assoc, Peeled& out, std::string& error) {
    if (packet.size() != kPacket) { error = "onion: bad size"; return false; }
    OQS_KEM* k = OQS_KEM_new(OQS_KEM_alg_ml_kem_1024);
    if (!k || sk.size() != k->length_secret_key) { if (k) OQS_KEM_free(k); error = "onion: bad key"; return false; }
    Bytes ss(k->length_shared_secret);
    const bool ok = OQS_KEM_decaps(k, ss.data(), packet.data(), sk.data()) == OQS_SUCCESS;
    OQS_KEM_free(k);
    if (!ok) { error = "onion: decaps failed"; return false; }
    const Bytes rho = kdf(ss, "ADD-ONION rho"), mu = kdf(ss, "ADD-ONION mu");
    out.replay_tag = bytes_to_hex(kdf(ss, "ADD-ONION replay", 16));
    OPENSSL_cleanse(ss.data(), ss.size());
    const Bytes r(packet.begin() + kKemCt, packet.begin() + kKemCt + kRouting);
    const Bytes m(packet.end() - kMac, packet.end());
    const Bytes want = mac(mu, r, assoc);
    if (CRYPTO_memcmp(want.data(), m.data(), kMac) != 0) { error = "onion: bad mac"; return false; }
    const Bytes ks = stream(rho, kRouting + kEntry);
    Bytes buf(r);
    buf.resize(kRouting + kEntry, 0);
    for (std::size_t j = 0; j < buf.size(); ++j) buf[j] ^= ks[j];
    out.final = buf[0] == 1;
    if (buf[0] > 1) { error = "onion: bad header"; return false; }
    const char* c = reinterpret_cast<const char*>(buf.data() + 1);
    out.next_chan.assign(c, strnlen(c, 32));
    out.amount = get64(buf.data() + 33);
    out.expiry = get64(buf.data() + 41);
    out.next_packet.clear();
    if (!out.final) {
        if (out.next_chan.empty()) { error = "onion: missing next channel"; return false; }
        out.next_packet.assign(buf.begin() + kHdr + kMac, buf.begin() + kEntry);       // next KEM ct
        out.next_packet.insert(out.next_packet.end(), buf.begin() + kEntry, buf.end());  // shifted routing
        out.next_packet.insert(out.next_packet.end(), buf.begin() + kHdr, buf.begin() + kHdr + kMac);
    }
    return true;
}

}  // namespace addition::onion
