#include "addition/wallet.hpp"

#include "addition/block.hpp"
#include "addition/crypto.hpp"

namespace addition {

Wallet::Wallet(std::string address, std::string public_key, std::string private_key)
        : address_(std::move(address)),
            public_key_(std::move(public_key)),
            private_key_(std::move(private_key)) {}

const std::string& Wallet::address() const { return address_; }

const std::string& Wallet::public_key() const { return public_key_; }

std::uint64_t Wallet::balance(const Chain& chain) const {
    return chain.balance_of(address_);
}

bool Wallet::build_signed_send(const Chain& chain,
                               const std::string& to,
                               std::uint64_t amount,
                               std::uint64_t fee,
                               Transaction& out_tx,
                               std::string& error) const {
    const auto nonce = chain.next_nonce(address_);
    if (!chain.build_transaction(address_, to, amount, fee, nonce, out_tx, error)) {
        return false;
    }

    out_tx.signer = address_;
    out_tx.signer_pubkey = public_key_;
    out_tx.signature.clear();
    const auto msg = hash_transaction(out_tx);
    out_tx.signature = sign_message_hybrid(private_key_, msg);

    if (!chain.validate_transaction(out_tx, error)) {
        return false;
    }
    return true;
}

bool Wallet::send(Mempool& mempool,
                  const Chain& chain,
                  const std::string& to,
                  std::uint64_t amount,
                  std::uint64_t fee,
                  std::string& error) {
    Transaction tx{};
    if (!build_signed_send(chain, to, amount, fee, tx, error)) {
        return false;
    }

    if (!mempool.submit(tx)) {
        error = "transaction rejected by mempool";
        return false;
    }
    return true;
}

} // namespace addition
