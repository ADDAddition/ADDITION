#include "addition/auto_mine.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"

#include <chrono>
#include <iostream>
#include <string>

int main() {
    {
        const auto cfg = addition::default_node_config();
        if (cfg.enable_auto_mine) {
            std::cerr << "test failed: auto-mine must be off by default\n";
            return 1;
        }
        if (cfg.auto_mine_interval_sec != 60 || cfg.auto_mine_reward != "miner1") {
            std::cerr << "test failed: auto-mine default interval/reward\n";
            return 1;
        }
    }

    addition::Chain chain(addition::testnet_chain_config());
    addition::Mempool mempool;
    addition::Miner miner(chain, mempool);
    const auto t0 = std::chrono::steady_clock::now();

    {
        addition::AutoMineSettings off{};
        addition::AutoMiner autominer(miner, addition::NetworkMode::Testnet, off);
        if (autominer.enabled()) {
            std::cerr << "test failed: AutoMiner enabled when settings say off\n";
            return 1;
        }
        std::string hash;
        std::string error;
        if (autominer.maybe_mine(t0 + std::chrono::seconds(3600), hash, error)) {
            std::cerr << "test failed: auto-mine ran while disabled: " << error << '\n';
            return 1;
        }
        if (chain.height() != 0) {
            std::cerr << "test failed: disabled auto-mine changed height\n";
            return 1;
        }
    }

    {
        addition::AutoMineSettings on{};
        on.enabled = true;
        on.interval_sec = 1;
        on.reward_address = "auto_miner";
        addition::AutoMiner autominer(miner, addition::NetworkMode::Testnet, on);
        if (!autominer.enabled()) {
            std::cerr << "test failed: AutoMiner should be enabled on testnet\n";
            return 1;
        }
        std::string hash;
        std::string error;
        if (autominer.maybe_mine(t0, hash, error)) {
            std::cerr << "test failed: auto-mine must wait for the interval\n";
            return 1;
        }
        if (!autominer.maybe_mine(t0 + std::chrono::seconds(1), hash, error)) {
            std::cerr << "test failed: enabled auto-mine did not mine: " << error << '\n';
            return 1;
        }
        if (chain.height() != 1 || hash.empty()) {
            std::cerr << "test failed: expected one mined block, height=" << chain.height() << '\n';
            return 1;
        }
        if (chain.balance_of("auto_miner") != 50) {
            std::cerr << "test failed: auto-mine reward not credited\n";
            return 1;
        }
    }

    {
        addition::Chain mainnet_chain(addition::mainnet_chain_config());
        addition::Mempool mainnet_mempool;
        addition::Miner mainnet_miner(mainnet_chain, mainnet_mempool);
        addition::AutoMineSettings on{};
        on.enabled = true;
        on.interval_sec = 1;
        addition::AutoMiner autominer(mainnet_miner, addition::NetworkMode::Mainnet, on);
        if (autominer.enabled()) {
            std::cerr << "test failed: auto-mine must stay off on mainnet profile\n";
            return 1;
        }
        std::string hash;
        std::string error;
        if (autominer.maybe_mine(t0 + std::chrono::seconds(10), hash, error)) {
            std::cerr << "test failed: auto-mine ran on mainnet\n";
            return 1;
        }
        if (mainnet_chain.height() != 0) {
            std::cerr << "test failed: mainnet auto-mine changed height\n";
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
            std::cerr << "test failed: cli default parse: " << err << '\n';
            return 1;
        }
        if (ncfg.enable_auto_mine) {
            std::cerr << "test failed: CLI default must leave auto-mine off\n";
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
        char arg3[] = "--auto-mine";
        char arg4[] = "--auto-mine-interval";
        char arg5[] = "15";
        char arg6[] = "--auto-mine-reward";
        char arg7[] = "reward_addr";
        char arg8[] = "--bootstrap";
        char arg9[] = "34.27.30.115:28545";
        char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9};
        if (!addition::apply_cli_args(10, argv, ncfg, help, err)) {
            std::cerr << "test failed: cli auto-mine parse: " << err << '\n';
            return 1;
        }
        if (!ncfg.enable_auto_mine || ncfg.auto_mine_interval_sec != 15 ||
            ncfg.auto_mine_reward != "reward_addr" ||
            ncfg.bootstrap_peers.size() != 1 ||
            ncfg.bootstrap_peers[0] != addition::kOperatorPublicP2p) {
            std::cerr << "test failed: --auto-mine / public bootstrap not applied\n";
            return 1;
        }
    }

    {
        addition::NodeConfig ncfg;
        bool help = false;
        std::string err;
        char arg0[] = "additiond";
        char arg1[] = "--bootstrap";
        char arg2[] = "example.com:28545";
        char* argv[] = {arg0, arg1, arg2};
        if (addition::apply_cli_args(3, argv, ncfg, help, err)) {
            std::cerr << "test failed: hostname bootstrap must be rejected\n";
            return 1;
        }
    }

    {
        addition::NodeConfig loaded;
        std::string err;
        const bool ok = addition::load_toml_config("config.toml", loaded, err) ||
                        addition::load_toml_config("../config.toml", loaded, err);
        if (ok) {
            if (loaded.enable_auto_mine) {
                std::cerr << "test failed: config.toml must leave auto-mine off\n";
                return 1;
            }
            if (loaded.bootstrap_peers.size() != 1 ||
                loaded.bootstrap_peers[0] != addition::kOperatorPublicP2p) {
                std::cerr << "test failed: config.toml must list only the operator public P2P\n";
                return 1;
            }
        }
    }

    if (!addition::is_ipv4_endpoint("127.0.0.1:28545") ||
        !addition::is_ipv4_endpoint("34.27.30.115:28545") ||
        addition::is_ipv4_endpoint("localhost:28545") ||
        addition::is_ipv4_endpoint("::1:28545") ||
        addition::is_ipv4_endpoint("34.27.30.115")) {
        std::cerr << "test failed: IPv4 endpoint validation\n";
        return 1;
    }

    std::cout << "all auto-mine tests passed\n";
    return 0;
}
