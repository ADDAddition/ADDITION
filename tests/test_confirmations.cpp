#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/wallet.hpp"
#include "addition/wallet_keys.hpp"

#include <iostream>
#include <string>

int main() {
    {
        addition::Chain shared(addition::testnet_chain_config());
        if (shared.config().pow_profile != "shared-testnet" ||
            shared.config().economic_security != "none" ||
            shared.config().confirmations_policy != 2) {
            std::cerr << "test failed: shared-testnet honesty fields\n";
            return 1;
        }
        if (shared.config().max_difficulty_target >= 0x0000FFFFFFFFFFFFULL) {
            std::cerr << "test failed: shared-testnet max target is still toy-easy\n";
            return 1;
        }

        addition::Block toy = shared.tip();
        toy.header.height = 1;
        toy.header.previous_hash = addition::hash_block_header(shared.tip().header);
        toy.header.difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
        toy.header.merkle_root = addition::compute_merkle_root(toy.transactions);
        std::string error;
        if (shared.add_block(toy, error)) {
            std::cerr << "test failed: shared-testnet accepted toy-diff header\n";
            return 1;
        }
        if (error.find("min-diff") == std::string::npos && error.find("toy") == std::string::npos &&
            error.find("difficulty") == std::string::npos) {
            std::cerr << "test failed: unexpected toy-diff error: " << error << '\n';
            return 1;
        }
    }

    addition::ChainConfig easy = addition::regtest_chain_config();
    if (easy.pow_profile != "regtest" || easy.initial_difficulty_target != 0xFFFFFFFFFFFFFFFFULL) {
        std::cerr << "test failed: --regtest profile must stay fast\n";
        return 1;
    }
    easy.confirmations_policy = 2;
    addition::Chain chain_a(easy);
    addition::Chain chain_b(easy);
    if (chain_a.genesis_hash() != chain_b.genesis_hash()) {
        std::cerr << "test failed: genesis mismatch between A and B\n";
        return 1;
    }

    addition::WalletKeys keys = addition::generate_wallet_keys();
    std::string error;
    if (!chain_a.credit_balance(keys.address, 80, "conf_seed", error)) {
        std::cerr << "test failed: credit A: " << error << '\n';
        return 1;
    }

    addition::Mempool mempool;
    addition::Wallet wallet(keys.address, keys.public_key, keys.private_key);
    if (!wallet.send(mempool, chain_a, "bob", 7, 1, error)) {
        std::cerr << "test failed: send: " << error << '\n';
        return 1;
    }
    addition::Miner miner_a(chain_a, mempool);
    std::string mined;
    if (!miner_a.mine_next_block("miner-a", 200, 1, mined, error)) {
        std::cerr << "test failed: mine A1: " << error << '\n';
        return 1;
    }
    std::string tx_hash;
    for (const auto& b : chain_a.blocks()) {
        for (const auto& tx : b.transactions) {
            if (!tx.inputs.empty() && tx.signer == keys.address) {
                tx_hash = addition::hash_transaction(tx);
            }
        }
    }
    if (tx_hash.empty() || chain_a.tx_confirmations(tx_hash) != 1) {
        std::cerr << "test failed: expected 1 confirmation after inclusion\n";
        return 1;
    }
    addition::Mempool empty_mp;
    addition::Miner miner_a2(chain_a, empty_mp);
    if (!miner_a2.mine_next_block("miner-a", 200, 1, mined, error)) {
        std::cerr << "test failed: mine A2: " << error << '\n';
        return 1;
    }
    if (chain_a.tx_confirmations(tx_hash) != 2) {
        std::cerr << "test failed: expected 2 confirmations, got "
                  << chain_a.tx_confirmations(tx_hash) << '\n';
        return 1;
    }

    addition::Mempool mp_b;
    addition::Miner miner_b(chain_b, mp_b);
    if (!miner_b.mine_next_block("miner-b", 200, 1, mined, error) ||
        !miner_b.mine_next_block("miner-b", 200, 1, mined, error) ||
        !miner_b.mine_next_block("miner-b", 200, 1, mined, error)) {
        std::cerr << "test failed: mine B side chain: " << error << '\n';
        return 1;
    }
    if (chain_b.cumulative_work() <= chain_a.cumulative_work()) {
        std::cerr << "test failed: B must have more work for reorg\n";
        return 1;
    }
    if (!chain_a.replace_with_chain(chain_b.blocks(), error)) {
        std::cerr << "test failed: reorg: " << error << '\n';
        return 1;
    }
    if (chain_a.tx_confirmations(tx_hash) != 0 || chain_a.tx_in_best_chain(tx_hash)) {
        std::cerr << "test failed: 1-block reorg must mark tx unconfirmed\n";
        return 1;
    }

    std::cout << "confirmations and min-diff tests passed\n";
    return 0;
}
