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

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace {

std::filesystem::path make_temp_dir(const std::string& tag) {
    const auto root = std::filesystem::temp_directory_path() / ("addition-runtime-test-" + tag);
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

bool expect_eq(const std::string& got, const std::string& want, const char* label) {
    if (got != want) {
        std::cerr << "test failed: " << label << " got [" << got << "] want [" << want << "]\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    set_master_key();

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

    const auto selftest = rpc.handle_command("crypto_selftest");
    if (selftest.rfind("ok:", 0) != 0) {
        std::cerr << "test failed: crypto_selftest: " << selftest << '\n';
        return 1;
    }

    const auto created = rpc.handle_command("createwallet miner");
    if (!expect_contains(created, "algo=ml-dsa-87", "createwallet algo")) {
        return 1;
    }
    const auto addr_pos = created.find("address=");
    const auto addr_end = created.find(' ', addr_pos);
    const auto miner_addr = created.substr(addr_pos + 8, addr_end - addr_pos - 8);

    std::string error;
    std::string mined_hash;
    if (!miner.mine_next_block(miner_addr, 200, 1, mined_hash, error)) {
        std::cerr << "test failed: mine: " << error << '\n';
        return 1;
    }
    const auto bal0 = rpc.handle_command("getbalance " + miner_addr);
    if (!expect_eq(bal0, "50", "getbalance after mine")) {
        return 1;
    }

    const auto stake = rpc.handle_command("stake " + miner_addr + " 40");
    if (!expect_eq(stake, "ok", "stake")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("getbalance " + miner_addr), "10", "getbalance after stake") ||
        !expect_eq(rpc.handle_command("staked " + miner_addr), "40", "staked after stake")) {
        return 1;
    }
    const auto reward = rpc.handle_command("stake_reward 100");
    if (!expect_eq(reward, "ok", "stake_reward")) {
        return 1;
    }
    const auto claimed = rpc.handle_command("stake_claim " + miner_addr);
    if (claimed == "0" || claimed.rfind("error:", 0) == 0) {
        std::cerr << "test failed: stake_claim should credit a reward, got " << claimed << '\n';
        return 1;
    }
    const auto after_claim = std::stoull(rpc.handle_command("getbalance " + miner_addr));
    if (after_claim <= 10) {
        std::cerr << "test failed: getbalance after stake_claim must rise, got " << after_claim << '\n';
        return 1;
    }
    const auto unstake = rpc.handle_command("unstake " + miner_addr + " 40");
    if (!expect_eq(unstake, "ok", "unstake")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("staked " + miner_addr), "0", "staked after unstake")) {
        return 1;
    }
    const auto after_unstake = std::stoull(rpc.handle_command("getbalance " + miner_addr));
    if (after_unstake <= after_claim) {
        std::cerr << "test failed: unstake must unlock coins, got " << after_unstake
                  << " after claim " << after_claim << '\n';
        return 1;
    }

    if (!expect_eq(rpc.handle_command("token_create AAA alice 100000 1000"), "ok", "token_create AAA") ||
        !expect_eq(rpc.handle_command("token_create BBB alice 100000 1000"), "ok", "token_create BBB")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("token_mint AAA alice bob 50"), "ok", "token_mint") ||
        !expect_eq(rpc.handle_command("token_transfer AAA alice bob 10"), "ok", "token_transfer")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("token_balance AAA alice"), "990", "token_balance alice") ||
        !expect_eq(rpc.handle_command("token_balance AAA bob"), "60", "token_balance bob")) {
        return 1;
    }
    const auto info = rpc.handle_command("token_info AAA");
    if (!expect_contains(info, "symbol=AAA", "token_info symbol") ||
        !expect_contains(info, "total_supply=1050", "token_info supply")) {
        return 1;
    }

    const std::string nft_meta = "https://example.com/nft/item1.png#sha3-512:deadbeef";
    const auto minted_nft = rpc.handle_command("nft_mint COL item1 alice " + nft_meta);
    if (!expect_eq(minted_nft, "ok", "nft_mint")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("nft_owner COL item1"), "alice", "nft_owner after mint")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("nft_transfer COL item1 alice bob"), "ok", "nft_transfer")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("nft_owner COL item1"), "bob", "nft_owner after transfer")) {
        return 1;
    }
    const auto nft_info = rpc.handle_command("nft_info COL item1");
    if (!expect_contains(nft_info, "owner=bob", "nft_info owner") ||
        !expect_contains(nft_info, std::string("metadata=") + nft_meta, "nft_info metadata")) {
        return 1;
    }

    if (!expect_eq(rpc.handle_command("swap_tvl"), "tvl=0", "swap_tvl empty")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("swap_pool_create AAA BBB 30"), "ok", "swap_pool_create")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("add_liquidity AAA BBB alice 200 200"), "ok", "add_liquidity")) {
        return 1;
    }
    const auto pool = rpc.handle_command("swap_pool_info AAA BBB");
    if (!expect_contains(pool, "reserve_AAA=200", "pool reserve A") ||
        !expect_contains(pool, "reserve_BBB=200", "pool reserve B")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("swap_tvl"), "tvl=400", "swap_tvl live reserves")) {
        return 1;
    }
    const auto quote = rpc.handle_command("swap_quote AAA BBB 10");
    if (quote.rfind("error:", 0) == 0 || quote == "0") {
        std::cerr << "test failed: swap_quote: " << quote << '\n';
        return 1;
    }
    const auto swapped = rpc.handle_command("swap_exact_in AAA BBB alice 10 1");
    if (swapped.rfind("error:", 0) == 0) {
        std::cerr << "test failed: swap_exact_in: " << swapped << '\n';
        return 1;
    }
    const auto alice_aaa = std::stoull(rpc.handle_command("token_balance AAA alice"));
    const auto alice_bbb = std::stoull(rpc.handle_command("token_balance BBB alice"));
    if (alice_aaa != 780 || alice_bbb <= 800) {
        std::cerr << "test failed: swap did not move balances aaa=" << alice_aaa
                  << " bbb=" << alice_bbb << '\n';
        return 1;
    }

    const auto cid = rpc.handle_command("contract_deploy alice kvstore");
    if (cid.rfind("error:", 0) == 0 || cid.empty()) {
        std::cerr << "test failed: contract_deploy: " << cid << '\n';
        return 1;
    }
    if (!expect_eq(rpc.handle_command("contract_call " + cid + " set counter 7"), "ok", "contract set")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("contract_call " + cid + " add counter 3"), "10", "contract add")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("contract_call " + cid + " get counter 0"), "10", "contract get")) {
        return 1;
    }

    const auto prep = rpc.handle_command("privacy_note_prepare 5");
    if (!expect_contains(prep, "verifier=sha3_opening", "prepare verifier") ||
        !expect_contains(prep, "claim=opening_not_zk", "prepare claim")) {
        return 1;
    }
    const auto trap_pos = prep.find("trapdoor=");
    const auto cm_pos = prep.find("commitment=");
    const auto nf_pos = prep.find("nullifier=");
    const auto trap = prep.substr(trap_pos + 9, prep.find(' ', trap_pos) - trap_pos - 9);
    const auto cm = prep.substr(cm_pos + 11, prep.find(' ', cm_pos) - cm_pos - 11);
    const auto nf = prep.substr(nf_pos + 10, prep.find(' ', nf_pos) - nf_pos - 10);
    const auto mint_open = rpc.handle_command("privacy_mint_open alice 5 " + cm + " " + nf + " " + trap);
    if (!expect_contains(mint_open, "ok:note_id=", "privacy_mint_open")) {
        return 1;
    }
    const auto note_id = mint_open.substr(std::string("ok:note_id=").size(),
                                          mint_open.find(' ') == std::string::npos
                                              ? std::string::npos
                                              : mint_open.find(' ') - std::string("ok:note_id=").size());
    const auto spent = rpc.handle_command("privacy_spend_open alice " + note_id + " bob 2 " + trap);
    if (!expect_contains(spent, "ok:spent", "privacy_spend_open")) {
        return 1;
    }
    const auto pstatus = rpc.handle_command("privacy_status");
    if (!expect_contains(pstatus, "opening_verifier=sha3_opening", "privacy_status verifier")) {
        return 1;
    }
    const auto zk_bad = rpc.handle_command("privacy_mint_zk alice 1 aa bb cc dd");
    if (zk_bad.rfind("error:", 0) != 0) {
        std::cerr << "test failed: privacy_mint_zk stub/garbage must error, got " << zk_bad << '\n';
        return 1;
    }

    if (rpc.handle_command("mine", false).find("error:") != 0 ||
        rpc.handle_command("createwallet eve", false).find("error:") != 0) {
        std::cerr << "test failed: untrusted RPC must reject mine/createwallet\n";
        return 1;
    }
    if (!addition::is_public_read_command("getblockraw") ||
        addition::is_public_read_command("mine") ||
        addition::is_public_read_command("wallet_send")) {
        std::cerr << "test failed: public allowlist\n";
        return 1;
    }

    const auto json_get = addition::dispatch_public_read_rpc(
        rpc, "GET /jsonrpc?method=getinfo HTTP/1.1\r\n\r\n");
    if (json_get.find("\"jsonrpc\":\"2.0\"") == std::string::npos ||
        json_get.find("network=testnet") == std::string::npos ||
        json_get.find("application/json") == std::string::npos) {
        std::cerr << "test failed: public JSON getinfo: " << json_get << '\n';
        return 1;
    }
    const auto json_write = addition::dispatch_public_read_rpc(
        rpc, "GET /jsonrpc?method=mine HTTP/1.1\r\n\r\n");
    if (json_write.find("command disabled on public RPC") == std::string::npos) {
        std::cerr << "test failed: public JSON must reject mine: " << json_write << '\n';
        return 1;
    }
    const char* public_json_blocked[] = {
        "wallet_send",
        "stake",
        "unstake",
        "swap_exact_in",
        "add_liquidity",
    };
    for (const char* write_method : public_json_blocked) {
        const auto json_blocked = addition::dispatch_public_read_rpc(
            rpc, std::string("GET /jsonrpc?method=") + write_method + " HTTP/1.1\r\n\r\n");
        if (json_blocked.find("command disabled on public RPC") == std::string::npos) {
            std::cerr << "test failed: public JSON must reject " << write_method
                      << ": " << json_blocked << '\n';
            return 1;
        }
    }
    const auto json_raw = addition::dispatch_public_read_rpc(
        rpc, "GET /jsonrpc?method=getblockraw&params=1 HTTP/1.1\r\n\r\n");
    if (json_raw.find("ok:BLKDATA|") == std::string::npos &&
        json_raw.find("\"result\"") == std::string::npos) {
        std::cerr << "test failed: public JSON getblockraw: " << json_raw << '\n';
        return 1;
    }

    std::filesystem::remove_all(rpc_dir);
    std::cout << "all runtime function tests passed\n";
    return 0;
}
