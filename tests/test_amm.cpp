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

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool expect_eq(const std::string& got, const std::string& want, const char* label) {
    if (got != want) {
        std::cerr << "test failed: " << label << " got [" << got << "] want [" << want << "]\n";
        return false;
    }
    return true;
}

bool expect_contains(const std::string& hay, const std::string& needle, const char* label) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "test failed: " << label << " missing [" << needle << "] in [" << hay << "]\n";
        return false;
    }
    return true;
}

bool expect_error(const std::string& got, const char* label) {
    if (got.rfind("error:", 0) != 0) {
        std::cerr << "test failed: " << label << " expected error, got [" << got << "]\n";
        return false;
    }
    return true;
}

int test_engine_pool_persist_and_swap() {
    addition::TokenEngine tokens;
    std::string error;
    if (!tokens.create_token("AAA", "alice", 100000, 1000, error) ||
        !tokens.create_token("BBB", "alice", 100000, 1000, error)) {
        std::cerr << "test failed: token_create: " << error << '\n';
        return 1;
    }
    if (tokens.swap_tvl() != 0) {
        std::cerr << "test failed: empty swap_tvl\n";
        return 1;
    }
    if (!tokens.create_pool("AAA", "BBB", 30, error)) {
        std::cerr << "test failed: create_pool: " << error << '\n';
        return 1;
    }
    if (!tokens.add_liquidity("AAA", "BBB", "alice", 200, 200, error)) {
        std::cerr << "test failed: add_liquidity: " << error << '\n';
        return 1;
    }
    if (tokens.swap_tvl() != 400) {
        std::cerr << "test failed: swap_tvl after liquidity got " << tokens.swap_tvl() << '\n';
        return 1;
    }

    std::uint64_t amount_out = 0;
    if (!tokens.swap_exact_in("AAA", "BBB", "alice", 10, 1, amount_out, error) || amount_out == 0) {
        std::cerr << "test failed: swap_exact_in: " << error << '\n';
        return 1;
    }
    const auto tvl_after = tokens.swap_tvl();
    if (tvl_after == 400 || tvl_after == 0) {
        std::cerr << "test failed: swap_tvl must move with reserves, got " << tvl_after << '\n';
        return 1;
    }
    if (tokens.balance_of("AAA", "alice") != 790) {
        std::cerr << "test failed: alice AAA after swap " << tokens.balance_of("AAA", "alice") << '\n';
        return 1;
    }
    if (tokens.balance_of("BBB", "alice") <= 800) {
        std::cerr << "test failed: alice BBB must rise after exact-in\n";
        return 1;
    }

    const auto dumped = tokens.dump_state();
    if (dumped.find("P|AAA|BBB|") == std::string::npos) {
        std::cerr << "test failed: dump must write P|token0|token1|... without embedding key\n";
        return 1;
    }
    if (dumped.find("P|AAA|BBB|AAA|BBB|") != std::string::npos) {
        std::cerr << "test failed: dump still embeds split pool key\n";
        return 1;
    }

    addition::TokenEngine restored;
    if (!restored.load_state(dumped, error)) {
        std::cerr << "test failed: load_state: " << error << '\n';
        return 1;
    }
    if (restored.swap_tvl() != tvl_after) {
        std::cerr << "test failed: restored swap_tvl " << restored.swap_tvl()
                  << " want " << tvl_after << '\n';
        return 1;
    }
    std::uint64_t ra = 0;
    std::uint64_t rb = 0;
    std::uint64_t fee = 0;
    std::uint64_t lp = 0;
    if (!restored.pool_info("AAA", "BBB", ra, rb, fee, lp, error) || ra == 0 || rb == 0) {
        std::cerr << "test failed: restored pool_info: " << error << '\n';
        return 1;
    }

    addition::TokenEngine legacy;
    const std::string legacy_state =
        "T|AAA|AAA|alice|18|0||0||0|0|0|0|0|100000|1000\n"
        "B|AAA|alice|800\n"
        "T|BBB|BBB|alice|18|0||0||0|0|0|0|0|100000|1000\n"
        "B|BBB|alice|800\n"
        "P|AAA|BBB|AAA|BBB|200|200|30|200\n"
        "L|AAA|BBB|alice|200\n";
    if (!legacy.load_state(legacy_state, error)) {
        std::cerr << "test failed: legacy pool line: " << error << '\n';
        return 1;
    }
    if (legacy.swap_tvl() != 400) {
        std::cerr << "test failed: legacy swap_tvl " << legacy.swap_tvl() << '\n';
        return 1;
    }
    return 0;
}

int test_engine_rejects_bad_inputs() {
    addition::TokenEngine tokens;
    std::string error;
    if (!tokens.create_token("AAA", "alice", 100000, 1000, error) ||
        !tokens.create_token("BBB", "alice", 100000, 1000, error) ||
        !tokens.create_token("CCC", "alice", 100000, 1000, error)) {
        std::cerr << "test failed: token_create: " << error << '\n';
        return 1;
    }

    if (tokens.create_pool("AAA", "AAA", 30, error)) {
        std::cerr << "test failed: same-token pool must fail\n";
        return 1;
    }
    if (tokens.create_pool("AAA", "NOPE", 30, error)) {
        std::cerr << "test failed: missing token pool must fail\n";
        return 1;
    }
    if (tokens.create_pool("AAA", "BBB", 10000, error)) {
        std::cerr << "test failed: fee_bps 10000 must fail\n";
        return 1;
    }
    if (!tokens.create_pool("AAA", "CCC", 0, error)) {
        std::cerr << "test failed: fee_bps 0 must be allowed: " << error << '\n';
        return 1;
    }
    if (tokens.create_pool("AA|A", "BBB", 30, error)) {
        std::cerr << "test failed: pipe in token name must fail\n";
        return 1;
    }
    if (!tokens.create_pool("BBB", "AAA", 30, error)) {
        std::cerr << "test failed: ordered pair create: " << error << '\n';
        return 1;
    }
    if (tokens.create_pool("AAA", "BBB", 30, error)) {
        std::cerr << "test failed: duplicate pool must fail\n";
        return 1;
    }

    std::uint64_t amount_out = 0;
    if (tokens.swap_exact_in("AAA", "BBB", "alice", 10, 1, amount_out, error)) {
        std::cerr << "test failed: swap without liquidity must fail\n";
        return 1;
    }
    if (!tokens.add_liquidity("AAA", "BBB", "alice", 200, 200, error)) {
        std::cerr << "test failed: add_liquidity: " << error << '\n';
        return 1;
    }
    if (tokens.swap_exact_in("AAA", "BBB", "alice", 10, 999999, amount_out, error)) {
        std::cerr << "test failed: slippage must fail\n";
        return 1;
    }
    if (tokens.swap_exact_in("AAA", "CCC", "alice", 10, 1, amount_out, error)) {
        std::cerr << "test failed: missing pool must fail\n";
        return 1;
    }
    if (tokens.swap_exact_in("AAA", "BBB", "bob", 10, 1, amount_out, error)) {
        std::cerr << "test failed: empty trader balance must fail\n";
        return 1;
    }
    if (tokens.load_state("P|not-a-pool-line\n", error)) {
        std::cerr << "test failed: mutated pool dump must be rejected\n";
        return 1;
    }
    return 0;
}

int test_rpc_amm_path() {
    addition::ChainConfig easy = addition::testnet_chain_config();
    easy.initial_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.min_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
    easy.max_difficulty_target = 0xFFFFFFFFFFFFFFFFULL;
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
    addition::WalletKeys keys{};
    try {
        keys = addition::generate_wallet_keys();
    } catch (const std::exception& e) {
        std::cerr << "test failed: generate_wallet_keys: " << e.what() << '\n';
        return 1;
    }
    addition::DecentralizedNode node("self",
                                     keys.public_key,
                                     keys.private_key,
                                     chain,
                                     mempool,
                                     peers,
                                     consensus);
    const auto rpc_dir = std::filesystem::temp_directory_path() / "addition-amm-rpc";
    std::filesystem::remove_all(rpc_dir);
    std::filesystem::create_directories(rpc_dir);
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

    if (!expect_eq(rpc.handle_command("swap_tvl"), "tvl=0", "swap_tvl empty")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("token_create AAA alice 100000 1000"), "ok", "token_create AAA") ||
        !expect_eq(rpc.handle_command("token_create BBB alice 100000 1000"), "ok", "token_create BBB")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("swap_pool_create AAA BBB 30"), "ok", "swap_pool_create")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("add_liquidity AAA BBB alice 200 200"), "ok", "add_liquidity")) {
        return 1;
    }
    const auto pool = rpc.handle_command("swap_pool_info AAA BBB");
    if (!expect_contains(pool, "reserve_AAA=200", "reserve A") ||
        !expect_contains(pool, "reserve_BBB=200", "reserve B")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("swap_tvl"), "tvl=400", "swap_tvl live")) {
        return 1;
    }
    const auto swapped = rpc.handle_command("swap_exact_in AAA BBB alice 10 1");
    if (swapped.rfind("ok:amount_out=", 0) != 0) {
        std::cerr << "test failed: swap_exact_in: " << swapped << '\n';
        return 1;
    }
    if (!expect_eq(rpc.handle_command("token_balance AAA alice"), "790", "AAA after swap")) {
        return 1;
    }
    const auto tvl_after = rpc.handle_command("swap_tvl");
    if (tvl_after == "tvl=400" || tvl_after == "tvl=0" || tvl_after.rfind("tvl=", 0) != 0) {
        std::cerr << "test failed: swap_tvl after exact-in: " << tvl_after << '\n';
        return 1;
    }

    if (!expect_error(rpc.handle_command("swap_pool_create AAA BBB 30 EXTRA"), "trailing create") ||
        !expect_error(rpc.handle_command("swap_pool_create AAA BBB 30abc"), "mutated fee") ||
        !expect_error(rpc.handle_command("swap_pool_create AAA AAA 30"), "same pair") ||
        !expect_error(rpc.handle_command("swap_exact_in AAA BBB alice 10 1 EXTRA"), "trailing swap") ||
        !expect_error(rpc.handle_command("swap_exact_in AAA BBB alice -1 1"), "negative amount") ||
        !expect_error(rpc.handle_command("add_liquidity AAA BBB alice 10 0"), "zero liquidity")) {
        return 1;
    }

    const char* blocked[] = {
        "swap_pool_create",
        "swap_exact_in",
        "add_liquidity",
        "swap_add_liquidity",
    };
    for (const char* method : blocked) {
        if (addition::is_public_read_command(method)) {
            std::cerr << "test failed: public allowlist must not include " << method << '\n';
            return 1;
        }
        const auto text = addition::dispatch_public_read_rpc(rpc, method);
        if (text.find("command disabled on public RPC") == std::string::npos) {
            std::cerr << "test failed: public TEXT must reject " << method << ": " << text << '\n';
            return 1;
        }
        const auto http = addition::dispatch_public_read_rpc(
            rpc, std::string("GET /rpc?cmd=") + method + " HTTP/1.1\r\n\r\n");
        if (http.find("command disabled on public RPC") == std::string::npos) {
            std::cerr << "test failed: public HTTP must reject " << method << ": " << http << '\n';
            return 1;
        }
        const auto json = addition::dispatch_public_read_rpc(
            rpc, std::string("GET /jsonrpc?method=") + method + " HTTP/1.1\r\n\r\n");
        if (json.find("command disabled on public RPC") == std::string::npos) {
            std::cerr << "test failed: public JSON must reject " << method << ": " << json << '\n';
            return 1;
        }
    }

    std::filesystem::remove_all(rpc_dir);
    return 0;
}

} // namespace

int main() {
    if (int rc = test_engine_pool_persist_and_swap()) {
        return rc;
    }
    if (int rc = test_engine_rejects_bad_inputs()) {
        return rc;
    }
    if (int rc = test_rpc_amm_path()) {
        return rc;
    }
    std::cout << "all amm tests passed\n";
    return 0;
}
