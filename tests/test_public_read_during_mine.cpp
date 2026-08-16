#include "addition/ai_optimizer.hpp"
#include "addition/bridge.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/contract_engine.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/p2p.hpp"
#include "addition/pouw_compute.hpp"
#include "addition/pouw_storage.hpp"
#include "addition/privacy.hpp"
#include "addition/private_messaging.hpp"
#include "addition/rpc_access.hpp"
#include "addition/rpc_server.hpp"
#include "addition/staking.hpp"
#include "addition/token_engine.hpp"
#include "addition/wallet_keys.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace {

std::filesystem::path make_temp_dir(const std::string& tag) {
    const auto root = std::filesystem::temp_directory_path() / ("addition-public-read-mine-" + tag);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void set_master_key() {
    const char* key = "addition-research-privacy-master-key-32";
#ifdef _WIN32
    _putenv_s("ADDITION_PRIVACY_MASTER_KEY", key);
#else
    setenv("ADDITION_PRIVACY_MASTER_KEY", key, 1);
#endif
}

bool expect_contains(const std::string& hay, const std::string& needle, const char* label) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "test failed: " << label << " missing [" << needle << "] in [" << hay << "]\n";
        return false;
    }
    return true;
}

struct NodeKit {
    addition::Chain chain;
    addition::Mempool mempool;
    addition::Miner miner;
    addition::StakingEngine staking;
    addition::TokenEngine tokens;
    addition::PrivacyPool privacy;
    addition::ContractEngine contracts;
    addition::BridgeEngine bridge;
    addition::PeerNetwork peers;
    addition::ConsensusEngine consensus;
    addition::PoUWStorageEngine pouw_storage;
    addition::PoUWComputeEngine pouw_compute;
    addition::PrivateMessagingEngine private_messaging;
    addition::AIRoutingOptimizer ai_optimizer;
    addition::WalletKeys keys;
    addition::DecentralizedNode node;
    std::filesystem::path rpc_dir;
    addition::RpcServer rpc;

    explicit NodeKit(addition::ChainConfig cfg, const std::string& tag)
        : chain(std::move(cfg)),
          miner(chain, mempool),
          contracts(&tokens, &privacy),
          keys(addition::generate_wallet_keys()),
          node("self", keys.public_key, keys.private_key, chain, mempool, peers, consensus),
          rpc_dir(make_temp_dir(tag)),
          rpc(chain,
              mempool,
              miner,
              staking,
              contracts,
              bridge,
              tokens,
              peers,
              consensus,
              privacy,
              pouw_storage,
              pouw_compute,
              private_messaging,
              ai_optimizer,
              node,
              false,
              true,
              (rpc_dir / "wallets").string()) {}

    ~NodeKit() {
        std::error_code ec;
        std::filesystem::remove_all(rpc_dir, ec);
    }
};

bool check_banner(addition::RpcServer& rpc, const std::string& network_mode) {
    const auto want = addition::public_rpc_banner_text(network_mode);
    if (rpc.public_rpc_banner() != want) {
        std::cerr << "test failed: banner got [" << rpc.public_rpc_banner() << "] want [" << want << "]\n";
        return false;
    }
    const auto root = addition::dispatch_public_read_rpc(rpc, "GET / HTTP/1.1\r\n\r\n");
    const auto health = addition::dispatch_public_read_rpc(rpc, "GET /health HTTP/1.1\r\n\r\n");
    if (!expect_contains(root, want, "GET / banner") ||
        !expect_contains(health, want, "GET /health banner")) {
        return false;
    }
    if (network_mode != "testnet" && root.find("read-only testnet") != std::string::npos) {
        std::cerr << "test failed: banner hardcoded testnet on " << network_mode << " node\n";
        return false;
    }
    if (network_mode != "mainnet" && root.find("read-only mainnet") != std::string::npos) {
        std::cerr << "test failed: banner hardcoded mainnet on " << network_mode << " node\n";
        return false;
    }
    return true;
}

bool public_reads_return_promptly(addition::RpcServer& rpc, const char* label) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto info = addition::dispatch_public_read_rpc(
        rpc, "GET /rpc?cmd=getinfo HTTP/1.1\r\n\r\n");
    const auto block = addition::dispatch_public_read_rpc(
        rpc, "GET /rpc?cmd=getblock%200 HTTP/1.1\r\n\r\n");
    const auto json = addition::dispatch_public_read_rpc(
        rpc, "GET /jsonrpc?method=getinfo HTTP/1.1\r\n\r\n");
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    if (ms > 2000) {
        std::cerr << "test failed: " << label << " public reads took " << ms << "ms\n";
        return false;
    }
    if (!expect_contains(info, "height=0", "GET getinfo height") ||
        !expect_contains(info, "HTTP/1.1 200 OK", "GET getinfo status") ||
        !expect_contains(block, "height=0", "GET getblock 0") ||
        !expect_contains(json, "\"jsonrpc\":\"2.0\"", "GET jsonrpc getinfo")) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    set_master_key();

    try {
    {
        addition::ChainConfig testnet = addition::testnet_chain_config();
        NodeKit kit(testnet, "testnet");
        if (!check_banner(kit.rpc, "testnet")) {
            return 1;
        }
        if (!public_reads_return_promptly(kit.rpc, "height-0 testnet")) {
            return 1;
        }
        const auto info = addition::dispatch_public_read_rpc(
            kit.rpc, "GET /rpc?cmd=getinfo HTTP/1.1\r\n\r\n");
        if (!expect_contains(info, "network=testnet", "testnet getinfo network")) {
            return 1;
        }
    }

    {
        addition::ChainConfig mainnet = addition::mainnet_chain_config();
        NodeKit kit(mainnet, "mainnet-height0");
        if (kit.chain.height() != 0) {
            std::cerr << "test failed: expected height-0 mainnet fixture\n";
            return 1;
        }
        if (!check_banner(kit.rpc, "mainnet")) {
            return 1;
        }
        if (!public_reads_return_promptly(kit.rpc, "height-0 mainnet")) {
            return 1;
        }
        const auto info = addition::dispatch_public_read_rpc(
            kit.rpc, "GET /rpc?cmd=getinfo HTTP/1.1\r\n\r\n");
        if (!expect_contains(info, "network=mainnet", "mainnet getinfo network") ||
            info.find("network=testnet") != std::string::npos) {
            std::cerr << "test failed: mainnet getinfo network: " << info << '\n';
            return 1;
        }
    }

    {
        // Production mainnet target + no deadline: search runs until stop.
        // Public-read GET must not wait on that hash.
        addition::ChainConfig hard = addition::mainnet_chain_config();
        NodeKit kit(hard, "mainnet-mine");
        if (addition::mine_deadline_seconds(hard) != 0) {
            std::cerr << "test failed: fixture must use mainnet no-deadline mine\n";
            return 1;
        }

        std::atomic<bool> mine_started{false};
        std::atomic<bool> mine_finished{false};
        std::string mine_reply;
        std::thread miner_thread([&]() {
            mine_started.store(true, std::memory_order_release);
            mine_reply = kit.rpc.handle_command("mine miner1", true);
            mine_finished.store(true, std::memory_order_release);
        });

        const auto wait_start = std::chrono::steady_clock::now();
        while (!mine_started.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() - wait_start > std::chrono::seconds(2)) {
                std::cerr << "test failed: mine thread did not start\n";
                kit.miner.request_stop();
                miner_thread.join();
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Give handle_command time to enter mine_next_block / search_block_pow.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (mine_finished.load(std::memory_order_acquire)) {
            std::cerr << "test failed: hard mainnet mine finished before public reads: "
                      << mine_reply << '\n';
            miner_thread.join();
            return 1;
        }

        std::atomic<bool> reads_done{false};
        bool reads_ok = false;
        std::thread reader([&]() {
            reads_ok = public_reads_return_promptly(kit.rpc, "during memory_hard mine");
            reads_done.store(true, std::memory_order_release);
        });
        const auto read_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!reads_done.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < read_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!reads_done.load(std::memory_order_acquire)) {
            std::cerr << "test failed: public-read GET blocked while mine was hashing\n";
            kit.miner.request_stop();
            reader.join();
            miner_thread.join();
            return 1;
        }
        reader.join();
        if (!reads_ok) {
            kit.miner.request_stop();
            miner_thread.join();
            return 1;
        }
        if (mine_finished.load(std::memory_order_acquire)) {
            std::cerr << "test failed: mine finished during public reads; result is inconclusive\n";
            miner_thread.join();
            return 1;
        }

        const auto during = addition::dispatch_public_read_rpc(
            kit.rpc, "GET /rpc?cmd=getinfo HTTP/1.1\r\n\r\n");
        if (!expect_contains(during, "network=mainnet", "getinfo during mine") ||
            !expect_contains(during, "height=0", "getinfo still height 0 during mine")) {
            kit.miner.request_stop();
            miner_thread.join();
            return 1;
        }

        kit.miner.request_stop();
        miner_thread.join();
        if (!mine_finished.load(std::memory_order_acquire)) {
            std::cerr << "test failed: mine thread did not stop\n";
            return 1;
        }
        if (mine_reply.find("mining stopped") == std::string::npos &&
            mine_reply.find("error:") == std::string::npos) {
            std::cerr << "test failed: expected stopped mine, got: " << mine_reply << '\n';
            return 1;
        }
        if (kit.chain.height() != 0) {
            std::cerr << "test failed: cancelled mine must not claim a mainnet block\n";
            return 1;
        }
    }

    std::cout << "all public-read-during-mine tests passed\n";
    return 0;
    } catch (const std::exception& e) {
        std::cerr << "test failed: exception: " << e.what() << '\n';
        return 1;
    }
}
