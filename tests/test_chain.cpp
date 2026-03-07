#include "addition/chain.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/wallet_keys.hpp"

#include <iostream>

int main() {
    addition::Chain chain;
    addition::Mempool mempool;
    addition::Miner miner(chain, mempool);
    std::string error;

    std::string mined_hash;
    if (!miner.mine_next_block("miner1", 200, 1, mined_hash, error)) {
        std::cerr << "test failed: cannot mine b1: " << error << '\n';
        return 1;
    }

    if (chain.balance_of("miner1") != 50) {
        std::cerr << "test failed: miner balance mismatch after b1\n";
        return 1;
    }

    addition::WalletKeys miner_keys{};
    try {
        miner_keys = addition::generate_wallet_keys();
    } catch (const std::exception& e) {
        std::cerr << "test failed: wallet generation failed: " << e.what() << '\n';
        return 1;
    }

    if (!chain.credit_balance(miner_keys.address, 100, "test_seed", error)) {
        std::cerr << "test failed: cannot seed wallet: " << error << '\n';
        return 1;
    }

    addition::Transaction pay{};
    if (!chain.build_transaction(miner_keys.address, "bob", 20, 1, 1, pay, error)) {
        std::cerr << "test failed: cannot build tx: " << error << '\n';
        return 1;
    }
    pay.signer = miner_keys.address;
    pay.signer_pubkey = miner_keys.public_key;
    {
        addition::Transaction unsigned_pay = pay;
        unsigned_pay.signature.clear();
        const auto msg = addition::hash_transaction(unsigned_pay);
        pay.signature = addition::sign_message_hybrid(miner_keys.private_key, msg);
    }
    if (!chain.validate_transaction(pay, error)) {
        std::cerr << "test failed: signed tx invalid: " << error << '\n';
        return 1;
    }
    if (!mempool.submit(pay)) {
        std::cerr << "test failed: mempool rejected tx\n";
        return 1;
    }

    if (!miner.mine_next_block("miner1", 200, 1, mined_hash, error)) {
        std::cerr << "test failed: cannot mine b2: " << error << '\n';
        return 1;
    }

    if (chain.balance_of("bob") != 20) {
        std::cerr << "test failed: bob balance mismatch\n";
        return 1;
    }

    if (chain.balance_of(miner_keys.address) < 79) {
        std::cerr << "test failed: sender balance too low after spend\n";
        return 1;
    }

    std::cout << "all tests passed\n";
    return 0;
}
