#include "addition/wallet_keys.hpp"

#include "addition/crypto.hpp"

#include <stdexcept>
#include <vector>

#include <oqs/oqs.h>

namespace addition {

WalletKeys generate_wallet_keys(const std::string& scheme) {
    SigScheme parsed = SigScheme::Unknown;
    if (!parse_sig_scheme(scheme, parsed) || !sig_scheme_allowed_strict(parsed)) {
        throw std::runtime_error("unknown scheme rejected in strict mode: " + scheme);
    }

    const char* oqs_name = sig_scheme_oqs_alg(parsed);
    OQS_SIG* sig = OQS_SIG_new(oqs_name);
    if (sig == nullptr) {
        throw std::runtime_error(std::string("liboqs scheme unavailable: ") + sig_scheme_id(parsed));
    }

    std::vector<std::uint8_t> pub(sig->length_public_key, 0);
    std::vector<std::uint8_t> sec(sig->length_secret_key, 0);
    if (OQS_SIG_keypair(sig, pub.data(), sec.data()) != OQS_SUCCESS) {
        OQS_SIG_free(sig);
        throw std::runtime_error("liboqs key generation failed");
    }
    OQS_SIG_free(sig);

    const auto public_key = bytes_to_hex(pub);
    const auto private_key = bytes_to_hex(sec);
    const auto address = hash_committed_address(parsed, pub);
    OQS_MEM_cleanse(sec.data(), sec.size());
    return WalletKeys{private_key, public_key, address, sig_scheme_id(parsed)};
}

} // namespace addition
