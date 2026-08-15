#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/wallet_keys.hpp"

#include <iostream>
#include <string>

int main() {
    if (addition::default_config().network_name != "addition-testnet" ||
        addition::default_config().network_mode != "testnet") {
        std::cerr << "test failed: default config must be addition-testnet\n";
        return 1;
    }

    {
        addition::Chain default_chain;
        if (default_chain.config().network_mode != "testnet" ||
            default_chain.config().network_name != "addition-testnet") {
            std::cerr << "test failed: chain default network must be testnet\n";
            return 1;
        }
    }

    addition::ChainConfig easy = addition::testnet_chain_config();
    easy.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    addition::Chain chain(easy);

    {
        addition::NodeConfig ncfg;
        bool help = false;
        std::string err;
        char arg0[] = "additiond";
        char arg1[] = "--network";
        char arg2[] = "testnet";
        char* argv[] = {arg0, arg1, arg2};
        if (!addition::apply_cli_args(3, argv, ncfg, help, err)) {
            std::cerr << "test failed: cli parse: " << err << '\n';
            return 1;
        }
        if (ncfg.mode != addition::NetworkMode::Testnet || ncfg.chain.network_mode != "testnet") {
            std::cerr << "test failed: --network testnet not applied\n";
            return 1;
        }
    }

    {
        addition::ChainConfig genesis = addition::testnet_chain_config();
        std::string genesis_err;
        bool loaded = addition::load_genesis_json("genesis.json", genesis, genesis_err) ||
                      addition::load_genesis_json("../genesis.json", genesis, genesis_err);
        if (loaded && (genesis.network_name != "addition-testnet" || genesis.block_reward != 50)) {
            std::cerr << "test failed: genesis.json mismatch\n";
            return 1;
        }
    }
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

    {
        if (easy.min_fee != 1) {
            std::cerr << "test failed: testnet min_fee must be 1, got " << easy.min_fee << '\n';
            return 1;
        }
        addition::Transaction zero_fee{};
        if (!chain.build_transaction(miner_keys.address, "carol", 1, 0, 2, zero_fee, error)) {
            std::cerr << "test failed: build zero-fee tx: " << error << '\n';
            return 1;
        }
        zero_fee.signer = miner_keys.address;
        zero_fee.signer_pubkey = miner_keys.public_key;
        {
            addition::Transaction unsigned_zero = zero_fee;
            unsigned_zero.signature.clear();
            const auto msg = addition::hash_transaction(unsigned_zero);
            zero_fee.signature = addition::sign_message_hybrid(miner_keys.private_key, msg);
        }
        std::string fee_error;
        if (chain.validate_transaction(zero_fee, fee_error)) {
            std::cerr << "test failed: fee=0 must be rejected by min_fee=1\n";
            return 1;
        }
        if (fee_error.find("fee below network minimum") == std::string::npos) {
            std::cerr << "test failed: unexpected fee=0 error: " << fee_error << '\n';
            return 1;
        }

        addition::Transaction ok_fee{};
        if (!chain.build_transaction(miner_keys.address, "carol", 1, 1, 2, ok_fee, error)) {
            std::cerr << "test failed: build fee=1 tx: " << error << '\n';
            return 1;
        }
        ok_fee.signer = miner_keys.address;
        ok_fee.signer_pubkey = miner_keys.public_key;
        {
            addition::Transaction unsigned_ok = ok_fee;
            unsigned_ok.signature.clear();
            const auto msg = addition::hash_transaction(unsigned_ok);
            ok_fee.signature = addition::sign_message_hybrid(miner_keys.private_key, msg);
        }
        if (!chain.validate_transaction(ok_fee, error)) {
            std::cerr << "test failed: fee=1 must be accepted: " << error << '\n';
            return 1;
        }
    }

    std::cout << "all tests passed\n";
    return 0;
}
