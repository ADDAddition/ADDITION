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
#include "addition/wallet.hpp"
#include "addition/wallet_keys.hpp"
#include "addition/wallet_store.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path make_temp_dir(const std::string& tag) {
    const auto root = std::filesystem::temp_directory_path() / ("addition-wallet-test-" + tag);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

bool expect_contains(const std::string& hay, const std::string& needle, const char* label) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "test failed: " << label << " missing [" << needle << "] in [" << hay << "]\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (addition::WalletStore::valid_name("") ||
        addition::WalletStore::valid_name("../etc") ||
        addition::WalletStore::valid_name("has space") ||
        !addition::WalletStore::valid_name("default") ||
        !addition::WalletStore::valid_name("Alice_1")) {
        std::cerr << "test failed: wallet name validation\n";
        return 1;
    }

    const auto store_dir = make_temp_dir("store");
    addition::WalletStore store(store_dir.string());
    addition::WalletKeys keys{};
    try {
        keys = addition::generate_wallet_keys();
    } catch (const std::exception& e) {
        std::cerr << "test failed: generate_wallet_keys: " << e.what() << '\n';
        return 1;
    }
    if (keys.algorithm != "ml-dsa-87" || keys.address.empty() || keys.private_key.empty()) {
        std::cerr << "test failed: ML-DSA-87 key material incomplete\n";
        return 1;
    }

    addition::StoredWallet created{};
    std::string error;
    if (!store.create("alice", keys, created, error)) {
        std::cerr << "test failed: create wallet: " << error << '\n';
        return 1;
    }
    if (created.private_key.empty() == false) {
        // create() must not echo the private key in the returned public view
        std::cerr << "test failed: create() leaked private_key in StoredWallet\n";
        return 1;
    }
    if (created.address != keys.address || created.path.find("alice.wal") == std::string::npos) {
        std::cerr << "test failed: created wallet metadata\n";
        return 1;
    }
    if (!store.exists("alice")) {
        std::cerr << "test failed: exists after create\n";
        return 1;
    }
    if (store.create("alice", keys, created, error)) {
        std::cerr << "test failed: duplicate create should fail\n";
        return 1;
    }

    addition::StoredWallet loaded{};
    if (!store.load("alice", loaded, error, true) || loaded.private_key != keys.private_key) {
        std::cerr << "test failed: load private: " << error << '\n';
        return 1;
    }
    addition::StoredWallet public_view{};
    if (!store.load("alice", public_view, error, false) || !public_view.private_key.empty()) {
        std::cerr << "test failed: public load must omit private_key\n";
        return 1;
    }
    const auto listed = store.list(error);
    if (listed.size() != 1 || listed[0].name != "alice" || !listed[0].private_key.empty()) {
        std::cerr << "test failed: list contents\n";
        return 1;
    }

    addition::ChainConfig easy = addition::testnet_chain_config();
    easy.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    addition::Chain chain(easy);
    if (chain.next_nonce(keys.address) != 1) {
        std::cerr << "test failed: first next_nonce must be 1\n";
        return 1;
    }
    if (!chain.credit_balance(keys.address, 100, "wallet_test_seed", error)) {
        std::cerr << "test failed: credit: " << error << '\n';
        return 1;
    }

    addition::Mempool mempool;
    addition::Wallet wallet(keys.address, keys.public_key, keys.private_key);
    if (!wallet.send(mempool, chain, "bob", 20, 1, error)) {
        std::cerr << "test failed: Wallet::send: " << error << '\n';
        return 1;
    }

    addition::Miner miner(chain, mempool);
    std::string mined_hash;
    if (!miner.mine_next_block("miner1", 200, 1, mined_hash, error)) {
        std::cerr << "test failed: mine after send: " << error << '\n';
        return 1;
    }
    if (chain.balance_of("bob") != 20 || chain.balance_of(keys.address) < 79) {
        std::cerr << "test failed: UTXO balances after spend\n";
        return 1;
    }
    if (chain.last_nonce(keys.address) != 1 || chain.next_nonce(keys.address) != 2) {
        std::cerr << "test failed: nonce after confirmed spend\n";
        return 1;
    }

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
    addition::DecentralizedNode node("self",
                                     keys.public_key,
                                     keys.private_key,
                                     chain,
                                     mempool,
                                     peers,
                                     consensus);
    const auto rpc_dir = make_temp_dir("rpc");
    addition::RpcServer rpc(chain,
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
                            (rpc_dir / "wallets").string());

    const auto created_rpc = rpc.handle_command("createwallet demo");
    if (!expect_contains(created_rpc, "algo=ml-dsa-87", "createwallet algo") ||
        !expect_contains(created_rpc, "name=demo", "createwallet name") ||
        !expect_contains(created_rpc, "priv_printed=0", "createwallet priv") ||
        created_rpc.find("priv=") != std::string::npos) {
        std::cerr << "test failed: createwallet response: " << created_rpc << '\n';
        return 1;
    }
    if (rpc.handle_command("createwallet demo").find("error:") != 0) {
        std::cerr << "test failed: duplicate createwallet should error\n";
        return 1;
    }
    const auto listed_rpc = rpc.handle_command("wallet_list");
    if (!expect_contains(listed_rpc, "wallets=1", "wallet_list count") ||
        !expect_contains(listed_rpc, "name=demo", "wallet_list name")) {
        return 1;
    }
    const auto info = rpc.handle_command("wallet_info demo");
    if (!expect_contains(info, "address=", "wallet_info address") ||
        !expect_contains(info, "confirmed=0", "wallet_info confirmed") ||
        info.find("private_key") != std::string::npos) {
        std::cerr << "test failed: wallet_info: " << info << '\n';
        return 1;
    }

    const auto addr_pos = info.find("address=");
    const auto addr_end = info.find(' ', addr_pos);
    const auto demo_addr = info.substr(addr_pos + 8, addr_end - addr_pos - 8);
    if (!chain.credit_balance(demo_addr, 80, "rpc_wallet_seed", error)) {
        std::cerr << "test failed: credit demo: " << error << '\n';
        return 1;
    }
    const auto bal = rpc.handle_command("wallet_balance demo");
    if (!expect_contains(bal, "confirmed=80", "wallet_balance")) {
        return 1;
    }

    const auto sent = rpc.handle_command("wallet_send demo bob 10 1");
    if (!expect_contains(sent, "ok:gossiped", "wallet_send gossip") ||
        !expect_contains(sent, "amount=10", "wallet_send amount") ||
        sent.find("priv") != std::string::npos) {
        std::cerr << "test failed: wallet_send: " << sent << '\n';
        return 1;
    }
    if (!miner.mine_next_block("miner1", 200, 1, mined_hash, error)) {
        std::cerr << "test failed: mine wallet_send: " << error << '\n';
        return 1;
    }
    if (chain.balance_of("bob") != 30) {
        std::cerr << "test failed: bob should have 30 after second spend, got "
                  << chain.balance_of("bob") << '\n';
        return 1;
    }

    if (rpc.handle_command("createwallet", false).find("error:") != 0 ||
        rpc.handle_command("wallet_send demo bob 1 1", false).find("error:") != 0) {
        std::cerr << "test failed: wallet writes must be denied on untrusted RPC\n";
        return 1;
    }
    if (addition::is_public_read_command("createwallet") ||
        addition::is_public_read_command("wallet_send") ||
        addition::is_public_read_command("wallet_sign") ||
        addition::is_public_read_command("wallet_list")) {
        std::cerr << "test failed: wallet commands must stay off the public allowlist\n";
        return 1;
    }

    std::filesystem::remove_all(store_dir);
    std::filesystem::remove_all(rpc_dir);
    std::cout << "all wallet tests passed\n";
    return 0;
}
