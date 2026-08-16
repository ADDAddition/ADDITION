#include "addition/wallet_keys.hpp"

#include "addition/crypto.hpp"

#include <stdexcept>

#include <oqs/oqs.h>

namespace addition {

WalletKeys generate_wallet_keys() {
    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_87);
    if (sig != nullptr) {
        std::vector<std::uint8_t> pub(sig->length_public_key, 0);
        std::vector<std::uint8_t> sec(sig->length_secret_key, 0);
        if (OQS_SIG_keypair(sig, pub.data(), sec.data()) == OQS_SUCCESS) {
            OQS_SIG_free(sig);
            const auto public_key = bytes_to_hex(pub);
            const auto private_key = bytes_to_hex(sec);
            const auto address = hash_committed_address(kMlDsa87SchemeId, pub);
            return WalletKeys{private_key, public_key, address, kMlDsa87SchemeId};
        }
        OQS_SIG_free(sig);
    }
    throw std::runtime_error("liboqs key generation unavailable");
}

} // namespace addition
