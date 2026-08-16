#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/state_store.hpp"
#include "addition/wallet_keys.hpp"

#include <chrono>
#include <filesystem>
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

    if (addition::testnet_chain_config().pow_algorithm != addition::PowAlgorithm::Sha3_512) {
        std::cerr << "test failed: testnet must use sha3_512 header PoW\n";
        return 1;
    }
    if (addition::mainnet_chain_config().pow_algorithm != addition::PowAlgorithm::MemoryHard) {
        std::cerr << "test failed: mainnet profile must keep memory_hard PoW\n";
        return 1;
    }

    addition::ChainConfig live = addition::testnet_chain_config();
    addition::Chain chain(live);

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
        const auto persist_dir = std::filesystem::temp_directory_path() /
            ("addition-chain-persist-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(persist_dir);
        addition::StateStore store(persist_dir.string());
        if (!store.save_chain(chain, error)) {
            std::cerr << "test failed: save_chain: " << error << '\n';
            std::filesystem::remove_all(persist_dir);
            return 1;
        }
        if (!store.chain_file_exists()) {
            std::cerr << "test failed: blocks.dat missing after save_chain\n";
            std::filesystem::remove_all(persist_dir);
            return 1;
        }

        const auto height_before = chain.height();
        const auto tip_hash = addition::hash_block_header(chain.tip().header);
        const auto miner_bal = chain.balance_of("miner1");
        const auto bob_bal = chain.balance_of("bob");

        addition::Chain restored(live);
        if (!store.load_chain(restored, error)) {
            std::cerr << "test failed: load_chain: " << error << '\n';
            std::filesystem::remove_all(persist_dir);
            return 1;
        }
        if (restored.height() != height_before) {
            std::cerr << "test failed: restored height " << restored.height()
                      << " != " << height_before << '\n';
            std::filesystem::remove_all(persist_dir);
            return 1;
        }
        const auto restored_hash = addition::hash_block_header(restored.tip().header);
        if (restored_hash != tip_hash) {
            std::cerr << "test failed: restored tip hash mismatch\n";
            std::filesystem::remove_all(persist_dir);
            return 1;
        }
        if (restored.balance_of("miner1") != miner_bal || restored.balance_of("bob") != bob_bal) {
            std::cerr << "test failed: restored UTXO balances mismatch\n";
            std::filesystem::remove_all(persist_dir);
            return 1;
        }
        std::filesystem::remove_all(persist_dir);
    }

    std::cout << "all tests passed\n";
    return 0;
}
