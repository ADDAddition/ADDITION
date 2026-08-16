#include "addition/block.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/state_store.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::filesystem::path make_temp_dir(const std::string& tag) {
    const auto root = std::filesystem::temp_directory_path() /
        ("addition-mainnet-chain-" + tag + "-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

bool load_checked_genesis(const char* path, addition::ChainConfig& chain, std::string& err) {
    return addition::load_genesis_json(path, chain, err) ||
           addition::load_genesis_json((std::string("../") + path).c_str(), chain, err);
}

} // namespace

int main() {
    const auto testnet_cfg = addition::testnet_chain_config();
    const auto mainnet_cfg = addition::mainnet_chain_config();

    if (testnet_cfg.network_id != addition::kTestnetNetworkId ||
        testnet_cfg.network_mode != "testnet" ||
        testnet_cfg.network_name != addition::kTestnetNetworkName) {
        std::cerr << "test failed: testnet ids\n";
        return 1;
    }
    if (mainnet_cfg.network_id != addition::kMainnetNetworkId ||
        mainnet_cfg.network_mode != "mainnet" ||
        mainnet_cfg.network_name != addition::kMainnetNetworkName) {
        std::cerr << "test failed: mainnet ids\n";
        return 1;
    }
    if (mainnet_cfg.genesis_timestamp == testnet_cfg.genesis_timestamp) {
        std::cerr << "test failed: mainnet genesis timestamp must differ from testnet\n";
        return 1;
    }
    if (mainnet_cfg.pow_algorithm != addition::PowAlgorithm::MemoryHard) {
        std::cerr << "test failed: mainnet must keep memory_hard PoW\n";
        return 1;
    }
    if (mainnet_cfg.initial_difficulty_target != addition::kMainnetDifficultyTarget ||
        mainnet_cfg.min_difficulty_target != addition::kMainnetDifficultyTarget ||
        mainnet_cfg.max_difficulty_target != addition::kMainnetMaxDifficultyTarget) {
        std::cerr << "test failed: mainnet difficulty knobs\n";
        return 1;
    }
    if (mainnet_cfg.initial_difficulty_target >= addition::kTestnetEasyDifficultyTarget ||
        mainnet_cfg.max_difficulty_target >= addition::kTestnetEasyDifficultyTarget) {
        std::cerr << "test failed: mainnet must not use the ~4ms easy target\n";
        return 1;
    }
    if (mainnet_cfg.initial_difficulty_target > addition::kTestnetHardDifficultyTarget) {
        std::cerr << "test failed: mainnet initial target must be at or harder than testnet hard\n";
        return 1;
    }
    if (addition::kMainnetDifficultyTarget != 0x000000FFFFFFFFFFULL ||
        mainnet_cfg.initial_difficulty_target != 0x000000FFFFFFFFFFULL ||
        mainnet_cfg.min_difficulty_target != 0x000000FFFFFFFFFFULL ||
        mainnet_cfg.max_difficulty_target != 0x000000FFFFFFFFFFULL) {
        std::cerr << "test failed: ADDITION_MAINNET_V1 difficulty_target must stay 0x000000FFFFFFFFFF\n";
        return 1;
    }
    if (addition::mine_deadline_seconds(mainnet_cfg) != addition::kMainnetMineDeadlineSec ||
        addition::kMainnetMineDeadlineSec != 0) {
        std::cerr << "test failed: mainnet mine must not use a 30s deadline\n";
        return 1;
    }
    if (addition::mine_deadline_seconds(testnet_cfg) != 30) {
        std::cerr << "test failed: testnet mine deadline must stay 30s\n";
        return 1;
    }
    if (addition::default_mine_thread_count() < 1) {
        std::cerr << "test failed: default mine threads\n";
        return 1;
    }

    addition::Chain testnet(testnet_cfg);
    addition::Chain mainnet(mainnet_cfg);
    const auto testnet_genesis = addition::hash_block_header(testnet.genesis_block().header);
    const auto mainnet_genesis = addition::hash_block_header(mainnet.genesis_block().header);
    if (testnet_genesis == mainnet_genesis) {
        std::cerr << "test failed: genesis hashes must not match\n";
        return 1;
    }
    if (testnet.genesis_block().header.previous_hash != "0") {
        std::cerr << "test failed: testnet genesis previous_hash must stay 0\n";
        return 1;
    }
    if (mainnet.genesis_block().header.previous_hash !=
        std::string("genesis:") + addition::kMainnetNetworkId) {
        std::cerr << "test failed: mainnet genesis must bind previous_hash to network_id\n";
        return 1;
    }

    {
        addition::ChainConfig file = addition::mainnet_chain_config();
        std::string err;
        if (!load_checked_genesis("genesis-mainnet.json", file, err)) {
            std::cerr << "test failed: load genesis-mainnet.json: " << err << '\n';
            return 1;
        }
        if (file.network_id != addition::kMainnetNetworkId || file.network_mode != "mainnet" ||
            file.genesis_timestamp != addition::kMainnetGenesisTimestamp ||
            file.pow_algorithm != addition::PowAlgorithm::MemoryHard) {
            std::cerr << "test failed: genesis-mainnet.json fields\n";
            return 1;
        }
    }

    {
        addition::NodeConfig ncfg;
        bool help = false;
        std::string err;
        char arg0[] = "additiond";
        char arg1[] = "--network";
        char arg2[] = "testnet";
        char* argv[] = {arg0, arg1, arg2};
        if (!addition::apply_cli_args(3, argv, ncfg, help, err)) {
            std::cerr << "test failed: --network testnet: " << err << '\n';
            return 1;
        }
        if (ncfg.mode != addition::NetworkMode::Testnet ||
            ncfg.chain.network_id != addition::kTestnetNetworkId ||
            ncfg.chain.network_mode != "testnet" ||
            ncfg.data_dir == "data-mainnet") {
            std::cerr << "test failed: testnet CLI path changed\n";
            return 1;
        }
    }

    {
        addition::NodeConfig ncfg;
        bool help = false;
        std::string err;
        char arg0[] = "additiond";
        char arg1[] = "--mainnet";
        char* argv[] = {arg0, arg1};
        if (!addition::apply_cli_args(2, argv, ncfg, help, err)) {
            std::cerr << "test failed: --mainnet: " << err << '\n';
            return 1;
        }
        if (ncfg.mode != addition::NetworkMode::Mainnet ||
            ncfg.chain.network_id != addition::kMainnetNetworkId ||
            ncfg.chain.network_name != addition::kMainnetNetworkName ||
            ncfg.chain.network_mode != "mainnet" ||
            ncfg.data_dir != "data-mainnet" ||
            ncfg.local_rpc_port != 8546 ||
            ncfg.public_rpc_port != 38546) {
            std::cerr << "test failed: --mainnet profile\n";
            return 1;
        }
        if (ncfg.chain.genesis_timestamp == addition::kTestnetGenesisTimestamp) {
            std::cerr << "test failed: --mainnet still has testnet timestamp\n";
            return 1;
        }
        for (const auto& peer : ncfg.bootstrap_peers) {
            if (peer == addition::kOperatorPublicP2p) {
                std::cerr << "test failed: --mainnet bootstraps the public testnet seed\n";
                return 1;
            }
        }
    }

    {
        addition::NodeConfig ncfg;
        bool help = false;
        std::string err;
        char arg0[] = "additiond";
        char arg1[] = "--network";
        char arg2[] = "mainnet";
        char arg3[] = "--genesis";
        const char* genesis_path = std::filesystem::exists("genesis.json") ? "genesis.json" : "../genesis.json";
        std::string genesis_buf = genesis_path;
        std::vector<char> genesis_arg(genesis_buf.begin(), genesis_buf.end());
        genesis_arg.push_back('\0');
        char* argv[] = {arg0, arg1, arg2, arg3, genesis_arg.data()};
        if (std::filesystem::exists(genesis_path)) {
            if (addition::apply_cli_args(5, argv, ncfg, help, err)) {
                std::cerr << "test failed: mainnet must reject testnet genesis.json\n";
                return 1;
            }
            if (err.find("relabel") == std::string::npos && err.find("genesis") == std::string::npos &&
                err.find("timestamp") == std::string::npos) {
                std::cerr << "test failed: unexpected mix error: " << err << '\n';
                return 1;
            }
        }
    }

    {
        addition::NodeConfig ncfg;
        bool help = false;
        std::string err;
        char arg0[] = "additiond";
        char arg1[] = "--mainnet";
        char arg2[] = "--bootstrap";
        char arg3[] = "34.27.30.115:28545";
        char* argv[] = {arg0, arg1, arg2, arg3};
        if (addition::apply_cli_args(4, argv, ncfg, help, err)) {
            std::cerr << "test failed: mainnet accepted the public testnet seed\n";
            return 1;
        }
    }

    {
        addition::ChainConfig easy = addition::testnet_chain_config();
        easy.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
        easy.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
        easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
        addition::Chain mined(easy);
        addition::Mempool mempool;
        addition::Miner miner(mined, mempool);
        std::string hash;
        std::string err;
        if (!miner.mine_next_block("miner1", 200, 1, hash, err)) {
            std::cerr << "test failed: mine testnet block for mix check: " << err << '\n';
            return 1;
        }
        const auto dir = make_temp_dir("mix");
        addition::StateStore store(dir.string());
        if (!store.save_chain(mined, err)) {
            std::cerr << "test failed: save testnet chain: " << err << '\n';
            std::filesystem::remove_all(dir);
            return 1;
        }
        addition::Chain other(mainnet_cfg);
        if (store.load_chain(other, err)) {
            std::cerr << "test failed: mainnet loaded testnet blocks\n";
            std::filesystem::remove_all(dir);
            return 1;
        }
        if (err.find("chain load failed") == std::string::npos) {
            std::cerr << "test failed: expected chain load failure, got: " << err << '\n';
            std::filesystem::remove_all(dir);
            return 1;
        }
        std::filesystem::remove_all(dir);
    }

    {
        const auto dir = make_temp_dir("marker");
        addition::StateStore store(dir.string());
        std::string err;
        if (!store.write_network_marker(testnet, err)) {
            std::cerr << "test failed: write testnet marker: " << err << '\n';
            std::filesystem::remove_all(dir);
            return 1;
        }
        if (store.check_network_marker(mainnet, err)) {
            std::cerr << "test failed: mainnet accepted a testnet network.dat\n";
            std::filesystem::remove_all(dir);
            return 1;
        }
        if (!store.check_network_marker(testnet, err)) {
            std::cerr << "test failed: testnet marker should match testnet chain: " << err << '\n';
            std::filesystem::remove_all(dir);
            return 1;
        }
        std::filesystem::remove_all(dir);
    }

    {
        addition::NodeConfig cfg = addition::mainnet_node_config();
        std::string err;
        if (!addition::validate_network_profile(cfg, err)) {
            std::cerr << "test failed: mainnet_node_config should validate: " << err << '\n';
            return 1;
        }
        cfg.chain.network_id = addition::kTestnetNetworkId;
        if (addition::validate_network_profile(cfg, err)) {
            std::cerr << "test failed: mainnet with testnet id must not validate\n";
            return 1;
        }
        addition::NodeConfig tn = addition::default_node_config();
        if (!addition::validate_network_profile(tn, err)) {
            std::cerr << "test failed: default testnet should validate: " << err << '\n';
            return 1;
        }
        tn.chain.network_id = addition::kMainnetNetworkId;
        tn.chain.network_mode = "mainnet";
        if (addition::validate_network_profile(tn, err)) {
            std::cerr << "test failed: testnet mode with mainnet id must not validate\n";
            return 1;
        }
    }

    {
        // Prove the memory_hard multi-thread miner finds a block. Easy target is
        // test-only; production ADDITION_MAINNET_V1 stays 0x000000FFFFFFFFFF.
        addition::ChainConfig easy = addition::mainnet_chain_config();
        easy.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
        easy.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
        easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
        addition::Chain mined(easy);
        addition::Mempool mempool;
        addition::Miner miner(mined, mempool);
        std::string hash;
        std::string err;
        const auto t0 = std::chrono::steady_clock::now();
        if (!miner.mine_next_block("miner1", 200, 2, hash, err)) {
            std::cerr << "test failed: memory_hard multi-thread mine: " << err << '\n';
            return 1;
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        if (mined.height() != 1 || hash.empty()) {
            std::cerr << "test failed: memory_hard mine did not commit a block\n";
            return 1;
        }
        if (mined.current_difficulty_target() != 0xFFFFFFFFFFFFFFFFULL) {
            std::cerr << "test failed: easy test must not change production target knobs\n";
            return 1;
        }
        if (ms > 30000) {
            std::cerr << "test failed: easy memory_hard mine exceeded 30s: " << ms << "ms\n";
            return 1;
        }
        if (miner.last_threads() < 1) {
            std::cerr << "test failed: miner threads not recorded\n";
            return 1;
        }
    }

    {
        // Production mainnet target: mine must still be running after 31s.
        addition::Chain mainnet(mainnet_cfg);
        if (mainnet.current_difficulty_target() != 0x000000FFFFFFFFFFULL) {
            std::cerr << "test failed: live mainnet chain target drifted\n";
            return 1;
        }
        std::atomic<bool> stop{false};
        std::string hash;
        std::string err;
        std::atomic<bool> finished{false};
        std::thread worker([&]() {
            mainnet.mine_and_add_block("miner1", {}, 2, hash, err, &stop);
            finished.store(true, std::memory_order_relaxed);
        });
        std::this_thread::sleep_for(std::chrono::seconds(31));
        const bool done_at_31s = finished.load(std::memory_order_relaxed);
        stop.store(true, std::memory_order_relaxed);
        worker.join();
        if (err.find("30s") != std::string::npos) {
            std::cerr << "test failed: mainnet mine still uses the 30s leftover: " << err << '\n';
            return 1;
        }
        if (done_at_31s && hash.empty()) {
            std::cerr << "test failed: mainnet mine aborted before 31s: " << err << '\n';
            return 1;
        }
        if (!done_at_31s && err.find("stopped") == std::string::npos) {
            std::cerr << "test failed: expected mining stopped after cancel: " << err << '\n';
            return 1;
        }
    }

    std::cout << "all mainnet chain tests passed\n";
    return 0;
}
