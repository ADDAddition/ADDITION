#include "addition/ai_optimizer.hpp"
#include "addition/block.hpp"
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

std::string to_hex_ascii(const std::string& s) {
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 0x0f]);
    }
    return out;
}

std::string kv(const std::string& line, const std::string& key) {
    const auto needle = key + "=";
    const auto pos = line.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + needle.size();
    const auto end = line.find(' ', start);
    return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
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
    const auto miner_addr = kv(created, "address");
    const auto miner_pub = kv(created, "pub");

    const auto mined = rpc.handle_command("mine " + miner_addr);
    if (mined.rfind("mined block", 0) != 0) {
        std::cerr << "test failed: mine: " << mined << '\n';
        return 1;
    }
    if (!expect_eq(rpc.handle_command("getbalance " + miner_addr), "50", "getbalance after mine")) {
        return 1;
    }

    const auto sent = rpc.handle_command("wallet_send miner bob 10 1");
    if (!expect_contains(sent, "ok:gossiped", "wallet_send")) {
        return 1;
    }
    const auto sent2 = rpc.handle_command("wallet_send miner carol 5 1");
    if (sent2.rfind("error:", 0) != 0 && sent2.find("ok:gossiped") == std::string::npos) {
        std::cerr << "test failed: second wallet_send unexpected: " << sent2 << '\n';
        return 1;
    }
    const auto mined_send = rpc.handle_command("mine " + miner_addr);
    if (mined_send.rfind("mined block", 0) != 0) {
        std::cerr << "test failed: mine after wallet_send: " << mined_send << '\n';
        return 1;
    }
    if (chain.balance_of("bob") < 10) {
        std::cerr << "test failed: bob balance after wallet_send mine\n";
        return 1;
    }

    addition::Transaction leftover{};
    leftover.signer = "leftover";
    leftover.signer_pubkey = "00";
    leftover.signature = "pq=dead";
    leftover.fee = 1;
    leftover.nonce = 99;
    leftover.inputs.push_back(addition::TxInput{"deadbeef", 0});
    leftover.outputs.push_back(addition::TxOutput{"poison", 1});
    if (!mempool.submit(leftover)) {
        std::cerr << "test failed: leftover spent-input tx should enter mempool\n";
        return 1;
    }
    addition::Transaction fake_coinbase{};
    fake_coinbase.signer = "bench_signer";
    fake_coinbase.signer_pubkey = "bench_pub";
    fake_coinbase.signature = "pq=bench|privacy";
    fake_coinbase.fee = 10;
    fake_coinbase.outputs.push_back(addition::TxOutput{"bench_to", 1});
    mempool.submit(fake_coinbase);

    const auto mine_leftover = rpc.handle_command("mine " + miner_addr);
    if (mine_leftover.rfind("mined block", 0) != 0) {
        std::cerr << "test failed: leftover mempool must not block mine: " << mine_leftover << '\n';
        return 1;
    }

    mempool.submit(leftover);
    mempool.submit(fake_coinbase);
    const auto bench = rpc.handle_command("benchmark_objective 1 2");
    if (bench.rfind("error:", 0) == 0 ||
        bench.find("invalid or spent input") != std::string::npos) {
        std::cerr << "test failed: benchmark_objective: " << bench << '\n';
        return 1;
    }
    if (!expect_contains(bench, "objective_privacy_ok=false", "bench privacy_ok") ||
        !expect_contains(bench, "privacy_claim=opening_not_zk", "bench claim") ||
        !expect_contains(bench, "opening_hash_ok=true", "bench opening") ||
        !expect_contains(bench, "zk_circuit=0", "bench zk_circuit")) {
        return 1;
    }

    const auto pstat = rpc.handle_command("protocol_status");
    if (!expect_contains(pstat, "objective_privacy_ok=false", "protocol_status privacy_ok") ||
        !expect_contains(pstat, "privacy_claim=opening_not_zk", "protocol_status claim") ||
        !expect_contains(pstat, "zk_circuit=0", "protocol_status zk_circuit")) {
        return 1;
    }

    const auto unlocked_before_stake = std::stoull(rpc.handle_command("getbalance " + miner_addr));
    const auto stake_amt = unlocked_before_stake > 10 ? (unlocked_before_stake - 5) : unlocked_before_stake;
    const auto stake = rpc.handle_command("stake " + miner_addr + " " + std::to_string(stake_amt));
    if (!expect_eq(stake, "ok", "stake")) {
        return 1;
    }
    if (!expect_eq(rpc.handle_command("getbalance " + miner_addr),
                   std::to_string(chain.balance_of(miner_addr) - stake_amt),
                   "getbalance after stake") ||
        !expect_eq(rpc.handle_command("staked " + miner_addr), std::to_string(stake_amt), "staked after stake")) {
        return 1;
    }
    const auto overspend = rpc.handle_command("wallet_send miner dave 10 1");
    if (overspend.find("insufficient unlocked") == std::string::npos) {
        std::cerr << "test failed: wallet_send must refuse staked coins: " << overspend << '\n';
        return 1;
    }
    const auto reward = rpc.handle_command("stake_reward 100000");
    if (!expect_eq(reward, "ok", "stake_reward")) {
        return 1;
    }
    const auto claimed = rpc.handle_command("stake_claim " + miner_addr);
    if (claimed == "0" || claimed.rfind("error:", 0) == 0) {
        std::cerr << "test failed: stake_claim should credit a reward, got " << claimed << '\n';
        return 1;
    }
    const auto unstake = rpc.handle_command("unstake " + miner_addr + " " + std::to_string(stake_amt));
    if (!expect_eq(unstake, "ok", "unstake")) {
        return 1;
    }

    if (!expect_eq(rpc.handle_command("token_create AAA alice 100000 1000"), "ok", "token_create AAA") ||
        !expect_eq(rpc.handle_command("token_create BBB alice 100000 1000"), "ok", "token_create BBB") ||
        !expect_eq(rpc.handle_command("token_create CCC alice 100000 1000"), "ok", "token_create CCC")) {
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

    const std::string nft_meta = "https://example.com/nft/item1.png#sha3-512:deadbeef";
    if (!expect_eq(rpc.handle_command("nft_mint COL item1 alice " + nft_meta), "ok", "nft_mint") ||
        !expect_eq(rpc.handle_command("nft_transfer COL item1 alice bob"), "ok", "nft_transfer") ||
        !expect_eq(rpc.handle_command("nft_owner COL item1"), "bob", "nft_owner after transfer")) {
        return 1;
    }
    const auto nft_info = rpc.handle_command("nft_info COL item1");
    if (!expect_contains(nft_info, "owner=bob", "nft_info owner") ||
        !expect_contains(nft_info, std::string("metadata=") + nft_meta, "nft_info metadata")) {
        return 1;
    }

    if (!expect_eq(rpc.handle_command("swap_pool_create AAA BBB 30"), "ok", "swap_pool_create AB") ||
        !expect_eq(rpc.handle_command("swap_pool_create BBB CCC 30"), "ok", "swap_pool_create BC") ||
        !expect_eq(rpc.handle_command("add_liquidity AAA BBB alice 200 200"), "ok", "add_liquidity AB") ||
        !expect_eq(rpc.handle_command("add_liquidity BBB CCC alice 200 200"), "ok", "add_liquidity BC")) {
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
    const auto route_q = rpc.handle_command("swap_quote_route AAA>BBB>CCC 8");
    if (route_q.rfind("error:", 0) == 0) {
        std::cerr << "test failed: swap_quote_route: " << route_q << '\n';
        return 1;
    }
    const auto route_x = rpc.handle_command("swap_route_exact_in AAA>BBB>CCC alice 8 1");
    if (route_x.rfind("error:", 0) == 0) {
        std::cerr << "test failed: swap_route_exact_in: " << route_x << '\n';
        return 1;
    }
    const auto best = rpc.handle_command("swap_best_route AAA CCC 5 3");
    if (!expect_contains(best, "route=", "swap_best_route")) {
        return 1;
    }

    if (!expect_eq(rpc.handle_command("token_mint AAA alice " + miner_addr + " 40"), "ok", "mint to miner addr")) {
        return 1;
    }
    const std::string deadline = "2000000000";
    const auto payload = rpc.handle_command(
        "swap_best_route_sign_payload AAA BBB " + miner_addr + " 5 1 " + deadline + " 3");
    if (payload.rfind("swap_best_route_exact_in|", 0) != 0) {
        std::cerr << "test failed: sign payload: " << payload << '\n';
        return 1;
    }
    const auto signed_msg = rpc.handle_command("wallet_sign miner " + to_hex_ascii(payload));
    if (signed_msg.rfind("pq=", 0) != 0) {
        std::cerr << "test failed: wallet_sign: " << signed_msg << '\n';
        return 1;
    }
    const auto sig = signed_msg.substr(3);
    const auto signed_swap = rpc.handle_command(
        "swap_best_route_exact_in_signed AAA BBB " + miner_addr + " 5 1 " + deadline +
        " 3 " + miner_pub + " " + sig);
    if (signed_swap.rfind("ok:amount_out=", 0) != 0) {
        std::cerr << "test failed: signed best-route: " << signed_swap << '\n';
        return 1;
    }

    const auto cid = rpc.handle_command("contract_deploy alice kvstore");
    if (cid.rfind("error:", 0) == 0 || cid.empty()) {
        std::cerr << "test failed: contract_deploy: " << cid << '\n';
        return 1;
    }
    if (!expect_eq(rpc.handle_command("contract_call " + cid + " set counter 7"), "ok", "contract set") ||
        !expect_eq(rpc.handle_command("contract_call " + cid + " add counter 3"), "10", "contract add") ||
        !expect_eq(rpc.handle_command("contract_call " + cid + " get counter 0"), "10", "contract get")) {
        return 1;
    }

    const auto prep = rpc.handle_command("privacy_note_prepare 5");
    if (!expect_contains(prep, "verifier=sha3_opening", "prepare verifier") ||
        !expect_contains(prep, "claim=opening_not_zk", "prepare claim")) {
        return 1;
    }
    const auto trap = kv(prep, "trapdoor");
    const auto cm = kv(prep, "commitment");
    const auto nf = kv(prep, "nullifier");
    const auto mint_open = rpc.handle_command("privacy_mint_open alice 5 " + cm + " " + nf + " " + trap);
    if (!expect_contains(mint_open, "ok:note_id=", "privacy_mint_open")) {
        return 1;
    }
    const auto note_id = kv(mint_open, "ok:note_id");
    const auto spent_note = rpc.handle_command("privacy_spend_open alice " + note_id + " bob 2 " + trap);
    if (!expect_contains(spent_note, "ok:spent", "privacy_spend_open")) {
        return 1;
    }
    const auto zk_bad = rpc.handle_command("privacy_mint_zk alice 1 aa bb cc dd");
    if (zk_bad.rfind("error:", 0) != 0) {
        std::cerr << "test failed: privacy_mint_zk garbage must error, got " << zk_bad << '\n';
        return 1;
    }

    if (rpc.handle_command("mine", false).find("error:") != 0 ||
        rpc.handle_command("createwallet eve", false).find("error:") != 0) {
        std::cerr << "test failed: untrusted RPC must reject mine/createwallet\n";
        return 1;
    }
    if (!addition::is_public_read_command("getblockraw") ||
        addition::is_public_read_command("mine") ||
        addition::is_public_read_command("wallet_send") ||
        addition::is_public_read_command("swap_tvl")) {
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
