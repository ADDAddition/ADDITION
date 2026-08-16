#include "addition/auto_mine.hpp"
#include "addition/chain.hpp"
#include "addition/config.hpp"
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
#include "addition/rpc_access.hpp"
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
#include <cstdio>
#include <cstdint>
#include <exception>
#include <atomic>
#include <csignal>

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

std::atomic<bool> g_stay_alive_stop{false};

void handle_stop_signal(int) {
    g_stay_alive_stop = true;
}

bool stdin_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return ::isatty(::fileno(stdin)) != 0;
#endif
}

bool parse_env_u16(const char* name, std::uint16_t& out) {
    const char* v = std::getenv(name);
    if (v == nullptr) {
        return false;
    }
    try {
        const auto port = std::stoul(v);
        if (port > 0 && port <= 65535) {
            out = static_cast<std::uint16_t>(port);
            return true;
        }
    } catch (const std::exception&) {
    }
    return false;
}

bool is_self_p2p_endpoint(const std::string& endpoint, std::uint16_t p2p_port) {
    if (addition::is_self_peer_label(endpoint)) {
        return true;
    }
    const char* advertised = std::getenv("ADDITION_ADVERTISED_P2P");
    if (advertised != nullptr && endpoint == advertised) {
        return true;
    }
    const std::string suffix = ":" + std::to_string(p2p_port);
    if (endpoint.size() <= suffix.size()) {
        return false;
    }
    if (endpoint.compare(endpoint.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    const std::string host = endpoint.substr(0, endpoint.size() - suffix.size());
    return addition::is_loopback_host(host);
}

} // namespace

int main(int argc, char** argv) {
    addition::NodeConfig node_cfg = addition::default_node_config();
    bool show_help = false;
    std::string cli_error;
    if (!addition::apply_cli_args(argc, argv, node_cfg, show_help, cli_error)) {
        std::cerr << "fatal: " << cli_error << '\n';
        return 1;
    }
    if (show_help) {
        std::cout << addition::daemon_help_text();
        return 0;
    }

    addition::set_runtime_network_mode(node_cfg.mode);
    const bool mainnet_mode = node_cfg.mode == addition::NetworkMode::Mainnet;

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
        std::cerr << "fatal: ADDITION_PRIVACY_MASTER_KEY missing or too short (min 32 chars) for --network mainnet\n";
        return 3;
    }

    addition::Chain chain(node_cfg.chain);
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
        const std::string id_path = (std::filesystem::path(node_cfg.data_dir) / "node_identity.dat").string();
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
            std::filesystem::create_directories(node_cfg.data_dir);
            std::ofstream out(id_path, std::ios::binary | std::ios::trunc);
            out << "PUB|" << node_keys.public_key << '\n';
            // Intentionally do not persist private key on disk.
        }

        if (!node_keys.public_key.empty()) {
            std::filesystem::create_directories(node_cfg.data_dir);
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
    addition::StateStore store(node_cfg.data_dir);

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
        if (load_error.rfind("chain load failed:", 0) == 0) {
            std::cerr << "fatal: " << load_error << '\n';
            return 4;
        }
        std::cout << "warning: state load failed: " << load_error << '\n';
    } else {
        std::cout << "state loaded from " << node_cfg.data_dir
                  << " height=" << chain.height() << '\n';
    }
    if (!store.ensure_network_marker(chain, load_error)) {
        std::cerr << "fatal: " << load_error << '\n';
        return 4;
    }

    chain.set_on_commit([&]() {
        std::string persist_error;
        if (!store.save_chain(chain, persist_error)) {
            std::cout << "warning: chain persist failed: " << persist_error << '\n';
        } else {
            std::cout << "chain persisted height=" << chain.height()
                      << " path=" << (std::filesystem::path(node_cfg.data_dir) / "blocks.dat").string()
                      << '\n';
        }
    });

    addition::RpcNetworkServer p2p_rpc("0.0.0.0", node_cfg.p2p_port, [&](const std::string& cmd) {
        return node.handle_p2p_line(cmd);
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
                            strict_admin_mode,
                            (std::filesystem::path(node_cfg.data_dir) / "wallets").string());
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

    if (const char* v = std::getenv("ADDITION_ENABLE_PUBLIC_RPC")) {
        if (std::string(v) == "1") {
            node_cfg.enable_public_rpc = true;
        }
    }
    parse_env_u16("ADDITION_PUBLIC_RPC_PORT", node_cfg.public_rpc_port);
    if (const char* v = std::getenv("ADDITION_PUBLIC_RPC_BIND")) {
        const std::string bind = trim_copy(v);
        if (!bind.empty()) {
            node_cfg.public_rpc_bind = bind;
        }
    }
    parse_env_u16("ADDITION_LOCAL_RPC_PORT", node_cfg.local_rpc_port);
    parse_env_u16("ADDITION_P2P_PORT", node_cfg.p2p_port);
    addition::apply_advertised_p2p_env(node_cfg);
    rpc.set_advertised_p2p(node_cfg.advertised_p2p);
    if (!addition::apply_auto_mine_env(node_cfg)) {
        std::cerr << "fatal: invalid ADDITION_AUTO_MINE_INTERVAL (use 1-86400)\n";
        return 1;
    }

    for (const auto& peer : node_cfg.bootstrap_peers) {
        if (is_self_p2p_endpoint(peer, node_cfg.p2p_port)) {
            continue;
        }
        if (!node_cfg.advertised_p2p.empty() && peer == node_cfg.advertised_p2p) {
            continue;
        }
        if (peers.add_peer(peer)) {
            std::cout << "bootstrap addpeer " << peer << '\n';
        }
    }

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

    addition::RpcNetworkServer local_rpc("127.0.0.1", node_cfg.local_rpc_port, [&](const std::string& cmd) {
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
    addition::RpcNetworkServer lan_rpc("0.0.0.0", node_cfg.lan_rpc_port, [&](const std::string& cmd) {
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
    addition::RpcNetworkServer public_rpc(node_cfg.public_rpc_bind, node_cfg.public_rpc_port, [&](const std::string& cmd) {
        return addition::dispatch_public_read_rpc(rpc, cmd);
    });

    std::string local_error;
    if (!local_rpc.start(local_error)) {
        std::cout << "warning: local RPC failed to start: " << local_error << '\n';
    } else {
        std::cout << "local RPC listening on 127.0.0.1:" << node_cfg.local_rpc_port << '\n';
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
            std::cout << "LAN RPC listening on 0.0.0.0:" << node_cfg.lan_rpc_port << '\n';
        }
    } else {
        std::cout << "LAN RPC disabled by default (set ADDITION_ENABLE_LAN_RPC=1 to enable)\n";
    }

    std::string p2p_error;
    if (enable_p2p_rpc) {
        if (!p2p_rpc.start(p2p_error)) {
            std::cout << "warning: P2P RPC failed to start: " << p2p_error << '\n';
        } else {
            std::cout << "P2P RPC listening on 0.0.0.0:" << node_cfg.p2p_port << '\n';
        }
    } else {
        std::cout << "P2P RPC disabled by default (set ADDITION_ENABLE_P2P_RPC=1 to enable)\n";
    }

    std::string public_error;
    if (node_cfg.enable_public_rpc) {
        if (!public_rpc.start(public_error)) {
            std::cout << "warning: public read RPC failed to start: " << public_error << '\n';
        } else {
            std::cout << "public read RPC listening on " << node_cfg.public_rpc_bind << ':'
                      << node_cfg.public_rpc_port
                      << " allowlist=getinfo,monetary_info,crypto_selftest,tx_status,peers,getblock,getblockhash,getblockraw\n";
        }
    } else {
        std::cout << "public read RPC disabled (set --public-rpc or ADDITION_ENABLE_PUBLIC_RPC=1)\n";
    }

    std::cout << "ADDITION research daemon started. This is not a live mainnet.\n";
    std::cout << "network=" << addition::network_mode_label(node_cfg.mode)
              << " network_name=" << node_cfg.chain.network_name
              << " network_id=" << node_cfg.chain.network_id << '\n';
    if (!node_cfg.config_path.empty()) {
        std::cout << "config=" << node_cfg.config_path << '\n';
    }
    if (!node_cfg.genesis_path.empty()) {
        std::cout << "genesis=" << node_cfg.genesis_path << '\n';
    }
    if (mainnet_mode) {
        std::cout << "bootstrap_peers (IPv4 only; not the public testnet seed):";
    } else {
        std::cout << "bootstrap_peers (IPv4 only; operator public P2P is "
                  << addition::kOperatorPublicP2p << "):";
    }
    for (const auto& peer : node_cfg.bootstrap_peers) {
        std::cout << ' ' << peer;
    }
    std::cout << '\n';
    if (!node_cfg.advertised_p2p.empty()) {
        std::cout << "advertised_p2p=" << node_cfg.advertised_p2p
                  << " (public getinfo/peers; not self). Public TCP 28545 can timeout/filter; "
                  << "HTTP :80 sync is the reliable join path.\n";
    }

    const bool auto_mine_requested = node_cfg.enable_auto_mine;
    if (auto_mine_requested && mainnet_mode) {
        node_cfg.enable_auto_mine = false;
        std::cout << "warning: --auto-mine ignored on mainnet profile (testnet only)\n";
    }
    rpc.set_auto_mine_status(node_cfg.enable_auto_mine && !mainnet_mode, node_cfg.auto_mine_interval_sec);
    if (node_cfg.enable_auto_mine) {
        std::cout << "auto-mine enabled (testnet in-process) interval_sec="
                  << node_cfg.auto_mine_interval_sec
                  << " reward=" << (node_cfg.auto_mine_reward.empty() ? "miner1" : node_cfg.auto_mine_reward)
                  << " (not exposed on public RPC)\n";
    } else {
        std::cout << "auto-mine disabled (default; --auto-mine or ADDITION_AUTO_MINE=1 on testnet)\n";
    }
    std::cout << "Commands: getinfo, fee_info, createwallet [name], wallet_list, wallet_info <name>, wallet_balance <name>, wallet_send <name> <to> <amount> [fee], wallet_sign <name> <message_hex_utf8>, sign_message <privkey_hex> <message_hex_utf8>, verify_message <pubkey_hex> <message_hex_utf8> <sig_hex>, getbalance <addr>, getbalance_instant <addr>, tx_build <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce>, sendtx_signed <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex>, sendtx_signed_hash <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex>, mine,\n"
                 "monetary_info, crypto_selftest,\n"
                 "stake <addr> <amt>, unstake <addr> <amt>, staked <addr>, stake_reward <amt>, stake_claim <addr>,\n"
                 "stake_claimable <addr>, stake_policy <get|set> [cap_bps admin_addr admin_pubkey admin_sig],\n"
                 "contract_deploy <owner> <code>, contract_call <id> <set|add|get|token_balance|swap_quote|zk_mint|zk_spend|zk_privacy_status> <key> <value>,\n"
                 "addpeer <ip:port>, delpeer <ip:port>, peers, vote <peer> <height> <hash>, quorum <height> <hash>,\n"
                 "privacy_note_prepare <amt>, privacy_mint_open <owner> <amt> <cm> <nf> <trapdoor>,\n"
                 "privacy_spend_open <owner> <note_id> <recipient> <amt> <trapdoor>,\n"
                 "privacy_native_verifier <pq_mldsa87>,\n"
                 "privacy_mint_zk <owner> <amt> <commitment> <nullifier> <proof> <vk> (ML-DSA wrap, not a circuit),\n"
                 "privacy_spend_zk <owner> <note_id> <recipient> <amt> <nullifier> <proof> <vk> (ML-DSA wrap, not a circuit), privacy_status,\n"
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
                 "add_liquidity|swap_add_liquidity <token_a> <token_b> <provider> <amount_a> <amount_b>,\n"
                 "swap_tvl (sum of live pool reserves; 0 if no pools),\n"
                 "swap_remove_liquidity <token_a> <token_b> <provider> <lp_amount>,\n"
                 "swap_pool_info <token_a> <token_b>,\n"
                 "swap_quote <token_in> <token_out> <amount_in>,\n"
                 "swap_exact_in <token_in> <token_out> <trader> <amount_in> <min_out>,\n"
                 "swap_quote_route <A>B>C <amount_in>, swap_route_exact_in <A>B>C <trader> <amount_in> <min_out>,\n"
                 "swap_best_route <token_in> <token_out> <amount_in> [max_hops],\n"
                 "swap_best_route_exact_in <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> [max_hops],\n"
                 "swap_best_route_sign_payload <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> [max_hops],\n"
                 "swap_best_route_exact_in_signed <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> <max_hops> <trader_pubkey> <trader_sig>,\n"
                 "nft_mint <collection> <token_id> <owner> <metadata>, nft_transfer <collection> <token_id> <from> <to>, nft_owner <collection> <token_id>, nft_info <collection> <token_id>,\n"
                 "sendtx_hash <from_addr> <pubkey_hex> <privkey_hex> <to_addr> <amount> <fee> <nonce> (insecure legacy), tx_status <tx_hash>,\n"
                 "getblock <height_or_hash>, getblockhash <height>, getblockraw <height>,\n"
                 "peer_inbound <peer> <payload>, gossip_flush, sync, node_pubkey,\n"
                 "identity_rotate_propose <new_pub> <new_priv> <grace_sec>, identity_rotate_vote <peer_id>,\n"
                 "identity_rotate_vote_broadcast, identity_rotate_commit, identity_rotate_status, quit\n";

    if (!allow_insecure_tx_commands) {
        std::cout << "secure tx mode enabled: use wallet_send, or tx_build + wallet_sign/sign_message + sendtx_signed (insecure sendtx/sendtx_hash disabled)\n";
    } else {
        std::cout << "warning: insecure tx commands enabled (ADDITION_ALLOW_INSECURE_TX_COMMANDS=1)\n";
    }
    if (strict_admin_mode) {
        std::cout << "strict admin mode enabled\n";
    } else {
        std::cout << "warning: strict admin mode disabled (ADDITION_STRICT_ADMIN_MODE=0)\n";
    }
    if (mainnet_mode) {
        std::cout << "mainnet chain " << node_cfg.chain.network_id
                  << " data-dir=" << node_cfg.data_dir
                  << "; this is not a live public network\n";
    } else {
        std::cout << "testnet mode enabled (default)\n";
    }
    if (!has_privacy_master_key) {
        std::cout << "warning: ADDITION_PRIVACY_MASTER_KEY not set or too short (min 32); private note operations will fail\n";
    }

    auto last_sync = std::chrono::steady_clock::now();
    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);

    std::thread auto_mine_thread;
    if (node_cfg.enable_auto_mine) {
        const std::string reward =
            node_cfg.auto_mine_reward.empty() ? std::string("miner1") : node_cfg.auto_mine_reward;
        const auto interval = std::chrono::seconds(node_cfg.auto_mine_interval_sec == 0
                                                       ? 60
                                                       : node_cfg.auto_mine_interval_sec);
        auto_mine_thread = std::thread([&rpc, reward, interval]() {
            auto last = std::chrono::steady_clock::now();
            while (!g_stay_alive_stop) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (g_stay_alive_stop) {
                    break;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now - last < interval) {
                    continue;
                }
                last = now;
                const std::string reply = rpc.handle_command("mine " + reward, true);
                std::cout << "auto-mine: " << reply << '\n';
            }
        });
    }

    bool requested_quit = false;
    for (std::string line; std::getline(std::cin, line);) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_sync >= std::chrono::seconds(5)) {
            std::string sync_err;
            node.sync_once(sync_err);
            last_sync = now;
        }

        if (line == "quit" || line == "exit") {
            requested_quit = true;
            g_stay_alive_stop = true;
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

    if (!requested_quit && !stdin_is_tty()) {
        std::cout << "stdin closed; running until SIGINT/SIGTERM (write RPC stays 127.0.0.1)\n";
        while (!g_stay_alive_stop) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const auto now = std::chrono::steady_clock::now();
            if (now - last_sync >= std::chrono::seconds(5)) {
                std::string sync_err;
                node.sync_once(sync_err);
                last_sync = now;
            }
        }
    }

    g_stay_alive_stop = true;
    if (auto_mine_thread.joinable()) {
        auto_mine_thread.join();
    }

    local_rpc.stop();
    lan_rpc.stop();
    public_rpc.stop();
    p2p_rpc.stop();

    std::string save_error;
    if (!store.save_all(chain, mempool, staking, contracts, tokens, bridge, peers, node, pouw_storage, pouw_compute, private_messaging, privacy, save_error)) {
        std::cout << "warning: state save failed: " << save_error << '\n';
    } else {
        std::cout << "state saved to " << node_cfg.data_dir << '\n';
    }

    return 0;
}
