#include "addition/wallet.hpp"

#include "addition/block.hpp"
#include "addition/crypto.hpp"

namespace addition {

Wallet::Wallet(std::string address, std::string public_key, std::string private_key)
        : address_(std::move(address)),
            public_key_(std::move(public_key)),
            private_key_(std::move(private_key)) {}

const std::string& Wallet::address() const { return address_; }

std::uint64_t Wallet::balance(const Chain& chain) const {
    return chain.balance_of(address_);
}

bool Wallet::send(Mempool& mempool,
                  const Chain& chain,
                  const std::string& to,
                  std::uint64_t amount,
                  std::uint64_t fee,
                  std::string& error) {
    Transaction tx{};
    if (!chain.build_transaction(address_, to, amount, fee, next_nonce_, tx, error)) {
        return false;
    }

    tx.signer = address_;
    tx.signer_pubkey = public_key_;
    const auto msg = hash_transaction(tx);
    tx.signature = sign_message_hybrid(private_key_, msg);

    if (!chain.validate_transaction(tx, error)) {
        return false;
    }

    if (!mempool.submit(tx)) {
        error = "transaction rejected by mempool";
        return false;
    }
    ++next_nonce_;
    return true;
}

} // namespace addition
