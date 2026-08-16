#include "addition/crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <oqs/oqs.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace addition {
namespace {

struct PqKeySizes {
    std::size_t public_key{0};
    std::size_t secret_key{0};
    std::size_t signature{0};
};

bool get_ml_dsa_87_sizes(PqKeySizes& out) {
    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87);
    if (sig == nullptr) {
        return false;
    }
    out.public_key = sig->length_public_key;
    out.secret_key = sig->length_secret_key;
    out.signature = sig->length_signature;
    OQS_SIG_free(sig);
    return true;
}

bool is_hex_char(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

struct TlsOqsSig {
    OQS_SIG* sig{nullptr};
    TlsOqsSig() { sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87); }
    ~TlsOqsSig() {
        if (sig != nullptr) {
            OQS_SIG_free(sig);
        }
    }
    TlsOqsSig(const TlsOqsSig&) = delete;
    TlsOqsSig& operator=(const TlsOqsSig&) = delete;
};

OQS_SIG* tls_ml_dsa_87() {
    thread_local TlsOqsSig holder;
    return holder.sig;
}

bool is_hex_strict(const std::string& hex, std::size_t max_hex_len, std::string& error) {
    if (hex.empty()) {
        error = "empty hex";
        return false;
    }
    if (hex.size() > max_hex_len) {
        error = "hex too large";
        return false;
    }
    if ((hex.size() % 2) != 0) {
        error = "invalid hex length";
        return false;
    }
    for (char c : hex) {
        if (!is_hex_char(c)) {
            error = "invalid hex data";
            return false;
        }
    }
    return true;
}

} // namespace

Hash512 sha3_512_bytes(const std::vector<std::uint8_t>& data) {
    EVP_MD_CTX* raw_ctx = EVP_MD_CTX_new();
    if (raw_ctx == nullptr) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }

    Hash512 out{};
    unsigned int out_len = 0;

    const EVP_MD* md = EVP_sha3_512();
    if (md == nullptr) {
        EVP_MD_CTX_free(raw_ctx);
        throw std::runtime_error("EVP_sha3_512 unavailable");
    }

    if (EVP_DigestInit_ex(raw_ctx, md, nullptr) != 1 ||
        EVP_DigestUpdate(raw_ctx, data.data(), data.size()) != 1 ||
        EVP_DigestFinal_ex(raw_ctx, out.data(), &out_len) != 1) {
        EVP_MD_CTX_free(raw_ctx);
        throw std::runtime_error("SHA3-512 digest failed");
    }

    EVP_MD_CTX_free(raw_ctx);

    if (out_len != out.size()) {
        throw std::runtime_error("unexpected SHA3-512 length");
    }

    return out;
}

Hash512 sha3_512_bytes(const std::string& data) {
    std::vector<std::uint8_t> bytes(data.begin(), data.end());
    return sha3_512_bytes(bytes);
}

std::string to_hex(const Hash512& hash) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (const auto b : hash) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (const auto b : bytes) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

bool hex_to_bytes(const std::string& hex,
                  std::vector<std::uint8_t>& out,
                  std::string& error) {
    out.clear();
    if (!is_hex_strict(hex, 131072, error)) {
        return false;
    }
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        try {
            const auto byte = static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
            out.push_back(byte);
        } catch (...) {
            error = "invalid hex data";
            out.clear();
            return false;
        }
    }
    return true;
}

std::string hash_committed_address(const std::string& scheme_id,
                                   const std::vector<std::uint8_t>& pubkey) {
    if (scheme_id.empty() || pubkey.empty()) {
        return {};
    }
    std::vector<std::uint8_t> preimage;
    preimage.reserve(scheme_id.size() + 1 + pubkey.size());
    preimage.insert(preimage.end(), scheme_id.begin(), scheme_id.end());
    preimage.push_back(0);
    preimage.insert(preimage.end(), pubkey.begin(), pubkey.end());
    return to_hex(sha3_512_bytes(preimage));
}

std::string hash_committed_address_hex(const std::string& scheme_id,
                                       const std::string& pubkey_hex) {
    std::vector<std::uint8_t> pk;
    std::string error;
    if (!hex_to_bytes(pubkey_hex, pk, error) || pk.empty()) {
        return {};
    }
    return hash_committed_address(scheme_id, pk);
}

bool address_binds_pubkey(const std::string& address,
                          const std::string& scheme_id,
                          const std::string& pubkey_hex,
                          std::string& error) {
    if (scheme_id.empty()) {
        error = "missing scheme_id";
        return false;
    }
    if (address.size() != kHashCommittedAddressHexLen) {
        error = "address is not a 128-hex hash commitment";
        return false;
    }
    for (char c : address) {
        if (!is_hex_char(c)) {
            error = "address is not a 128-hex hash commitment";
            return false;
        }
    }
    std::vector<std::uint8_t> pk;
    if (!hex_to_bytes(pubkey_hex, pk, error) || pk.empty()) {
        error = "invalid pubkey hex";
        return false;
    }
    const auto derived = hash_committed_address(scheme_id, pk);
    if (derived.empty() || derived != address) {
        error = "signer/pubkey hash-address mismatch";
        return false;
    }
    return true;
}

std::string sign_message_hybrid(const std::string& private_key,
                                const std::string& message,
                                const std::string& pq_context) {
    PqKeySizes sizes{};
    if (!get_ml_dsa_87_sizes(sizes)) {
        throw std::runtime_error("liboqs size query failed");
    }

    std::string err;
    if (!is_hex_strict(private_key, sizes.secret_key * 2, err) || private_key.size() != sizes.secret_key * 2) {
        throw std::runtime_error("invalid private key hex: " + err);
    }

    std::vector<std::uint8_t> pq_sig;
    std::vector<std::uint8_t> sk;
    if (hex_to_bytes(private_key, sk, err) && !sk.empty() && pq_sign_message(sk, message, pq_sig, err)) {
        OQS_MEM_cleanse(sk.data(), sk.size());
        if (pq_sig.empty() || pq_sig.size() > sizes.signature) {
            throw std::runtime_error("liboqs produced invalid signature size");
        }
        return "pq=" + bytes_to_hex(pq_sig);
    }
    if (!sk.empty()) {
        OQS_MEM_cleanse(sk.data(), sk.size());
    }
    throw std::runtime_error("liboqs signing failed: " + err);

    (void)pq_context;
}

bool verify_message_signature_hybrid(const std::string& public_key,
                                     const std::string& message,
                                     const std::string& signature,
                                     const std::string& pq_context) {
    PqKeySizes sizes{};
    if (!get_ml_dsa_87_sizes(sizes)) {
        return false;
    }

    constexpr const char* kPrefix = "pq=";
    if (signature.rfind(kPrefix, 0) != 0) {
        return false;
    }

    std::string err;
    if (!is_hex_strict(public_key, sizes.public_key * 2, err) || public_key.size() != sizes.public_key * 2) {
        return false;
    }
    const auto sig_hex = signature.substr(3);
    if (!is_hex_strict(sig_hex, sizes.signature * 2, err)) {
        return false;
    }

    std::vector<std::uint8_t> sig_bytes;
    if (!hex_to_bytes(sig_hex, sig_bytes, err)) {
        return false;
    }
    if (sig_bytes.empty() || sig_bytes.size() > sizes.signature) {
        return false;
    }

    std::vector<std::uint8_t> pk_bytes;
    if (!hex_to_bytes(public_key, pk_bytes, err)) {
        return false;
    }
    if (pk_bytes.size() != sizes.public_key) {
        return false;
    }

    (void)pq_context;
    return pq_verify_message(pk_bytes, message, sig_bytes, err);
}

bool pq_sign_message(const std::vector<std::uint8_t>& secret_key,
                     const std::string& message,
                     std::vector<std::uint8_t>& signature,
                     std::string& error) {
    OQS_SIG* sig = tls_ml_dsa_87();
    if (sig == nullptr) {
        error = "OQS_SIG_new failed for ml-dsa-87";
        return false;
    }

    if (secret_key.size() != sig->length_secret_key) {
        error = "secret key size mismatch";
        return false;
    }

    signature.assign(sig->length_signature, 0);
    size_t sig_len = 0;
    const auto rc = OQS_SIG_sign(sig,
                                 signature.data(),
                                 &sig_len,
                                 reinterpret_cast<const std::uint8_t*>(message.data()),
                                 message.size(),
                                 secret_key.data());

    if (rc != OQS_SUCCESS) {
        error = "OQS_SIG_sign failed";
        signature.clear();
        return false;
    }

    signature.resize(sig_len);
    return true;
}

bool pq_verify_message(const std::vector<std::uint8_t>& public_key,
                       const std::string& message,
                       const std::vector<std::uint8_t>& signature,
                       std::string& error) {
    OQS_SIG* sig = tls_ml_dsa_87();
    if (sig == nullptr) {
        error = "OQS_SIG_new failed for ml-dsa-87";
        return false;
    }

    if (public_key.size() != sig->length_public_key) {
        error = "public key size mismatch";
        return false;
    }
    if (signature.empty() || signature.size() > sig->length_signature) {
        error = "signature size mismatch";
        return false;
    }

    const auto rc = OQS_SIG_verify(sig,
                                   reinterpret_cast<const std::uint8_t*>(message.data()),
                                   message.size(),
                                   signature.data(),
                                   signature.size(),
                                   public_key.data());

    if (rc != OQS_SUCCESS) {
        error = "OQS_SIG_verify failed";
        return false;
    }

    return true;
}

bool pq_verify_messages_parallel(const std::vector<PqVerifyItem>& items,
                                 std::size_t threads,
                                 std::size_t& accepted,
                                 std::uint64_t& elapsed_ms,
                                 std::string& error) {
    accepted = 0;
    elapsed_ms = 0;
    error.clear();
    if (items.empty()) {
        return true;
    }

    if (threads == 0) {
        const auto hw = std::thread::hardware_concurrency();
        threads = hw > 0 ? static_cast<std::size_t>(hw) : 1;
    }
    if (threads > items.size()) {
        threads = items.size();
    }

    std::atomic<std::size_t> ok{0};
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::string first_error;
    std::mutex err_mu;

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (std::size_t tid = 0; tid < threads; ++tid) {
        workers.emplace_back([&]() {
            while (!failed.load(std::memory_order_relaxed)) {
                const auto i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= items.size()) {
                    return;
                }
                std::string verr;
                if (!verify_message_signature_hybrid(items[i].public_key_hex,
                                                     items[i].message,
                                                     items[i].signature)) {
                    failed.store(true, std::memory_order_relaxed);
                    std::lock_guard<std::mutex> lk(err_mu);
                    if (first_error.empty()) {
                        first_error = "batch pq verify failed at index " + std::to_string(i);
                    }
                    (void)verr;
                    return;
                }
                ok.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& th : workers) {
        if (th.joinable()) {
            th.join();
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    if (elapsed_ms == 0) {
        elapsed_ms = 1;
    }
    accepted = ok.load(std::memory_order_relaxed);
    if (failed.load(std::memory_order_relaxed) || accepted != items.size()) {
        error = first_error.empty() ? "batch pq verify rejected" : first_error;
        return false;
    }
    return true;
}

bool random_hex(std::size_t nbytes, std::string& out, std::string& error) {
    out.clear();
    if (nbytes == 0 || nbytes > 1024) {
        error = "invalid random length";
        return false;
    }
    std::vector<std::uint8_t> buf(nbytes, 0);
    if (RAND_bytes(buf.data(), static_cast<int>(nbytes)) != 1) {
        error = "RAND_bytes failed";
        return false;
    }
    out = bytes_to_hex(buf);
    return true;
}

bool crypto_selftest(std::string& report) {
    report.clear();

    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87);
    if (sig == nullptr) {
        report = "selftest: OQS_SIG_new failed";
        return false;
    }

    std::vector<std::uint8_t> pub(sig->length_public_key, 0);
    std::vector<std::uint8_t> sec(sig->length_secret_key, 0);
    if (OQS_SIG_keypair(sig, pub.data(), sec.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        report = "selftest: keypair failed";
        return false;
    }

    const std::string msg = "addition-crypto-selftest";
    std::vector<std::uint8_t> sig_bytes(sig->length_signature, 0);
    size_t sig_len = 0;
    if (OQS_SIG_sign(sig,
                     sig_bytes.data(),
                     &sig_len,
                     reinterpret_cast<const std::uint8_t*>(msg.data()),
                     msg.size(),
                     sec.data()) != OQS_SUCCESS) {
        OQS_MEM_cleanse(sec.data(), sec.size());
        OQS_SIG_free(sig);
        report = "selftest: sign failed";
        return false;
    }
    sig_bytes.resize(sig_len);

    const auto vr = OQS_SIG_verify(sig,
                                   reinterpret_cast<const std::uint8_t*>(msg.data()),
                                   msg.size(),
                                   sig_bytes.data(),
                                   sig_bytes.size(),
                                   pub.data());

    OQS_MEM_cleanse(sec.data(), sec.size());
    OQS_SIG_free(sig);

    if (vr != OQS_SUCCESS) {
        report = "selftest: verify failed";
        return false;
    }

    report = "selftest: ok";
    return true;
}

} // namespace addition
