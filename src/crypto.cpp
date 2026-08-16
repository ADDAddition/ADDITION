#include "addition/crypto.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <oqs/oqs.h>

#include <cctype>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace addition {
namespace {

std::mutex g_ctx_mu;
std::string g_default_sign_ctx;

struct PqSizes {
    std::size_t public_key{0};
    std::size_t secret_key{0};
    std::size_t signature{0};
};

const char* oqs_name_for(SigScheme scheme) {
    switch (scheme) {
    case SigScheme::MlDsa87:
        return OQS_SIG_alg_ml_dsa_87;
    case SigScheme::SlhDsaShake256s:
        // liboqs 0.12.0 exposes FIPS 205 SLH-DSA-SHAKE-256s as this SPHINCS+ name.
        return OQS_SIG_alg_sphincs_shake_256s_simple;
    case SigScheme::Unknown:
        return "";
    }
    const SigScheme missing = scheme;
    switch (missing) {
    case SigScheme::MlDsa87:
    case SigScheme::SlhDsaShake256s:
    case SigScheme::Unknown:
        break;
    }
    return "";
}

bool get_scheme_sizes(SigScheme scheme, PqSizes& out) {
    const char* name = oqs_name_for(scheme);
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    OQS_SIG* sig = OQS_SIG_new(name);
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

std::string resolve_ctx(const std::string& ctx) {
    if (!ctx.empty()) {
        return ctx;
    }
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    return g_default_sign_ctx;
}

bool ctx_usable(const std::string& ctx, std::string& error) {
    if (ctx.empty()) {
        error = "empty FIPS 204 context rejected";
        return false;
    }
    if (ctx.size() > 255) {
        error = "context exceeds 255 bytes";
        return false;
    }
    return true;
}

bool scheme_supports_ctx_sign(SigScheme scheme) {
    static std::mutex mu;
    static bool ml_cached = false;
    static bool ml_ok = false;
    static bool slh_cached = false;
    static bool slh_ok = false;

    bool* cached = nullptr;
    bool* ok = nullptr;
    switch (scheme) {
    case SigScheme::MlDsa87:
        cached = &ml_cached;
        ok = &ml_ok;
        break;
    case SigScheme::SlhDsaShake256s:
        cached = &slh_cached;
        ok = &slh_ok;
        break;
    case SigScheme::Unknown:
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mu);
        if (*cached) {
            return *ok;
        }
    }

    const char* name = oqs_name_for(scheme);
    if (name == nullptr || name[0] == '\0') {
        std::lock_guard<std::mutex> lock(mu);
        *cached = true;
        *ok = false;
        return false;
    }
    OQS_SIG* sig = OQS_SIG_new(name);
    if (sig == nullptr) {
        std::lock_guard<std::mutex> lock(mu);
        *cached = true;
        *ok = false;
        return false;
    }

    std::vector<std::uint8_t> pub(sig->length_public_key, 0);
    std::vector<std::uint8_t> sec(sig->length_secret_key, 0);
    std::vector<std::uint8_t> out(sig->length_signature, 0);
    bool works = false;
    if (OQS_SIG_keypair(sig, pub.data(), sec.data()) == OQS_SUCCESS) {
        const std::string msg = "addition-ctx-probe";
        const std::string ctx = "ADDITION|probe|ctx|0";
        size_t slen = 0;
        if (OQS_SIG_sign_with_ctx_str(sig,
                                      out.data(),
                                      &slen,
                                      reinterpret_cast<const std::uint8_t*>(msg.data()),
                                      msg.size(),
                                      reinterpret_cast<const std::uint8_t*>(ctx.data()),
                                      ctx.size(),
                                      sec.data()) == OQS_SUCCESS &&
            slen > 0 &&
            OQS_SIG_verify_with_ctx_str(sig,
                                        reinterpret_cast<const std::uint8_t*>(msg.data()),
                                        msg.size(),
                                        out.data(),
                                        slen,
                                        reinterpret_cast<const std::uint8_t*>(ctx.data()),
                                        ctx.size(),
                                        pub.data()) == OQS_SUCCESS) {
            works = true;
        }
    }
    OQS_MEM_cleanse(sec.data(), sec.size());
    OQS_SIG_free(sig);

    std::lock_guard<std::mutex> lock(mu);
    *cached = true;
    *ok = works;
    return works;
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
    if (!is_hex_strict(hex, 262144, error)) {
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

const char* sig_scheme_id(SigScheme scheme) {
    switch (scheme) {
    case SigScheme::MlDsa87:
        return "ml-dsa-87";
    case SigScheme::SlhDsaShake256s:
        return "slh-dsa-shake-256s";
    case SigScheme::Unknown:
        return "unknown";
    }
    const SigScheme missing = scheme;
    switch (missing) {
    case SigScheme::MlDsa87:
    case SigScheme::SlhDsaShake256s:
    case SigScheme::Unknown:
        break;
    }
    return "unknown";
}

const char* sig_scheme_oqs_alg(SigScheme scheme) {
    return oqs_name_for(scheme);
}

bool parse_sig_scheme(const std::string& name, SigScheme& out) {
    if (name == "ml-dsa-87" || name == "ML-DSA-87") {
        out = SigScheme::MlDsa87;
        return true;
    }
    if (name == "slh-dsa-shake-256s" || name == "SLH-DSA-SHAKE-256s" ||
        name == "SPHINCS+-SHAKE-256s-simple") {
        out = SigScheme::SlhDsaShake256s;
        return true;
    }
    out = SigScheme::Unknown;
    return false;
}

bool sig_scheme_available(SigScheme scheme) {
    PqSizes sizes{};
    return get_scheme_sizes(scheme, sizes);
}

bool sig_scheme_allowed_strict(SigScheme scheme) {
    switch (scheme) {
    case SigScheme::MlDsa87:
        return sig_scheme_available(scheme) && scheme_supports_ctx_sign(scheme);
    case SigScheme::SlhDsaShake256s:
        // Fail closed unless this liboqs build can sign+verify with a non-empty context.
        return sig_scheme_available(scheme) && scheme_supports_ctx_sign(scheme);
    case SigScheme::Unknown:
        return false;
    }
    return false;
}

std::string allowed_sig_algs_list() {
    std::string out;
    if (sig_scheme_allowed_strict(SigScheme::MlDsa87)) {
        out = "ml-dsa-87";
    }
    if (sig_scheme_allowed_strict(SigScheme::SlhDsaShake256s)) {
        if (!out.empty()) {
            out += ',';
        }
        out += "slh-dsa-shake-256s";
    }
    if (out.empty()) {
        out = "none";
    }
    return out;
}

SigScheme infer_sig_scheme_from_pubkey_hex(const std::string& pubkey_hex) {
    if ((pubkey_hex.size() % 2) != 0) {
        return SigScheme::Unknown;
    }
    const auto nbytes = pubkey_hex.size() / 2;
    PqSizes ml{};
    if (get_scheme_sizes(SigScheme::MlDsa87, ml) && nbytes == ml.public_key) {
        return SigScheme::MlDsa87;
    }
    PqSizes slh{};
    if (get_scheme_sizes(SigScheme::SlhDsaShake256s, slh) && nbytes == slh.public_key) {
        return SigScheme::SlhDsaShake256s;
    }
    return SigScheme::Unknown;
}

std::string hash_committed_address(SigScheme scheme, const std::vector<std::uint8_t>& pubkey) {
    const std::string id = sig_scheme_id(scheme);
    std::vector<std::uint8_t> preimage;
    preimage.reserve(id.size() + 1 + pubkey.size());
    preimage.insert(preimage.end(), id.begin(), id.end());
    preimage.push_back(0);
    preimage.insert(preimage.end(), pubkey.begin(), pubkey.end());
    return to_hex(sha3_512_bytes(preimage));
}

std::string hash_committed_address_hex(SigScheme scheme, const std::string& pubkey_hex) {
    std::vector<std::uint8_t> pk;
    std::string error;
    if (!hex_to_bytes(pubkey_hex, pk, error)) {
        return {};
    }
    return hash_committed_address(scheme, pk);
}

bool address_binds_pubkey(const std::string& address,
                          SigScheme scheme,
                          const std::string& pubkey_hex,
                          std::string& error) {
    if (scheme == SigScheme::Unknown || !sig_scheme_allowed_strict(scheme)) {
        error = "unknown scheme rejected in strict mode";
        return false;
    }
    const auto derived = hash_committed_address_hex(scheme, pubkey_hex);
    if (derived.empty()) {
        error = "invalid pubkey hex";
        return false;
    }
    if (derived != address) {
        error = "signer/pubkey hash-address mismatch";
        return false;
    }
    return true;
}

std::string make_consensus_sign_context(const std::string& network_mode,
                                        const std::string& network_id,
                                        const std::string& genesis_hash) {
    std::string ctx = "ADDITION|";
    ctx += network_mode.empty() ? "testnet" : network_mode;
    ctx += '|';
    ctx += network_id.empty() ? "ADDITION_TESTNET_V1" : network_id;
    ctx += '|';
    ctx += genesis_hash;
    if (ctx.size() > 255) {
        ctx.resize(255);
    }
    return ctx;
}

void set_default_sign_context(const std::string& ctx) {
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    g_default_sign_ctx = ctx;
}

std::string default_sign_context() {
    std::lock_guard<std::mutex> lock(g_ctx_mu);
    return g_default_sign_ctx;
}

std::string sign_message_hybrid(const std::string& private_key,
                                const std::string& message,
                                const std::string& ctx,
                                const std::string& scheme) {
    SigScheme parsed = SigScheme::Unknown;
    if (!parse_sig_scheme(scheme, parsed) || !sig_scheme_allowed_strict(parsed)) {
        throw std::runtime_error("unknown or unavailable signature scheme");
    }

    PqSizes sizes{};
    if (!get_scheme_sizes(parsed, sizes)) {
        throw std::runtime_error("liboqs size query failed");
    }

    std::string err;
    const auto used_ctx = resolve_ctx(ctx);
    if (!ctx_usable(used_ctx, err)) {
        throw std::runtime_error(err);
    }

    if (!is_hex_strict(private_key, sizes.secret_key * 2, err) || private_key.size() != sizes.secret_key * 2) {
        throw std::runtime_error("invalid private key hex: " + err);
    }

    std::vector<std::uint8_t> pq_sig;
    std::vector<std::uint8_t> sk;
    if (hex_to_bytes(private_key, sk, err) && !sk.empty() &&
        pq_sign_message(sk, message, used_ctx, parsed, pq_sig, err)) {
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
}

bool verify_message_signature_hybrid(const std::string& public_key,
                                     const std::string& message,
                                     const std::string& signature,
                                     const std::string& ctx,
                                     const std::string& scheme) {
    SigScheme parsed = SigScheme::Unknown;
    if (!scheme.empty()) {
        if (!parse_sig_scheme(scheme, parsed) || !sig_scheme_allowed_strict(parsed)) {
            return false;
        }
    } else {
        parsed = infer_sig_scheme_from_pubkey_hex(public_key);
        if (!sig_scheme_allowed_strict(parsed)) {
            return false;
        }
    }

    PqSizes sizes{};
    if (!get_scheme_sizes(parsed, sizes)) {
        return false;
    }

    constexpr const char* kPrefix = "pq=";
    if (signature.rfind(kPrefix, 0) != 0) {
        return false;
    }

    std::string err;
    // Empty ctx is a failed verify (FIPS 204/205). Do not silently substitute the process default.
    if (!ctx_usable(ctx, err)) {
        return false;
    }

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

    return pq_verify_message(pk_bytes, message, sig_bytes, used_ctx, parsed, err);
}

bool pq_sign_message(const std::vector<std::uint8_t>& secret_key,
                     const std::string& message,
                     const std::string& ctx,
                     SigScheme scheme,
                     std::vector<std::uint8_t>& signature,
                     std::string& error) {
    if (!sig_scheme_allowed_strict(scheme)) {
        error = "unknown scheme rejected in strict mode";
        return false;
    }
    if (!ctx_usable(ctx, error)) {
        return false;
    }

    const char* name = oqs_name_for(scheme);
    OQS_SIG* sig = OQS_SIG_new(name);
    if (sig == nullptr) {
        error = std::string("OQS_SIG_new failed for ") + sig_scheme_id(scheme);
        return false;
    }

    if (secret_key.size() != sig->length_secret_key) {
        error = "secret key size mismatch";
        OQS_SIG_free(sig);
        return false;
    }

    signature.assign(sig->length_signature, 0);
    size_t sig_len = 0;
    const auto rc = OQS_SIG_sign_with_ctx_str(sig,
                                              signature.data(),
                                              &sig_len,
                                              reinterpret_cast<const std::uint8_t*>(message.data()),
                                              message.size(),
                                              reinterpret_cast<const std::uint8_t*>(ctx.data()),
                                              ctx.size(),
                                              secret_key.data());
    OQS_SIG_free(sig);

    if (rc != OQS_SUCCESS) {
        error = "OQS_SIG_sign_with_ctx_str failed";
        signature.clear();
        return false;
    }

    signature.resize(sig_len);
    return true;
}

bool pq_verify_message(const std::vector<std::uint8_t>& public_key,
                       const std::string& message,
                       const std::vector<std::uint8_t>& signature,
                       const std::string& ctx,
                       SigScheme scheme,
                       std::string& error) {
    if (!sig_scheme_allowed_strict(scheme)) {
        error = "unknown scheme rejected in strict mode";
        return false;
    }
    if (!ctx_usable(ctx, error)) {
        return false;
    }

    const char* name = oqs_name_for(scheme);
    OQS_SIG* sig = OQS_SIG_new(name);
    if (sig == nullptr) {
        error = std::string("OQS_SIG_new failed for ") + sig_scheme_id(scheme);
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

    const auto rc = OQS_SIG_verify_with_ctx_str(sig,
                                                reinterpret_cast<const std::uint8_t*>(message.data()),
                                                message.size(),
                                                signature.data(),
                                                signature.size(),
                                                reinterpret_cast<const std::uint8_t*>(ctx.data()),
                                                ctx.size(),
                                                public_key.data());
    OQS_SIG_free(sig);

    if (rc != OQS_SUCCESS) {
        error = "OQS_SIG_verify_with_ctx_str failed";
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

    const char* oqs_name = oqs_name_for(SigScheme::MlDsa87);
    OQS_SIG* sig = OQS_SIG_new(oqs_name);
    if (sig == nullptr) {
        report = "selftest: OQS_SIG_new failed";
        return false;
    }

    const std::size_t pk_len = sig->length_public_key;
    const std::size_t sk_len = sig->length_secret_key;
    const std::size_t sig_max = sig->length_signature;

    std::vector<std::uint8_t> pub(pk_len, 0);
    std::vector<std::uint8_t> sec(sk_len, 0);
    if (OQS_SIG_keypair(sig, pub.data(), sec.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        report = "selftest: keypair failed";
        return false;
    }

    const std::string msg = "addition-crypto-selftest";
    const auto sha = to_hex(sha3_512_bytes(msg));
    const std::string ctx = make_consensus_sign_context("testnet", "ADDITION_TESTNET_V1", sha);

    std::vector<std::uint8_t> sig_bytes(sig_max, 0);
    size_t sig_len = 0;
    if (OQS_SIG_sign_with_ctx_str(sig,
                                  sig_bytes.data(),
                                  &sig_len,
                                  reinterpret_cast<const std::uint8_t*>(msg.data()),
                                  msg.size(),
                                  reinterpret_cast<const std::uint8_t*>(ctx.data()),
                                  ctx.size(),
                                  sec.data()) != OQS_SUCCESS) {
        OQS_MEM_cleanse(sec.data(), sec.size());
        OQS_SIG_free(sig);
        report = "selftest: sign failed";
        return false;
    }
    sig_bytes.resize(sig_len);

    const auto vr = OQS_SIG_verify_with_ctx_str(sig,
                                                reinterpret_cast<const std::uint8_t*>(msg.data()),
                                                msg.size(),
                                                sig_bytes.data(),
                                                sig_bytes.size(),
                                                reinterpret_cast<const std::uint8_t*>(ctx.data()),
                                                ctx.size(),
                                                pub.data());

    std::string empty_err;
    const bool empty_ctx_rejected = !pq_verify_message(pub, msg, sig_bytes, "", SigScheme::MlDsa87, empty_err);

    OQS_MEM_cleanse(sec.data(), sec.size());
    OQS_SIG_free(sig);

    if (vr != OQS_SUCCESS) {
        report = "selftest: verify failed";
        return false;
    }
    if (!empty_ctx_rejected) {
        report = "selftest: empty context was accepted";
        return false;
    }

    const char* openssl_ver = OpenSSL_version(OPENSSL_VERSION);
    std::ostringstream out;
    out << "scheme=ml-dsa-87"
        << " pk_bytes=" << pk_len
        << " sk_bytes=" << sk_len
        << " sig_bytes=" << sig_len
        << " sha3_512=" << sha
        << " pq_mode=strict"
        << " liboqs=" << OQS_VERSION_TEXT
        << " openssl=" << (openssl_ver != nullptr ? openssl_ver : "unknown")
        << " sign_verify=ok"
        << " ctx_len=" << ctx.size()
        << " empty_ctx_rejected=1"
        << " allowed_sig_algs=" << allowed_sig_algs_list()
        << " address_format=sha3_512(scheme_id||0x00||pubkey)";
    report = out.str();
    return true;
}

} // namespace addition
