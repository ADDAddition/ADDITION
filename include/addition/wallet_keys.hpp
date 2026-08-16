#pragma once

#include <string>

namespace addition {

struct WalletKeys {
    std::string private_key;
    std::string public_key;
    std::string address;
    std::string algorithm;
};

WalletKeys generate_wallet_keys(const std::string& scheme = "ml-dsa-87");

} // namespace addition
