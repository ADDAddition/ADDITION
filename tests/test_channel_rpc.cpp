// RPC surface for loopback payment channels (test keys only, no network).
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
#include "addition/rpc_server.hpp"
#include "addition/staking.hpp"
#include "addition/token_engine.hpp"
#include "addition/wallet_keys.hpp"

#include <filesystem>
#include <iostream>

static std::string field(const std::string& s, const std::string& k) {
    const auto p = s.find(k + "=");
    if (p == std::string::npos) return {};
    const auto e = s.find_first_of(" \n", p);
    return s.substr(p + k.size() + 1, e == std::string::npos ? std::string::npos : e - p - k.size() - 1);
}

int main() {
    addition::ChainConfig easy = addition::testnet_chain_config();
    easy.initial_difficulty_target = easy.min_difficulty_target = easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    addition::Chain chain(easy);
    addition::Mempool mempool;
    addition::Miner miner(chain, mempool);
    addition::StakingEngine staking;
    addition::TokenEngine tokens;
    addition::PrivacyPool privacy;
    addition::ContractEngine contracts(&tokens, &privacy);
    addition::BridgeEngine bridge;
    addition::PeerNetwork peers;
    addition::ConsensusEngine consensus;
    addition::PoUWStorageEngine pouw_storage;
    addition::PoUWComputeEngine pouw_compute;
    addition::PrivateMessagingEngine private_messaging;
    addition::AIRoutingOptimizer ai_optimizer;
    const auto nk = addition::generate_wallet_keys();
    addition::DecentralizedNode node("self", nk.public_key, nk.private_key, chain, mempool, peers, consensus);
    const auto dir = std::filesystem::temp_directory_path() / "addition-channel-rpc-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    addition::RpcServer rpc(chain, mempool, miner, staking, contracts, bridge, tokens, peers, consensus, privacy,
                            pouw_storage, pouw_compute, private_messaging, ai_optimizer, node, false, true,
                            (dir / "wallets").string());
    int fails = 0;
    auto expect = [&](bool c, const std::string& what, const std::string& got) {
        if (!c) { std::cerr << "FAIL " << what << ": " << got << "\n"; ++fails; }
    };
    rpc.handle_command("createwallet alice");
    rpc.handle_command("createwallet bob");
    const auto aa = field(rpc.handle_command("wallet_info alice"), "address");
    const auto ba = field(rpc.handle_command("wallet_info bob"), "address");
    std::string err;
    chain.credit_balance(aa, 5000, "test", err);

    const auto open = rpc.handle_command("channel_open alice bob 1000");
    expect(open.rfind("ok channel=", 0) == 0, "open", open);
    const auto id = field(open, "channel");
    auto mined = rpc.handle_command("mine miner1 1");
    expect(mined.find("mined block") == 0, "mine funding", mined);
    for (int i = 0; i < 20; ++i) {
        const auto r = rpc.handle_command("channel_pay " + id + " alice 10");
        if (r.rfind("ok", 0) != 0) { expect(false, "pay", r); break; }
    }
    expect(rpc.handle_command("channel_pay " + id + " bob 5").find("bal_b=195") != std::string::npos, "pay back", "");
    expect(rpc.handle_command("channel_pay " + id + " mallory 5").find("error") == 0, "non-party rejected", "");
    const auto list = rpc.handle_command("channel_list");
    expect(list.find("channels=1") == 0 && list.find("state=21") != std::string::npos, "list", list);
    expect(!rpc.handle_command("channel_list", false).empty() &&
               rpc.handle_command("channel_list", false).find("error") == 0,
           "remote denied", rpc.handle_command("channel_list", false));
    const auto close = rpc.handle_command("channel_close " + id);
    expect(close.rfind("ok close_txid=", 0) == 0, "close", close);
    mined = rpc.handle_command("mine miner1 1");
    expect(mined.find("mined block") == 0, "mine close", mined);
    expect(chain.balance_of(ba) == 195, "bob on-chain 195", std::to_string(chain.balance_of(ba)));
    std::filesystem::remove_all(dir);
    if (fails) return 1;
    std::cout << "test_channel_rpc: all passed\n";
    return 0;
}
