#pragma once

#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/mempool.hpp"

#include <cstdint>
#include <string>

namespace addition {

class Wallet {
public:
    Wallet(std::string address, std::string public_key, std::string private_key);

    const std::string& address() const;
    const std::string& public_key() const;
    std::uint64_t balance(const Chain& chain) const;

    bool build_signed_send(const Chain& chain,
                           const std::string& to,
                           std::uint64_t amount,
                           std::uint64_t fee,
                           Transaction& out_tx,
                           std::string& error) const;

    bool send(Mempool& mempool,
              const Chain& chain,
              const std::string& to,
              std::uint64_t amount,
              std::uint64_t fee,
              std::string& error);

private:
    std::string address_;
    std::string public_key_;
    std::string private_key_;
};

} // namespace addition
