#include "addition/chain.hpp"
#include "addition/bridge.hpp"
#include "addition/contract_engine.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/p2p.hpp"
#include "addition/pouw_compute.hpp"
#include "addition/pouw_storage.hpp"
#include "addition/private_messaging.hpp"
#include "addition/privacy.hpp"
#include "addition/rpc_server.hpp"
#include "addition/rpc_network_server.hpp"
#include "addition/state_store.hpp"
#include "addition/staking.hpp"
#include "addition/wallet_keys.hpp"
#include "addition/token_engine.hpp"
#include "addition/crypto.hpp"
#include "addition/ai_optimizer.hpp"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdlib>

#include <chrono>
#include <thread>

namespace {

std::string trim_copy(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    if (i > 0) {
        s.erase(0, i);
    }
    return s;
}

bool parse_rpc_auth(const std::string& cmd,
                    const std::string& expected_token,
                    std::string& stripped,
                    std::string& error) {
    stripped.clear();
    error.clear();

    std::istringstream iss(cmd);
    std::string provided;
    if (!(iss >> provided)) {
        error = "error: missing auth token";
        return false;
    }
    if (provided != expected_token) {
        error = "error: unauthorized";
        return false;
    }

    std::string rest;
    std::getline(iss, rest);
    stripped = trim_copy(rest);
    if (stripped.empty()) {
        error = "error: missing command";
        return false;
    }
    return true;
}

} // namespace

int main() {
    const bool mainnet_mode = []() {
        if (const char* v = std::getenv("ADDITION_MAINNET_MODE")) {
            return std::string(v) == "1";
        }
        return false;
    }();

    const bool allow_insecure_tx_commands = []() {
        if (const char* v = std::getenv("ADDITION_ALLOW_INSECURE_TX_COMMANDS")) {
            return std::string(v) == "1";
        }
        return false;
    }();

    const bool strict_admin_mode = []() {
        if (const char* v = std::getenv("ADDITION_STRICT_ADMIN_MODE")) {
            return std::string(v) != "0";
        }
        return true;
    }();

    const char* privacy_master_key_env = std::getenv("ADDITION_PRIVACY_MASTER_KEY");
    const bool has_privacy_master_key = privacy_master_key_env != nullptr && std::string(privacy_master_key_env).size() >= 32;
    if (mainnet_mode && !has_privacy_master_key) {
        std::cerr << "fatal: ADDITION_PRIVACY_MASTER_KEY missing or too short (min 32 chars) while ADDITION_MAINNET_MODE=1\n";
        return 3;
    }

    addition::Chain chain;
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
    addition::WalletKeys node_keys{};
    node_keys.algorithm = "ml-dsa-87";
    {
        const std::string id_path = "data/node_identity.dat";
        std::string legacy_priv_from_file;
        if (std::filesystem::exists(id_path)) {
            std::ifstream in(id_path, std::ios::binary);
            std::string line;
            while (std::getline(in, line)) {
                if (line.rfind("PUB|", 0) == 0) {
                    node_keys.public_key = line.substr(4);
                } else if (line.rfind("PRIV|", 0) == 0) {
                    legacy_priv_from_file = line.substr(5);
                }
            }
        }

        if (const char* env_priv = std::getenv("ADDITION_NODE_PRIVKEY")) {
            node_keys.private_key = env_priv;
        } else if (!legacy_priv_from_file.empty()) {
            node_keys.private_key = legacy_priv_from_file;
            std::cout << "warning: using legacy private key from data/node_identity.dat; migrate to ADDITION_NODE_PRIVKEY env var\n";
        }

        if (node_keys.public_key.empty()) {
            if (const char* env_pub = std::getenv("ADDITION_NODE_PUBKEY")) {
                node_keys.public_key = env_pub;
            }
        }

        if (node_keys.public_key.empty() || node_keys.private_key.empty()) {
            node_keys = addition::generate_wallet_keys();
            std::cout << "warning: generated ephemeral node private key; set ADDITION_NODE_PRIVKEY for restart persistence\n";
            std::filesystem::create_directories("data");
            std::ofstream out(id_path, std::ios::binary | std::ios::trunc);
            out << "PUB|" << node_keys.public_key << '\n';
            // Intentionally do not persist private key on disk.
        }

        if (!node_keys.public_key.empty()) {
            std::filesystem::create_directories("data");
            std::ofstream out(id_path, std::ios::binary | std::ios::trunc);
            out << "PUB|" << node_keys.public_key << '\n';
        }
    }

    addition::DecentralizedNode node("self",
                                     node_keys.public_key,
                                     node_keys.private_key,
                                     chain,
                                     mempool,
                                     peers,
                                     consensus);
    addition::StateStore store("data");

    {
        std::string report;
        if (!addition::crypto_selftest(report)) {
            std::cerr << "fatal: crypto selftest failed: " << report << '\n';
            return 2;
        }
        std::cout << report << '\n';
    }

    std::string load_error;
    if (!store.load_all(chain, mempool, staking, contracts, tokens, bridge, peers, node, pouw_storage, pouw_compute, private_messaging, privacy, load_error)) {
        std::cout << "warning: state load failed: " << load_error << '\n';
    } else {
        std::cout << "state loaded from ./data\n";
    }

    addition::RpcNetworkServer p2p_rpc("0.0.0.0", 28545, [&](const std::string& cmd) {
        std::istringstream iss(cmd);
        std::string peer;
        iss >> peer;
        std::string payload;
        std::getline(iss, payload);
        if (!payload.empty() && payload.front() == ' ') {
            payload.erase(payload.begin());
        }

        if (peer.empty() || payload.empty()) {
            return std::string("error: usage <peer> <payload>");
        }

        std::string err;
        if (!node.handle_inbound_message(peer, payload, err)) {
            return std::string("error: ") + err;
        }

        auto outbound = node.pull_outbound_messages();
        if (outbound.empty()) {
            return std::string("ok");
        }
        std::ostringstream out;
        out << "ok:" << outbound.front();
        return out.str();
    });

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
                            allow_insecure_tx_commands,
                            strict_admin_mode);
    const bool enable_lan_rpc = []() {
        if (const char* v = std::getenv("ADDITION_ENABLE_LAN_RPC")) {
            return std::string(v) == "1";
        }
        return false;
    }();

    const bool enable_p2p_rpc = []() {
        if (const char* v = std::getenv("ADDITION_ENABLE_P2P_RPC")) {
            return std::string(v) == "1";
        }
        return false;
    }();

    std::string local_rpc_token;
    if (const char* v = std::getenv("ADDITION_RPC_TOKEN")) {
        local_rpc_token = trim_copy(v);
    }
    const bool local_rpc_auth_required = !local_rpc_token.empty();

    std::string lan_rpc_token;
    if (const char* v = std::getenv("ADDITION_LAN_RPC_TOKEN")) {
        lan_rpc_token = trim_copy(v);
    }
    const bool lan_rpc_auth_required = !lan_rpc_token.empty();

    addition::RpcNetworkServer local_rpc("127.0.0.1", 8545, [&](const std::string& cmd) {
        if (!local_rpc_auth_required) {
            return rpc.handle_command(cmd, true);
        }
        std::string stripped;
        std::string error;
        if (!parse_rpc_auth(cmd, local_rpc_token, stripped, error)) {
            return error;
        }
        return rpc.handle_command(stripped, true);
    });
    addition::RpcNetworkServer lan_rpc("0.0.0.0", 18545, [&](const std::string& cmd) {
        if (!lan_rpc_auth_required) {
            return std::string("error: LAN RPC auth token not configured");
        }
        std::string stripped;
        std::string error;
        if (!parse_rpc_auth(cmd, lan_rpc_token, stripped, error)) {
            return error;
        }
        return rpc.handle_command(stripped, false);
    });

    std::string local_error;
    if (!local_rpc.start(local_error)) {
        std::cout << "warning: local RPC failed to start: " << local_error << '\n';
    } else {
        std::cout << "local RPC listening on 127.0.0.1:8545\n";
        if (local_rpc_auth_required) {
            std::cout << "local RPC auth enabled (prefix command with token)\n";
        } else {
            std::cout << "warning: local RPC auth disabled (set ADDITION_RPC_TOKEN)\n";
        }
    }

    std::string lan_error;
    if (enable_lan_rpc) {
        if (!lan_rpc_auth_required) {
            std::cout << "warning: LAN RPC requested but ADDITION_LAN_RPC_TOKEN is not set; LAN RPC will reject all requests\n";
        }
        if (!lan_rpc.start(lan_error)) {
            std::cout << "warning: LAN RPC failed to start: " << lan_error << '\n';
        } else {
            std::cout << "LAN RPC listening on 0.0.0.0:18545\n";
        }
    } else {
        std::cout << "LAN RPC disabled by default (set ADDITION_ENABLE_LAN_RPC=1 to enable)\n";
    }

    std::string p2p_error;
    if (enable_p2p_rpc) {
        if (!p2p_rpc.start(p2p_error)) {
            std::cout << "warning: P2P RPC failed to start: " << p2p_error << '\n';
        } else {
            std::cout << "P2P RPC listening on 0.0.0.0:28545\n";
        }
    } else {
        std::cout << "P2P RPC disabled by default (set ADDITION_ENABLE_P2P_RPC=1 to enable)\n";
    }

    std::cout << "ADDITION_FINAL daemon started. Commands: getinfo, fee_info, createwallet, sign_message <privkey_hex> <message_hex_utf8>, verify_message <pubkey_hex> <message_hex_utf8> <sig_hex>, getbalance <addr>, getbalance_instant <addr>, tx_build <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce>, sendtx_signed <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex>, sendtx_signed_hash <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex>, mine,\n"
                 "monetary_info, crypto_selftest,\n"
                 "stake <addr> <amt>, unstake <addr> <amt>, staked <addr>, stake_reward <amt>, stake_claim <addr>,\n"
                 "stake_claimable <addr>, stake_policy <get|set> [cap_bps admin_addr admin_pubkey admin_sig],\n"
                 "contract_deploy <owner> <code>, contract_call <id> <set|add|get|token_balance|swap_quote|zk_mint|zk_spend|zk_privacy_status> <key> <value>,\n"
                 "addpeer <ip:port>, delpeer <ip:port>, peers, vote <peer> <height> <hash>, quorum <height> <hash>,\n"
                 "privacy_native_verifier <pq_mldsa87>,\n"
                 "privacy_mint_zk <owner> <amt> <commitment> <nullifier> <proof> <vk>,\n"
                 "privacy_spend_zk <owner> <note_id> <recipient> <amt> <nullifier> <proof> <vk>, privacy_status,\n"
                 "pouw_storage_create_deal <client_addr> <content_root> <chunk_count> <replication_factor> <start_height> <end_height> <price_per_epoch>,\n"
                 "pouw_storage_commit <deal_id> <worker_addr> <sealed_commitment> <collateral>,\n"
                 "pouw_storage_challenge <deal_id> <worker_addr> <height>,\n"
                 "pouw_storage_submit_proof <challenge_id> <worker_addr> <proof_blob_hash>,\n"
                 "pouw_storage_deal_status <deal_id>, pouw_storage_worker_status <worker_addr>,\n"
                 "pouw_compute_submit_job <requester_addr> <job_type> <input_ref> <determinism_profile> <max_latency_sec> <reward_budget> <min_reputation>,\n"
                 "pouw_compute_assign_job <job_id> <worker_addr> <collateral_locked>,\n"
                 "pouw_compute_submit_result <job_id> <worker_addr> <output_ref> <result_hash> <proof_ref>,\n"
                 "pouw_compute_validate <job_id> <validator_addr> <pass|fail> <score>,\n"
                 "pouw_compute_job_status <job_id>, pouw_compute_worker_status <worker_addr>,\n"
                 "pm_send_ttl <sender> <recipient> <ciphertext_ref> <ttl_sec> [policy] (auto-destroy hard delete at TTL, anchored on-chain), pm_inbox <recipient>,\n"
                 "pm_status <msg_id>, pm_fetch <msg_id> <requester>, pm_destroy <msg_id> <requester>, pm_purge, ai_status,\n"
                 "bridge_register <chain>, bridge_set_attestor <chain> <pubkey> <admin_addr> <admin_pubkey> <admin_sig>, bridge_attestor <chain>,\n"
                 "bridge_lock <chain> <user> <amt>, bridge_mint <chain> <user> <amt>, bridge_mint_attested <chain> <user> <amt> <sig>,\n"
                 "bridge_burn <chain> <user> <amt>, bridge_release <chain> <user> <amt>, bridge_release_attested <chain> <user> <amt> <sig>,\n"
                 "bridge_balance <chain> <user>,\n"
                 "token_create <symbol> <owner> <max_supply> <initial_mint>, token_mint <symbol> <caller> <to> <amount>,\n"
                 "token_create_ex <symbol> <name> <owner> <max_supply> <initial_mint> <decimals> <burnable_0_1> <dev_wallet_or_dash> <dev_allocation>,\n"
                 "token_transfer <symbol> <from> <to> <amount>, token_balance <symbol> <owner>, token_info <symbol>, token_burn <symbol> <from> <amount>,\n"
                 "token_set_policy <symbol> <caller_owner> <treasury_wallet_or_dash> <transfer_fee_bps> <burn_fee_bps> <paused_0_1>,\n"
                 "token_blacklist <symbol> <caller_owner> <wallet> <blocked_0_1>,\n"
                 "token_fee_exempt <symbol> <caller_owner> <wallet> <exempt_0_1>,\n"
                 "token_set_limits <symbol> <caller_owner> <max_tx_amount_or_0> <max_wallet_amount_or_0>,\n"
                 "swap_pool_create <token_a> <token_b> <fee_bps>,\n"
                 "swap_add_liquidity <token_a> <token_b> <provider> <amount_a> <amount_b>,\n"
                 "swap_remove_liquidity <token_a> <token_b> <provider> <lp_amount>,\n"
                 "swap_pool_info <token_a> <token_b>,\n"
                 "swap_quote <token_in> <token_out> <amount_in>,\n"
                 "swap_exact_in <token_in> <token_out> <trader> <amount_in> <min_out>,\n"
                 "swap_quote_route <A>B>C <amount_in>, swap_route_exact_in <A>B>C <trader> <amount_in> <min_out>,\n"
                 "swap_best_route <token_in> <token_out> <amount_in> [max_hops],\n"
                 "swap_best_route_exact_in <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> [max_hops],\n"
                 "swap_best_route_sign_payload <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> [max_hops],\n"
                 "swap_best_route_exact_in_signed <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> <max_hops> <trader_pubkey> <trader_sig>,\n"
                 "nft_mint <collection> <token_id> <owner> <metadata>, nft_transfer <collection> <token_id> <from> <to>, nft_owner <collection> <token_id>,\n"
                 "sendtx_hash <from_addr> <pubkey_hex> <privkey_hex> <to_addr> <amount> <fee> <nonce> (insecure legacy), tx_status <tx_hash>,\n"
                 "peer_inbound <peer> <payload>, gossip_flush, sync, node_pubkey,\n"
                 "identity_rotate_propose <new_pub> <new_priv> <grace_sec>, identity_rotate_vote <peer_id>,\n"
                 "identity_rotate_vote_broadcast, identity_rotate_commit, identity_rotate_status, quit\n";

    if (!allow_insecure_tx_commands) {
        std::cout << "secure tx mode enabled: use tx_build + sign_message + sendtx_signed (insecure sendtx/sendtx_hash disabled)\n";
    } else {
        std::cout << "warning: insecure tx commands enabled (ADDITION_ALLOW_INSECURE_TX_COMMANDS=1)\n";
    }
    if (strict_admin_mode) {
        std::cout << "strict admin mode enabled\n";
    } else {
        std::cout << "warning: strict admin mode disabled (ADDITION_STRICT_ADMIN_MODE=0)\n";
    }
    if (mainnet_mode) {
        std::cout << "mainnet mode enabled (ADDITION_MAINNET_MODE=1)\n";
    }
    if (!has_privacy_master_key) {
        std::cout << "warning: ADDITION_PRIVACY_MASTER_KEY not set or too short (min 32); private note operations will fail\n";
    }

    auto last_sync = std::chrono::steady_clock::now();

    for (std::string line; std::getline(std::cin, line);) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_sync >= std::chrono::seconds(5)) {
            std::string sync_err;
            node.sync_once(sync_err);
            last_sync = now;
        }

        if (line == "quit" || line == "exit") {
            break;
        }

        if (line.rfind("mine", 0) == 0) {
            std::string reward_address = "miner1";
            {
                std::istringstream ls(line);
                std::string cmd;
                ls >> cmd;
                std::string maybe_address;
                ls >> maybe_address;
                if (!maybe_address.empty()) {
                    reward_address = maybe_address;
                }
            }

            std::string mined_hash;
            std::string error;
            const auto hw = std::thread::hardware_concurrency();
            const std::size_t threads = hw > 0 ? static_cast<std::size_t>(hw) : 1;
            if (!miner.mine_next_block(reward_address, 500, threads, mined_hash, error)) {
                std::cout << "error: " << error << '\n';
            } else {
                std::cout << "mined block " << chain.height() << " reward=" << reward_address
                          << " threads=" << threads << " hash=" << mined_hash << '\n';
            }
            continue;
        }

        std::cout << rpc.handle_command(line, true) << '\n';
    }

    local_rpc.stop();
    lan_rpc.stop();
    p2p_rpc.stop();

    std::string save_error;
    if (!store.save_all(chain, mempool, staking, contracts, tokens, bridge, peers, node, pouw_storage, pouw_compute, private_messaging, privacy, save_error)) {
        std::cout << "warning: state save failed: " << save_error << '\n';
    } else {
        std::cout << "state saved to ./data\n";
    }

    return 0;
}
