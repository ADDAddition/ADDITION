#include "addition/crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <oqs/oqs.h>

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

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
    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87);
    if (sig == nullptr) {
        error = "OQS_SIG_new failed for ml-dsa-87";
        return false;
    }

    if (secret_key.size() != sig->length_secret_key) {
        error = "secret key size mismatch";
        OQS_SIG_free(sig);
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
    OQS_SIG_free(sig);

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
    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87);
    if (sig == nullptr) {
        error = "OQS_SIG_new failed for ml-dsa-87";
        return false;
    }

    if (public_key.size() != sig->length_public_key) {
        error = "public key size mismatch";
        OQS_SIG_free(sig);
        return false;
    }
    if (signature.empty() || signature.size() > sig->length_signature) {
        error = "signature size mismatch";
        OQS_SIG_free(sig);
        return false;
    }

    const auto rc = OQS_SIG_verify(sig,
                                   reinterpret_cast<const std::uint8_t*>(message.data()),
                                   message.size(),
                                   signature.data(),
                                   signature.size(),
                                   public_key.data());
    OQS_SIG_free(sig);

    if (rc != OQS_SUCCESS) {
        error = "OQS_SIG_verify failed";
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
