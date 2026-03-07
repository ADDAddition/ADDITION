#include "addition/rpc_server.hpp"

#include "addition/block.hpp"
#include "addition/crypto.hpp"
#include "addition/wallet_keys.hpp"

#include <exception>
#include <algorithm>
#include <sstream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <limits>
#include <thread>

namespace addition {

namespace {

constexpr double kObjectiveTps = 100000.0;

std::uint64_t recommended_min_fee(std::size_t mempool_size, std::uint64_t last_block_fees) {
    std::uint64_t base = 1;
    if (mempool_size > 1000) {
        base += 20;
    } else if (mempool_size > 500) {
        base += 10;
    } else if (mempool_size > 200) {
        base += 5;
    } else if (mempool_size > 100) {
        base += 3;
    } else if (mempool_size > 20) {
        base += 1;
    }

    const std::uint64_t pressure = std::min<std::uint64_t>(last_block_fees / 50, 25);
    return base + pressure;
}

std::vector<std::string> split_route(const std::string& route) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : route) {
        if (c == '>') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
            continue;
        }
        if (c != ' ') {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

bool is_remote_allowed_command(const std::string& cmd) {
    return cmd == "getinfo" ||
           cmd == "protocol_status" ||
           cmd == "fee_info" ||
           cmd == "monetary_info" ||
           cmd == "crypto_selftest" ||
           cmd == "node_pubkey" ||
           cmd == "peers" ||
           cmd == "tx_status" ||
           cmd == "getbalance" ||
           cmd == "getbalance_instant" ||
           cmd == "staked" ||
           cmd == "stake_claimable" ||
           cmd == "token_balance" ||
           cmd == "token_info" ||
           cmd == "swap_quote" ||
           cmd == "swap_pool_info" ||
           cmd == "swap_quote_route" ||
           cmd == "swap_best_route" ||
           cmd == "nft_owner" ||
           cmd == "privacy_status" ||
           cmd == "bridge_balance" ||
           cmd == "bridge_attestor" ||
           cmd == "pouw_storage_deal_status" ||
           cmd == "pouw_storage_worker_status" ||
           cmd == "pouw_compute_job_status" ||
           cmd == "pouw_compute_worker_status" ||
           cmd == "pm_inbox" ||
           cmd == "pm_status" ||
           cmd == "pm_fetch" ||
           cmd == "verify_message" ||
           cmd == "ai_status";
}

std::string derive_address_from_pubkey(const std::string& pubkey_hex) {
    return to_hex(sha3_512_bytes("addr|" + pubkey_hex)).substr(0, 40);
}

bool verify_admin_signature(const std::string& admin_addr,
                            const std::string& admin_pubkey,
                            const std::string& admin_sig_hex,
                            const std::string& payload) {
    if (admin_addr.empty() || admin_pubkey.empty() || admin_sig_hex.empty() || payload.empty()) {
        return false;
    }
    if (derive_address_from_pubkey(admin_pubkey) != admin_addr) {
        return false;
    }
    return verify_message_signature_hybrid(admin_pubkey, payload, std::string("pq=") + admin_sig_hex);
}

bool requires_admin_signature_command(const std::string& cmd) {
    return cmd == "stake_policy" ||
           cmd == "bridge_set_attestor";
}

} // namespace

RpcServer::RpcServer(Chain& chain,
                                         Mempool& mempool,
                                         Miner& miner,
                                         StakingEngine& staking,
                                         ContractEngine& contracts,
                                         BridgeEngine& bridge,
                                         TokenEngine& tokens,
                                         PeerNetwork& peers,
                                         ConsensusEngine& consensus,
                                         PrivacyPool& privacy,
                                         PoUWStorageEngine& pouw_storage,
                                         PoUWComputeEngine& pouw_compute,
                                         PrivateMessagingEngine& private_messaging,
                                         AIRoutingOptimizer& ai_optimizer,
                                         DecentralizedNode& node,
                                         bool allow_insecure_tx_commands,
                                         bool strict_admin_mode)
        : chain_(chain),
            mempool_(mempool),
            miner_(miner),
            staking_(staking),
            contracts_(contracts),
            bridge_(bridge),
            tokens_(tokens),
            peers_(peers),
            consensus_(consensus),
            privacy_(privacy),
            pouw_storage_(pouw_storage),
            pouw_compute_(pouw_compute),
            private_messaging_(private_messaging),
            ai_optimizer_(ai_optimizer),
            node_(node),
            allow_insecure_tx_commands_(allow_insecure_tx_commands),
            strict_admin_mode_(strict_admin_mode) {}

std::string RpcServer::handle_command(const std::string& line, bool trusted) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd.empty()) {
        return "error: empty command";
    }

    if (!trusted && !is_remote_allowed_command(cmd)) {
        return "error: command disabled on remote RPC";
    }

    if (strict_admin_mode_ && requires_admin_signature_command(cmd) && !trusted) {
        return "error: admin command allowed only on trusted interface";
    }

    ai_optimizer_.observe(mempool_.size(), chain_.total_fees_last_block(), chain_.height());

    if (cmd == "getinfo") {
        const auto dyn_fee = recommended_min_fee(mempool_.size(), chain_.total_fees_last_block());
        std::ostringstream out;
        out << "height=" << chain_.height() << " mempool=" << mempool_.size()
            << " total_staked=" << staking_.total_staked()
            << " peers=" << peers_.peer_count()
            << " difficulty_target=" << chain_.current_difficulty_target()
            << " next_reward=" << chain_.current_block_reward()
            << " fees_last_block=" << chain_.total_fees_last_block()
            << " dynamic_min_fee=" << dyn_fee
            << " max_supply=" << chain_.max_supply()
            << " last_mine_ms=" << miner_.last_mine_ms()
            << " last_mined_txs=" << miner_.last_mined_txs()
            << " last_tps=" << std::fixed << std::setprecision(2) << miner_.last_tps()
            << " pq_mode=strict"
            << " privacy_mode=enabled";
        return out.str();
    }

    if (cmd == "protocol_status") {
        std::ostringstream out;
        const auto last_tps = miner_.last_tps();
        const bool objective_tps_ok = last_tps >= kObjectiveTps;
        const bool objective_privacy_ok = privacy_.strict_zk_mode() && privacy_.verifier_configured();
        const bool objective_100_ok = objective_tps_ok && objective_privacy_ok;

        out << "objective_tps_target=" << std::fixed << std::setprecision(0) << kObjectiveTps
            << " objective_tps_last=" << std::fixed << std::setprecision(2) << last_tps
            << " objective_tps_ok=" << (objective_tps_ok ? "true" : "false")
            << " objective_privacy_ok=" << (objective_privacy_ok ? "true" : "false")
            << " objective_100_ok=" << (objective_100_ok ? "true" : "false")
            << " strict_zk_mode=" << (privacy_.strict_zk_mode() ? "true" : "false")
            << " verifier_configured=" << (privacy_.verifier_configured() ? "true" : "false")
            << " last_mine_ms=" << miner_.last_mine_ms()
            << " last_mined_txs=" << miner_.last_mined_txs();
        return out.str();
    }

    if (cmd == "fee_info") {
        const auto msz = mempool_.size();
        const auto last = chain_.total_fees_last_block();
        const auto dyn = recommended_min_fee(msz, last);
        const auto ai_floor = ai_optimizer_.recommended_fee_floor();
        std::ostringstream out;
        out << "base_min_fee=1"
            << " mempool_size=" << msz
            << " fees_last_block=" << last
            << " recommended_min_fee=" << std::max(dyn, ai_floor)
            << " ai_fee_floor=" << ai_floor
            << " ai_difficulty_bias_bps=" << ai_optimizer_.suggested_difficulty_bias_bps();
        return out.str();
    }

    if (cmd == "monetary_info") {
        std::ostringstream out;
        out << "max_supply=" << chain_.max_supply()
            << " emitted=" << chain_.total_emitted()
            << " remaining=" << chain_.remaining_supply()
            << " next_reward=" << chain_.current_block_reward()
            << " next_halving_height=" << chain_.next_halving_height();
        return out.str();
    }

    if (cmd == "crypto_selftest") {
        std::string report;
        const auto ok = crypto_selftest(report);
        return ok ? std::string("ok:") + report : std::string("error:") + report;
    }

    if (cmd == "sign_message") {
        std::string privkey;
        std::string message_hex;
        iss >> privkey >> message_hex;
        if (privkey.empty() || message_hex.empty()) {
            return "error: usage sign_message <privkey_hex> <message_hex_utf8>";
        }
        std::vector<std::uint8_t> msg_bytes;
        std::string error;
        if (!hex_to_bytes(message_hex, msg_bytes, error)) {
            return "error: invalid message_hex: " + error;
        }
        if (msg_bytes.empty() || msg_bytes.size() > 8192) {
            return "error: message size invalid";
        }

        const std::string msg(reinterpret_cast<const char*>(msg_bytes.data()), msg_bytes.size());
        try {
            return sign_message_hybrid(privkey, msg);
        } catch (const std::exception& e) {
            return std::string("error: signing failed: ") + e.what();
        }
    }

    if (cmd == "verify_message") {
        std::string pubkey;
        std::string message_hex;
        std::string sig_hex;
        iss >> pubkey >> message_hex >> sig_hex;
        if (pubkey.empty() || message_hex.empty() || sig_hex.empty()) {
            return "error: usage verify_message <pubkey_hex> <message_hex_utf8> <sig_hex_without_pq_prefix>";
        }

        std::vector<std::uint8_t> msg_bytes;
        std::string error;
        if (!hex_to_bytes(message_hex, msg_bytes, error)) {
            return "error: invalid message_hex: " + error;
        }
        if (msg_bytes.empty() || msg_bytes.size() > 8192) {
            return "error: message size invalid";
        }

        const std::string msg(reinterpret_cast<const char*>(msg_bytes.data()), msg_bytes.size());
        const bool ok = verify_message_signature_hybrid(pubkey, msg, std::string("pq=") + sig_hex);
        return ok ? "true" : "false";
    }

    if (cmd == "addpeer") {
        std::string endpoint;
        iss >> endpoint;
        if (endpoint.empty()) {
            return "error: usage addpeer <ip:port>";
        }
        return peers_.add_peer(endpoint) ? "ok" : "error: invalid/duplicate peer";
    }

    if (cmd == "gossip_flush") {
        auto msgs = node_.pull_outbound_messages();
        std::ostringstream out;
        out << "messages=" << msgs.size();
        return out.str();
    }

    if (cmd == "sync") {
        std::string err;
        if (!node_.sync_once(err)) {
            return "error: " + err;
        }
        std::ostringstream out;
        out << "ok:height=" << chain_.height();
        return out.str();
    }

    if (cmd == "node_pubkey") {
        return node_.node_public_key();
    }

    if (cmd == "identity_rotate_propose") {
        std::string new_pub;
        std::string new_priv;
        std::uint64_t grace = 0;
        iss >> new_pub >> new_priv >> grace;
        if (new_pub.empty() || new_priv.empty() || grace == 0) {
            return "error: usage identity_rotate_propose <new_pubkey_hex> <new_privkey_hex> <grace_seconds>";
        }
        std::string err;
        if (!node_.propose_identity_rotation(new_pub, new_priv, grace, err)) {
            return "error: " + err;
        }
        return "ok";
    }

    if (cmd == "identity_rotate_vote") {
        std::string peer_id;
        iss >> peer_id;
        if (peer_id.empty()) {
            return "error: usage identity_rotate_vote <peer_id>";
        }
        std::string err;
        if (!node_.vote_identity_rotation(peer_id, err)) {
            return "error: " + err;
        }
        return "ok";
    }

    if (cmd == "identity_rotate_vote_broadcast") {
        std::string err;
        if (!node_.broadcast_identity_rotation_vote(err)) {
            return "error: " + err;
        }
        return "ok";
    }

    if (cmd == "identity_rotate_commit") {
        std::string err;
        if (!node_.commit_identity_rotation(err)) {
            return "error: " + err;
        }
        return "ok";
    }

    if (cmd == "identity_rotate_status") {
        return node_.identity_rotation_status();
    }

    if (cmd == "peer_inbound") {
        std::string peer;
        iss >> peer;
        std::string payload;
        std::getline(iss, payload);
        if (!payload.empty() && payload.front() == ' ') {
            payload.erase(payload.begin());
        }
        if (peer.empty() || payload.empty()) {
            return "error: usage peer_inbound <peer> <payload>";
        }
        std::string err;
        if (!node_.handle_inbound_message(peer, payload, err)) {
            return "error: " + err;
        }
        return "ok";
    }

    if (cmd == "delpeer") {
        std::string endpoint;
        iss >> endpoint;
        if (endpoint.empty()) {
            return "error: usage delpeer <ip:port>";
        }
        return peers_.remove_peer(endpoint) ? "ok" : "error: peer not found";
    }

    if (cmd == "peers") {
        auto all = peers_.peers();
        std::ostringstream out;
        for (std::size_t i = 0; i < all.size(); ++i) {
            out << all[i];
            if (i + 1 < all.size()) {
                out << ',';
            }
        }
        return out.str();
    }

    if (cmd == "vote") {
        std::string peer;
        std::uint64_t height = 0;
        std::string block_hash;
        iss >> peer >> height >> block_hash;
        if (peer.empty() || block_hash.empty() || height == 0) {
            return "error: usage vote <peer> <height> <block_hash>";
        }
        consensus_.submit_vote(peer, height, block_hash);
        return "ok";
    }

    if (cmd == "quorum") {
        std::uint64_t height = 0;
        std::string block_hash;
        iss >> height >> block_hash;
        if (height == 0 || block_hash.empty()) {
            return "error: usage quorum <height> <block_hash>";
        }
        return consensus_.has_quorum(height, block_hash, peers_.peer_count()) ? "true" : "false";
    }

    if (cmd == "createwallet") {
        WalletKeys keys{};
        try {
            keys = generate_wallet_keys();
        } catch (const std::exception& e) {
            return std::string("error: wallet generation failed: ") + e.what();
        }
        std::ostringstream out;
        out << "address=" << keys.address << " pub=" << keys.public_key << " priv=" << keys.private_key
            << " algo=" << keys.algorithm;
        return out.str();
    }

    if (cmd == "mine") {
        std::string reward_address;
        std::size_t threads = 0;
        iss >> reward_address;
        iss >> threads;
        if (reward_address.empty()) {
            reward_address = "miner1";
        }
        if (threads == 0) {
            const auto hw = std::thread::hardware_concurrency();
            threads = hw > 0 ? static_cast<std::size_t>(hw) : 1;
        }

        std::string mined_hash;
        std::string error;
        if (!miner_.mine_next_block(reward_address, 500, threads, mined_hash, error)) {
            return "error: " + error;
        }

        std::ostringstream out;
        out << "mined block " << chain_.height() << " reward=" << reward_address << " threads=" << threads << " hash=" << mined_hash;
        return out.str();
    }

    if (cmd == "benchmark_objective") {
        std::size_t blocks = 0;
        std::size_t tx_per_block = 0;
        iss >> blocks >> tx_per_block;
        if (blocks == 0 || tx_per_block == 0) {
            return "error: usage benchmark_objective <blocks> <tx_per_block>";
        }

        const auto bench_start = std::chrono::steady_clock::now();
        std::size_t submitted = 0;
        std::size_t mined_total = 0;

        for (std::size_t b = 0; b < blocks; ++b) {
            for (std::size_t i = 0; i < tx_per_block; ++i) {
                Transaction tx{};
                tx.signer = "bench_signer";
                tx.signer_pubkey = "bench_pub";
                tx.signature = "pq=bench|privacy";
                tx.fee = 10 + static_cast<std::uint64_t>(i % 10);
                tx.nonce = static_cast<std::uint64_t>(b * tx_per_block + i + 1);
                tx.outputs.push_back(TxOutput{"bench_to_" + std::to_string(i % 256), 1});
                if (mempool_.submit(tx)) {
                    ++submitted;
                }
            }

            std::string mined_hash;
            std::string error;
            const auto hw = std::thread::hardware_concurrency();
            const std::size_t threads = hw > 0 ? static_cast<std::size_t>(hw) : 1;
            if (!miner_.mine_next_block("bench_miner", tx_per_block + 50, threads, mined_hash, error)) {
                return "error: benchmark mine failed: " + error;
            }
            mined_total += miner_.last_mined_txs();
        }

        const auto bench_end = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(bench_end - bench_start).count();
        const double sec = static_cast<double>(elapsed_ms > 0 ? elapsed_ms : 1) / 1000.0;
        const double avg_tps = sec > 0.0 ? static_cast<double>(mined_total) / sec : static_cast<double>(mined_total);
        const bool objective_tps_ok = avg_tps >= kObjectiveTps;
        const bool objective_privacy_ok = privacy_.strict_zk_mode() && privacy_.verifier_configured();
        const bool objective_100_ok = objective_tps_ok && objective_privacy_ok;

        std::ostringstream out;
        out << "bench_blocks=" << blocks
            << " bench_tx_per_block=" << tx_per_block
            << " bench_submitted=" << submitted
            << " bench_mined=" << mined_total
            << " bench_elapsed_ms=" << elapsed_ms
            << " bench_avg_tps=" << std::fixed << std::setprecision(2) << avg_tps
            << " objective_tps_ok=" << (objective_tps_ok ? "true" : "false")
            << " objective_privacy_ok=" << (objective_privacy_ok ? "true" : "false")
            << " objective_100_ok=" << (objective_100_ok ? "true" : "false");
        return out.str();
    }

    if (cmd == "getbalance") {
        std::string addr;
        iss >> addr;
        if (addr.empty()) {
            return "error: usage getbalance <address>";
        }
        return std::to_string(chain_.balance_of(addr));
    }

    if (cmd == "getbalance_instant") {
        std::string addr;
        iss >> addr;
        if (addr.empty()) {
            return "error: usage getbalance_instant <address>";
        }

        std::uint64_t incoming_unconfirmed = 0;
        const auto pending = mempool_.snapshot();
        for (const auto& tx : pending) {
            for (const auto& out : tx.outputs) {
                if (out.recipient == addr) {
                    incoming_unconfirmed += out.amount;
                }
            }
        }

        std::ostringstream out;
        out << "confirmed=" << chain_.balance_of(addr)
            << " incoming_unconfirmed=" << incoming_unconfirmed
            << " instant_total=" << (chain_.balance_of(addr) + incoming_unconfirmed);
        return out.str();
    }

    if (cmd == "sendtx") {
        if (!allow_insecure_tx_commands_) {
            return "error: insecure command disabled; use tx_build + sign_message + sendtx_signed";
        }
        std::string from;
        std::string pubkey;
        std::string privkey;
        std::string to;
        std::uint64_t amount = 0;
        std::uint64_t fee = 0;
        std::uint64_t nonce = 0;
        iss >> from >> pubkey >> privkey >> to >> amount >> fee >> nonce;
        if (from.empty() || pubkey.empty() || privkey.empty() || to.empty()) {
            return "error: usage sendtx <from_addr> <pubkey_hex> <privkey_hex> <to_addr> <amount> <fee> <nonce>";
        }

        const auto required_fee = std::max(recommended_min_fee(mempool_.size(), chain_.total_fees_last_block()),
                                           ai_optimizer_.recommended_fee_floor());
        if (fee < required_fee) {
            return "error: fee too low, required>=" + std::to_string(required_fee);
        }

        Transaction tx{};
        std::string error;
        if (!chain_.build_transaction(from, to, amount, fee, nonce, tx, error)) {
            return "error: " + error;
        }

        tx.signer = from;
        tx.signer_pubkey = pubkey;
        const auto msg = hash_transaction(tx);
        try {
            tx.signature = sign_message_hybrid(privkey, msg);
        } catch (const std::exception& e) {
            return std::string("error: signing failed: ") + e.what();
        }

        if (!chain_.validate_transaction(tx, error)) {
            return "error: " + error;
        }

        if (!node_.submit_transaction(tx, error)) {
            return "error: " + error;
        }
        return "ok:gossiped";
    }

    if (cmd == "sendtx_hash") {
        if (!allow_insecure_tx_commands_) {
            return "error: insecure command disabled; use tx_build + sign_message + sendtx_signed_hash";
        }
        std::string from;
        std::string pubkey;
        std::string privkey;
        std::string to;
        std::uint64_t amount = 0;
        std::uint64_t fee = 0;
        std::uint64_t nonce = 0;
        iss >> from >> pubkey >> privkey >> to >> amount >> fee >> nonce;
        if (from.empty() || pubkey.empty() || privkey.empty() || to.empty()) {
            return "error: usage sendtx_hash <from_addr> <pubkey_hex> <privkey_hex> <to_addr> <amount> <fee> <nonce>";
        }
        const auto required_fee = std::max(recommended_min_fee(mempool_.size(), chain_.total_fees_last_block()),
                                           ai_optimizer_.recommended_fee_floor());
        if (fee < required_fee) {
            return "error: fee too low, required>=" + std::to_string(required_fee);
        }

        Transaction tx{};
        std::string error;
        if (!chain_.build_transaction(from, to, amount, fee, nonce, tx, error)) {
            return "error: " + error;
        }

        tx.signer = from;
        tx.signer_pubkey = pubkey;
        const auto msg = hash_transaction(tx);
        try {
            tx.signature = sign_message_hybrid(privkey, msg);
        } catch (const std::exception& e) {
            return std::string("error: signing failed: ") + e.what();
        }

        if (!chain_.validate_transaction(tx, error)) {
            return "error: " + error;
        }

        if (!node_.submit_transaction(tx, error)) {
            return "error: " + error;
        }

        return hash_transaction(tx);
    }

    if (cmd == "tx_build") {
        std::string from;
        std::string pubkey;
        std::string to;
        std::uint64_t amount = 0;
        std::uint64_t fee = 0;
        std::uint64_t nonce = 0;
        iss >> from >> pubkey >> to >> amount >> fee >> nonce;
        if (from.empty() || pubkey.empty() || to.empty()) {
            return "error: usage tx_build <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce>";
        }

        if (derive_address_from_pubkey(pubkey) != from) {
            return "error: from/pubkey mismatch";
        }

        const auto required_fee = std::max(recommended_min_fee(mempool_.size(), chain_.total_fees_last_block()),
                                           ai_optimizer_.recommended_fee_floor());
        if (fee < required_fee) {
            return "error: fee too low, required>=" + std::to_string(required_fee);
        }

        Transaction tx{};
        std::string error;
        if (!chain_.build_transaction(from, to, amount, fee, nonce, tx, error)) {
            return "error: " + error;
        }

        tx.signer = from;
        tx.signer_pubkey = pubkey;
        tx.signature.clear();
        const auto sign_hash = hash_transaction(tx);
        return "sign_hash=" + sign_hash;
    }

    if (cmd == "sendtx_signed") {
        std::string from;
        std::string pubkey;
        std::string to;
        std::uint64_t amount = 0;
        std::uint64_t fee = 0;
        std::uint64_t nonce = 0;
        std::string sig_hex;
        iss >> from >> pubkey >> to >> amount >> fee >> nonce >> sig_hex;
        if (from.empty() || pubkey.empty() || to.empty() || sig_hex.empty()) {
            return "error: usage sendtx_signed <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex_without_pq_prefix>";
        }

        const auto required_fee = std::max(recommended_min_fee(mempool_.size(), chain_.total_fees_last_block()),
                                           ai_optimizer_.recommended_fee_floor());
        if (fee < required_fee) {
            return "error: fee too low, required>=" + std::to_string(required_fee);
        }

        Transaction tx{};
        std::string error;
        if (!chain_.build_transaction(from, to, amount, fee, nonce, tx, error)) {
            return "error: " + error;
        }

        tx.signer = from;
        tx.signer_pubkey = pubkey;
        tx.signature = std::string("pq=") + sig_hex;

        if (!chain_.validate_transaction(tx, error)) {
            return "error: " + error;
        }

        if (!node_.submit_transaction(tx, error)) {
            return "error: " + error;
        }
        return "ok:gossiped";
    }

    if (cmd == "sendtx_signed_hash") {
        std::string from;
        std::string pubkey;
        std::string to;
        std::uint64_t amount = 0;
        std::uint64_t fee = 0;
        std::uint64_t nonce = 0;
        std::string sig_hex;
        iss >> from >> pubkey >> to >> amount >> fee >> nonce >> sig_hex;
        if (from.empty() || pubkey.empty() || to.empty() || sig_hex.empty()) {
            return "error: usage sendtx_signed_hash <from_addr> <pubkey_hex> <to_addr> <amount> <fee> <nonce> <sig_hex_without_pq_prefix>";
        }

        const auto required_fee = std::max(recommended_min_fee(mempool_.size(), chain_.total_fees_last_block()),
                                           ai_optimizer_.recommended_fee_floor());
        if (fee < required_fee) {
            return "error: fee too low, required>=" + std::to_string(required_fee);
        }

        Transaction tx{};
        std::string error;
        if (!chain_.build_transaction(from, to, amount, fee, nonce, tx, error)) {
            return "error: " + error;
        }

        tx.signer = from;
        tx.signer_pubkey = pubkey;
        tx.signature = std::string("pq=") + sig_hex;

        if (!chain_.validate_transaction(tx, error)) {
            return "error: " + error;
        }

        if (!node_.submit_transaction(tx, error)) {
            return "error: " + error;
        }
        return hash_transaction(tx);
    }

    if (cmd == "tx_status") {
        std::string tx_hash;
        iss >> tx_hash;
        if (tx_hash.empty()) {
            return "error: usage tx_status <tx_hash>";
        }

        const auto mp = mempool_.snapshot();
        for (const auto& tx : mp) {
            if (hash_transaction(tx) == tx_hash) {
                return "status=mempool tx_hash=" + tx_hash;
            }
        }

        const auto& bs = chain_.blocks();
        for (const auto& b : bs) {
            for (std::size_t i = 0; i < b.transactions.size(); ++i) {
                if (hash_transaction(b.transactions[i]) == tx_hash) {
                    return "status=mined tx_hash=" + tx_hash + " block_height=" +
                           std::to_string(b.header.height) + " tx_index=" + std::to_string(i);
                }
            }
        }

        return "status=unknown tx_hash=" + tx_hash;
    }

    if (cmd == "stake") {
        std::string addr;
        std::uint64_t amount = 0;
        iss >> addr >> amount;
        if (addr.empty() || amount == 0) {
            return "error: usage stake <address> <amount>";
        }
        if (chain_.balance_of(addr) < amount) {
            return "error: insufficient on-chain balance to stake";
        }
        if (staking_.staked_of(addr) > (std::numeric_limits<std::uint64_t>::max() - amount)) {
            return "error: stake overflow";
        }
        const auto effective_balance = chain_.balance_of(addr) - staking_.staked_of(addr);
        if (effective_balance < amount) {
            return "error: insufficient unlocked balance to stake";
        }
        std::string error;
        if (!staking_.stake(addr, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "unstake") {
        std::string addr;
        std::uint64_t amount = 0;
        iss >> addr >> amount;
        if (addr.empty() || amount == 0) {
            return "error: usage unstake <address> <amount>";
        }
        std::string error;
        if (!staking_.unstake(addr, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "staked") {
        std::string addr;
        iss >> addr;
        if (addr.empty()) {
            return "error: usage staked <address>";
        }
        return std::to_string(staking_.staked_of(addr));
    }

    if (cmd == "stake_reward") {
        std::uint64_t pool = 0;
        iss >> pool;
        if (pool == 0) {
            return "error: usage stake_reward <amount>";
        }
        staking_.distribute_epoch_rewards(pool);
        return "ok";
    }

    if (cmd == "stake_policy") {
        std::string mode;
        iss >> mode;
        if (mode.empty() || mode == "get") {
            return "reward_cap_bps=" + std::to_string(staking_.reward_cap_bps());
        }
        if (mode != "set") {
            return "error: usage stake_policy <get|set> [cap_bps]";
        }
        std::uint64_t cap_bps = 0;
        std::string admin_addr;
        std::string admin_pubkey;
        std::string admin_sig;
        iss >> cap_bps >> admin_addr >> admin_pubkey >> admin_sig;
        if (cap_bps == 0 || admin_addr.empty() || admin_pubkey.empty() || admin_sig.empty()) {
            return "error: usage stake_policy set <cap_bps> <admin_addr> <admin_pubkey_hex> <admin_sig_hex>";
        }
        const std::string payload = "stake_policy_set|" + std::to_string(cap_bps);
        if (!verify_admin_signature(admin_addr, admin_pubkey, admin_sig, payload)) {
            return "error: invalid admin signature";
        }
        staking_.set_reward_cap_bps(cap_bps);
        return "ok:reward_cap_bps=" + std::to_string(staking_.reward_cap_bps());
    }

    if (cmd == "stake_claim") {
        std::string addr;
        iss >> addr;
        if (addr.empty()) {
            return "error: usage stake_claim <address>";
        }
        const auto reward = staking_.claim(addr);
        if (reward == 0) {
            return "0";
        }

        std::string error;
        if (!chain_.credit_balance(addr, reward, "staking_claim", error)) {
            return "error: failed to credit claim reward: " + error;
        }
        return std::to_string(reward);
    }

    if (cmd == "consume_stake_credit") {
        std::string addr;
        std::uint64_t amount = 0;
        iss >> addr >> amount;
        if (addr.empty() || amount == 0) {
            return "error: usage consume_stake_credit <address> <amount>";
        }
        std::string error;
        if (!staking_.consume_staked_credit(addr, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "contract_deploy") {
        std::string owner;
        iss >> owner;
        std::string code;
        std::getline(iss, code);
        if (owner.empty()) {
            return "error: usage contract_deploy <owner> <code>";
        }
        if (!code.empty() && code.front() == ' ') {
            code.erase(code.begin());
        }
        if (code.empty()) {
            return "error: contract code empty";
        }
        return contracts_.deploy(owner, code);
    }

    if (cmd == "contract_call") {
        std::string cid;
        std::string method;
        std::string key;
        std::int64_t value = 0;
        iss >> cid >> method >> key >> value;
        if (cid.empty() || method.empty()) {
            return "error: usage contract_call <id> <set|add|get|token_balance|swap_quote|zk_mint|zk_spend|zk_privacy_status> <key> <value>";
        }
        std::string out;
        std::string error;
        if (!contracts_.call(cid, method, key, value, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "bridge_register") {
        std::string chain;
        iss >> chain;
        std::string error;
        if (!bridge_.register_chain(chain, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "bridge_set_attestor") {
        std::string chain;
        std::string pubkey;
        std::string admin_addr;
        std::string admin_pubkey;
        std::string admin_sig;
        iss >> chain >> pubkey >> admin_addr >> admin_pubkey >> admin_sig;
        if (chain.empty() || pubkey.empty() || admin_addr.empty() || admin_pubkey.empty() || admin_sig.empty()) {
            return "error: usage bridge_set_attestor <chain> <attestor_pubkey_hex> <admin_addr> <admin_pubkey_hex> <admin_sig_hex>";
        }
        const std::string payload = "bridge_set_attestor|" + chain + "|" + pubkey;
        if (!verify_admin_signature(admin_addr, admin_pubkey, admin_sig, payload)) {
            return "error: invalid admin signature";
        }
        std::string error;
        if (!bridge_.set_attestor_key(chain, pubkey, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "bridge_attestor") {
        std::string chain;
        iss >> chain;
        if (chain.empty()) {
            return "error: usage bridge_attestor <chain>";
        }
        const auto key = bridge_.attestor_key(chain);
        if (key.empty()) {
            return "error: attestor not set";
        }
        return key;
    }

    if (cmd == "bridge_lock") {
        std::string chain;
        std::string user;
        std::uint64_t amount = 0;
        iss >> chain >> user >> amount;
        std::string receipt;
        std::string error;
        if (!bridge_.lock(chain, user, amount, receipt, error)) {
            return "error: " + error;
        }
        return receipt;
    }

    if (cmd == "bridge_mint") {
        std::string chain;
        std::string user;
        std::uint64_t amount = 0;
        iss >> chain >> user >> amount;
        std::string error;
        if (!bridge_.mint_wrapped(chain, user, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "bridge_mint_attested") {
        std::string chain;
        std::string user;
        std::uint64_t amount = 0;
        std::string attestation;
        iss >> chain >> user >> amount >> attestation;
        if (chain.empty() || user.empty() || amount == 0 || attestation.empty()) {
            return "error: usage bridge_mint_attested <chain> <user> <amount> <attestation_sig_hex>";
        }
        std::string error;
        if (!bridge_.mint_wrapped_attested(chain, user, amount, attestation, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "bridge_burn") {
        std::string chain;
        std::string user;
        std::uint64_t amount = 0;
        iss >> chain >> user >> amount;
        std::string error;
        if (!bridge_.burn_wrapped(chain, user, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "bridge_release") {
        std::string chain;
        std::string user;
        std::uint64_t amount = 0;
        iss >> chain >> user >> amount;
        std::string error;
        if (!bridge_.release(chain, user, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "bridge_release_attested") {
        std::string chain;
        std::string user;
        std::uint64_t amount = 0;
        std::string attestation;
        iss >> chain >> user >> amount >> attestation;
        if (chain.empty() || user.empty() || amount == 0 || attestation.empty()) {
            return "error: usage bridge_release_attested <chain> <user> <amount> <attestation_sig_hex>";
        }
        std::string error;
        if (!bridge_.release_attested(chain, user, amount, attestation, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "bridge_balance") {
        std::string chain;
        std::string user;
        iss >> chain >> user;
        if (chain.empty() || user.empty()) {
            return "error: usage bridge_balance <chain> <user>";
        }
        return std::to_string(bridge_.wrapped_balance(chain, user));
    }

    if (cmd == "token_create") {
        std::string symbol;
        std::string owner;
        std::uint64_t max_supply = 0;
        std::uint64_t initial_mint = 0;
        iss >> symbol >> owner >> max_supply >> initial_mint;
        if (symbol.empty() || owner.empty() || max_supply == 0) {
            return "error: usage token_create <symbol> <owner> <max_supply> <initial_mint>";
        }
        std::string error;
        if (!tokens_.create_token(symbol, owner, max_supply, initial_mint, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_create_ex") {
        std::string symbol;
        std::string name;
        std::string owner;
        std::uint64_t max_supply = 0;
        std::uint64_t initial_mint = 0;
        std::uint32_t decimals = 18;
        std::uint64_t burnable_u = 0;
        std::string dev_wallet;
        std::uint64_t dev_allocation = 0;
        iss >> symbol >> name >> owner >> max_supply >> initial_mint >> decimals >> burnable_u >> dev_wallet >> dev_allocation;
        if (symbol.empty() || name.empty() || owner.empty() || max_supply == 0) {
            return "error: usage token_create_ex <symbol> <name_no_space> <owner> <max_supply> <initial_mint> <decimals> <burnable_0_1> <dev_wallet_or_dash> <dev_allocation>";
        }
        if (dev_wallet == "-") {
            dev_wallet.clear();
        }
        std::string error;
        if (!tokens_.create_token_ex(symbol,
                                     name,
                                     owner,
                                     max_supply,
                                     initial_mint,
                                     decimals,
                                     burnable_u != 0,
                                     dev_wallet,
                                     dev_allocation,
                                     error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_mint") {
        std::string symbol;
        std::string caller;
        std::string to;
        std::uint64_t amount = 0;
        iss >> symbol >> caller >> to >> amount;
        if (symbol.empty() || caller.empty() || to.empty() || amount == 0) {
            return "error: usage token_mint <symbol> <caller> <to> <amount>";
        }
        std::string error;
        if (!tokens_.mint(symbol, caller, to, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_transfer") {
        std::string symbol;
        std::string from;
        std::string to;
        std::uint64_t amount = 0;
        iss >> symbol >> from >> to >> amount;
        if (symbol.empty() || from.empty() || to.empty() || amount == 0) {
            return "error: usage token_transfer <symbol> <from> <to> <amount>";
        }
        std::string error;
        if (!tokens_.transfer(symbol, from, to, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_balance") {
        std::string symbol;
        std::string owner;
        iss >> symbol >> owner;
        if (symbol.empty() || owner.empty()) {
            return "error: usage token_balance <symbol> <owner>";
        }
        return std::to_string(tokens_.balance_of(symbol, owner));
    }

    if (cmd == "token_burn") {
        std::string symbol;
        std::string from;
        std::uint64_t amount = 0;
        iss >> symbol >> from >> amount;
        if (symbol.empty() || from.empty() || amount == 0) {
            return "error: usage token_burn <symbol> <from> <amount>";
        }
        std::string error;
        if (!tokens_.burn(symbol, from, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_info") {
        std::string symbol;
        iss >> symbol;
        if (symbol.empty()) {
            return "error: usage token_info <symbol>";
        }
        std::string out;
        std::string error;
        if (!tokens_.token_info(symbol, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "token_set_policy") {
        std::string symbol;
        std::string caller;
        std::string treasury_wallet;
        std::uint64_t transfer_fee_bps = 0;
        std::uint64_t burn_fee_bps = 0;
        std::uint64_t paused_u = 0;
        iss >> symbol >> caller >> treasury_wallet >> transfer_fee_bps >> burn_fee_bps >> paused_u;
        if (symbol.empty() || caller.empty()) {
            return "error: usage token_set_policy <symbol> <caller_owner> <treasury_wallet_or_dash> <transfer_fee_bps> <burn_fee_bps> <paused_0_1>";
        }
        if (treasury_wallet == "-") {
            treasury_wallet.clear();
        }
        std::string error;
        if (!tokens_.set_policy(symbol,
                                caller,
                                treasury_wallet,
                                transfer_fee_bps,
                                burn_fee_bps,
                                paused_u != 0,
                                error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_blacklist") {
        std::string symbol;
        std::string caller;
        std::string wallet;
        std::uint64_t blocked_u = 0;
        iss >> symbol >> caller >> wallet >> blocked_u;
        if (symbol.empty() || caller.empty() || wallet.empty()) {
            return "error: usage token_blacklist <symbol> <caller_owner> <wallet> <blocked_0_1>";
        }
        std::string error;
        if (!tokens_.set_blacklist(symbol, caller, wallet, blocked_u != 0, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_fee_exempt") {
        std::string symbol;
        std::string caller;
        std::string wallet;
        std::uint64_t exempt_u = 0;
        iss >> symbol >> caller >> wallet >> exempt_u;
        if (symbol.empty() || caller.empty() || wallet.empty()) {
            return "error: usage token_fee_exempt <symbol> <caller_owner> <wallet> <exempt_0_1>";
        }
        std::string error;
        if (!tokens_.set_fee_exempt(symbol, caller, wallet, exempt_u != 0, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_set_limits") {
        std::string symbol;
        std::string caller;
        std::uint64_t max_tx = 0;
        std::uint64_t max_wallet = 0;
        iss >> symbol >> caller >> max_tx >> max_wallet;
        if (symbol.empty() || caller.empty()) {
            return "error: usage token_set_limits <symbol> <caller_owner> <max_tx_amount_or_0> <max_wallet_amount_or_0>";
        }
        std::string error;
        if (!tokens_.set_limits(symbol, caller, max_tx, max_wallet, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "swap_pool_create") {
        std::string token_a;
        std::string token_b;
        std::uint64_t fee_bps = 0;
        iss >> token_a >> token_b >> fee_bps;
        if (token_a.empty() || token_b.empty() || fee_bps == 0) {
            return "error: usage swap_pool_create <token_a> <token_b> <fee_bps>";
        }
        std::string error;
        if (!tokens_.create_pool(token_a, token_b, fee_bps, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "swap_add_liquidity") {
        std::string token_a;
        std::string token_b;
        std::string provider;
        std::uint64_t amount_a = 0;
        std::uint64_t amount_b = 0;
        iss >> token_a >> token_b >> provider >> amount_a >> amount_b;
        if (token_a.empty() || token_b.empty() || provider.empty() || amount_a == 0 || amount_b == 0) {
            return "error: usage swap_add_liquidity <token_a> <token_b> <provider> <amount_a> <amount_b>";
        }
        std::string error;
        if (!tokens_.add_liquidity(token_a, token_b, provider, amount_a, amount_b, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "swap_remove_liquidity") {
        std::string token_a;
        std::string token_b;
        std::string provider;
        std::uint64_t lp_amount = 0;
        iss >> token_a >> token_b >> provider >> lp_amount;
        if (token_a.empty() || token_b.empty() || provider.empty() || lp_amount == 0) {
            return "error: usage swap_remove_liquidity <token_a> <token_b> <provider> <lp_amount>";
        }
        std::uint64_t out_a = 0;
        std::uint64_t out_b = 0;
        std::string error;
        if (!tokens_.remove_liquidity(token_a, token_b, provider, lp_amount, out_a, out_b, error)) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "amount_a=" << out_a << " amount_b=" << out_b;
        return out.str();
    }

    if (cmd == "swap_quote") {
        std::string token_in;
        std::string token_out;
        std::uint64_t amount_in = 0;
        iss >> token_in >> token_out >> amount_in;
        if (token_in.empty() || token_out.empty() || amount_in == 0) {
            return "error: usage swap_quote <token_in> <token_out> <amount_in>";
        }
        std::uint64_t amount_out = 0;
        std::string error;
        if (!tokens_.quote_exact_in(token_in, token_out, amount_in, amount_out, error)) {
            return "error: " + error;
        }
        return std::to_string(amount_out);
    }

    if (cmd == "swap_pool_info") {
        std::string token_a;
        std::string token_b;
        iss >> token_a >> token_b;
        if (token_a.empty() || token_b.empty()) {
            return "error: usage swap_pool_info <token_a> <token_b>";
        }
        std::uint64_t reserve_a = 0;
        std::uint64_t reserve_b = 0;
        std::uint64_t fee_bps = 0;
        std::uint64_t lp_total_supply = 0;
        std::string error;
        if (!tokens_.pool_info(token_a, token_b, reserve_a, reserve_b, fee_bps, lp_total_supply, error)) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "reserve_" << token_a << '=' << reserve_a
            << " reserve_" << token_b << '=' << reserve_b
            << " fee_bps=" << fee_bps
            << " lp_total_supply=" << lp_total_supply;
        return out.str();
    }

    if (cmd == "swap_exact_in") {
        std::string token_in;
        std::string token_out;
        std::string trader;
        std::uint64_t amount_in = 0;
        std::uint64_t min_out = 0;
        iss >> token_in >> token_out >> trader >> amount_in >> min_out;
        if (token_in.empty() || token_out.empty() || trader.empty() || amount_in == 0) {
            return "error: usage swap_exact_in <token_in> <token_out> <trader> <amount_in> <min_out>";
        }
        std::uint64_t amount_out = 0;
        std::string error;
        if (!tokens_.swap_exact_in(token_in, token_out, trader, amount_in, min_out, amount_out, error)) {
            return "error: " + error;
        }
        return std::string("ok:amount_out=") + std::to_string(amount_out);
    }

    if (cmd == "swap_quote_route") {
        std::string route_str;
        std::uint64_t amount_in = 0;
        iss >> route_str >> amount_in;
        if (route_str.empty() || amount_in == 0) {
            return "error: usage swap_quote_route <TOKENA>TOKENB>TOKENC <amount_in>";
        }
        const auto route = split_route(route_str);
        std::uint64_t amount_out = 0;
        std::string error;
        if (!tokens_.quote_route_exact_in(route, amount_in, amount_out, error)) {
            return "error: " + error;
        }
        return std::to_string(amount_out);
    }

    if (cmd == "swap_route_exact_in") {
        std::string route_str;
        std::string trader;
        std::uint64_t amount_in = 0;
        std::uint64_t min_out = 0;
        iss >> route_str >> trader >> amount_in >> min_out;
        if (route_str.empty() || trader.empty() || amount_in == 0) {
            return "error: usage swap_route_exact_in <TOKENA>TOKENB>TOKENC <trader> <amount_in> <min_out>";
        }
        const auto route = split_route(route_str);
        std::uint64_t amount_out = 0;
        std::string error;
        if (!tokens_.swap_route_exact_in(route, trader, amount_in, min_out, amount_out, error)) {
            return "error: " + error;
        }
        return std::string("ok:amount_out=") + std::to_string(amount_out);
    }

    if (cmd == "swap_best_route") {
        std::string token_in;
        std::string token_out;
        std::uint64_t amount_in = 0;
        std::size_t max_hops = 3;
        iss >> token_in >> token_out >> amount_in >> max_hops;
        if (token_in.empty() || token_out.empty() || amount_in == 0) {
            return "error: usage swap_best_route <token_in> <token_out> <amount_in> [max_hops]";
        }
        std::vector<std::string> route;
        std::uint64_t amount_out = 0;
        std::string error;
        if (!tokens_.best_route_exact_in(token_in, token_out, amount_in, max_hops, route, amount_out, error)) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "amount_out=" << amount_out << " route=";
        for (std::size_t i = 0; i < route.size(); ++i) {
            out << route[i];
            if (i + 1 < route.size()) {
                out << '>';
            }
        }
        return out.str();
    }

    if (cmd == "swap_best_route_exact_in") {
        std::string token_in;
        std::string token_out;
        std::string trader;
        std::uint64_t amount_in = 0;
        std::uint64_t min_out = 0;
        std::uint64_t deadline_unix = 0;
        std::size_t max_hops = 3;
        iss >> token_in >> token_out >> trader >> amount_in >> min_out >> deadline_unix >> max_hops;
        if (token_in.empty() || token_out.empty() || trader.empty() || amount_in == 0 || deadline_unix == 0) {
            return "error: usage swap_best_route_exact_in <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> [max_hops]";
        }

        const auto now = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        if (now > deadline_unix) {
            return "error: deadline exceeded";
        }

        std::vector<std::string> route;
        std::uint64_t quote_out = 0;
        std::string error;
        if (!tokens_.best_route_exact_in(token_in, token_out, amount_in, max_hops, route, quote_out, error)) {
            return "error: " + error;
        }
        if (quote_out < min_out) {
            return "error: slippage exceeded before execution";
        }

        std::uint64_t exec_out = 0;
        if (!tokens_.swap_route_exact_in(route, trader, amount_in, min_out, exec_out, error)) {
            return "error: " + error;
        }

        std::ostringstream out;
        out << "ok:amount_out=" << exec_out << " route=";
        for (std::size_t i = 0; i < route.size(); ++i) {
            out << route[i];
            if (i + 1 < route.size()) {
                out << '>';
            }
        }
        return out.str();
    }

    if (cmd == "swap_best_route_sign_payload") {
        std::string token_in;
        std::string token_out;
        std::string trader;
        std::uint64_t amount_in = 0;
        std::uint64_t min_out = 0;
        std::uint64_t deadline_unix = 0;
        std::size_t max_hops = 3;
        iss >> token_in >> token_out >> trader >> amount_in >> min_out >> deadline_unix >> max_hops;
        if (token_in.empty() || token_out.empty() || trader.empty() || amount_in == 0 || deadline_unix == 0) {
            return "error: usage swap_best_route_sign_payload <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> [max_hops]";
        }
        return "swap_best_route_exact_in|" + token_in + "|" + token_out + "|" + trader +
               "|" + std::to_string(amount_in) + "|" + std::to_string(min_out) + "|" +
               std::to_string(deadline_unix) + "|" + std::to_string(max_hops);
    }

    if (cmd == "swap_best_route_exact_in_signed") {
        std::string token_in;
        std::string token_out;
        std::string trader;
        std::uint64_t amount_in = 0;
        std::uint64_t min_out = 0;
        std::uint64_t deadline_unix = 0;
        std::size_t max_hops = 3;
        std::string trader_pubkey;
        std::string trader_sig;
        iss >> token_in >> token_out >> trader >> amount_in >> min_out >> deadline_unix >> max_hops >> trader_pubkey >> trader_sig;
        if (token_in.empty() || token_out.empty() || trader.empty() || amount_in == 0 || deadline_unix == 0 ||
            trader_pubkey.empty() || trader_sig.empty()) {
            return "error: usage swap_best_route_exact_in_signed <token_in> <token_out> <trader> <amount_in> <min_out> <deadline_unix> <max_hops> <trader_pubkey_hex> <trader_sig_hex>";
        }

        const auto now = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        if (now > deadline_unix) {
            return "error: deadline exceeded";
        }

        const std::string sign_payload = "swap_best_route_exact_in|" + token_in + "|" + token_out + "|" + trader +
                                         "|" + std::to_string(amount_in) + "|" + std::to_string(min_out) + "|" +
                                         std::to_string(deadline_unix) + "|" + std::to_string(max_hops);
        if (!verify_message_signature_hybrid(trader_pubkey, sign_payload, std::string("pq=") + trader_sig)) {
            return "error: invalid trader signature";
        }

        const auto derived_trader = derive_address_from_pubkey(trader_pubkey);
        if (derived_trader != trader) {
            return "error: trader address/pubkey mismatch";
        }

        std::vector<std::string> route;
        std::uint64_t quote_out = 0;
        std::string error;
        if (!tokens_.best_route_exact_in(token_in, token_out, amount_in, max_hops, route, quote_out, error)) {
            return "error: " + error;
        }
        if (quote_out < min_out) {
            return "error: slippage exceeded before execution";
        }

        std::uint64_t exec_out = 0;
        if (!tokens_.swap_route_exact_in(route, trader, amount_in, min_out, exec_out, error)) {
            return "error: " + error;
        }

        std::ostringstream out;
        out << "ok:amount_out=" << exec_out << " route=";
        for (std::size_t i = 0; i < route.size(); ++i) {
            out << route[i];
            if (i + 1 < route.size()) {
                out << '>';
            }
        }
        return out.str();
    }

    if (cmd == "nft_mint") {
        std::string collection;
        std::string token_id;
        std::string owner;
        iss >> collection >> token_id >> owner;
        std::string metadata;
        std::getline(iss, metadata);
        if (!metadata.empty() && metadata.front() == ' ') {
            metadata.erase(metadata.begin());
        }
        if (collection.empty() || token_id.empty() || owner.empty()) {
            return "error: usage nft_mint <collection> <token_id> <owner> <metadata>";
        }
        std::string error;
        if (!tokens_.mint_nft(collection, token_id, owner, metadata, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "nft_transfer") {
        std::string collection;
        std::string token_id;
        std::string from;
        std::string to;
        iss >> collection >> token_id >> from >> to;
        if (collection.empty() || token_id.empty() || from.empty() || to.empty()) {
            return "error: usage nft_transfer <collection> <token_id> <from> <to>";
        }
        std::string error;
        if (!tokens_.transfer_nft(collection, token_id, from, to, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "nft_owner") {
        std::string collection;
        std::string token_id;
        iss >> collection >> token_id;
        if (collection.empty() || token_id.empty()) {
            return "error: usage nft_owner <collection> <token_id>";
        }
        const auto owner = tokens_.nft_owner_of(collection, token_id);
        return owner.empty() ? std::string("error: nft not found") : owner;
    }

    if (cmd == "privacy_native_verifier") {
        std::string mode;
        iss >> mode;
        if (mode.empty()) {
            return "error: usage privacy_native_verifier <pq_mldsa87>";
        }
        std::string error;
        if (!privacy_.set_native_verifier_mode(mode, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "privacy_mint_zk") {
        std::string owner;
        std::uint64_t amount = 0;
        std::string commitment;
        std::string nullifier;
        std::string proof_hex;
        std::string vk_hex;
        iss >> owner >> amount >> commitment >> nullifier >> proof_hex >> vk_hex;
        if (owner.empty() || amount == 0 || commitment.empty() || nullifier.empty() || proof_hex.empty() || vk_hex.empty()) {
            return "error: usage privacy_mint_zk <owner> <amount> <commitment_hex> <nullifier_hex> <proof_hex> <vk_hex>";
        }
        std::string error;
        const auto note_id = privacy_.mint_zk(owner, amount, commitment, nullifier, proof_hex, vk_hex, error);
        if (!error.empty()) {
            return "error: " + error;
        }
        return note_id;
    }

    if (cmd == "privacy_spend_zk") {
        std::string owner;
        std::string note_id;
        std::string recipient;
        std::uint64_t amount = 0;
        std::string nullifier;
        std::string proof_hex;
        std::string vk_hex;
        iss >> owner >> note_id >> recipient >> amount >> nullifier >> proof_hex >> vk_hex;
        if (owner.empty() || note_id.empty() || recipient.empty() || amount == 0 || nullifier.empty() || proof_hex.empty() || vk_hex.empty()) {
            return "error: usage privacy_spend_zk <owner> <note_id> <recipient> <amount> <nullifier_hex> <proof_hex> <vk_hex>";
        }
        std::string new_note;
        std::string error;
        if (!privacy_.spend_zk(owner, note_id, recipient, amount, nullifier, proof_hex, vk_hex, new_note, error)) {
            return "error: " + error;
        }
        return new_note;
    }

    if (cmd == "privacy_status") {
        std::ostringstream out;
        out << "verifier_configured=" << (privacy_.verifier_configured() ? "true" : "false")
            << " native_verifier_mode=" << privacy_.native_verifier_mode()
            << " strict_zk_mode=" << (privacy_.strict_zk_mode() ? "true" : "false")
            << " notes=" << privacy_.note_count()
            << " used_nullifiers=" << privacy_.used_nullifier_count();
        return out.str();
    }

    if (cmd == "pouw_storage_create_deal") {
        std::string client_addr;
        std::string content_root;
        std::uint64_t chunk_count = 0;
        std::uint64_t replication_factor = 0;
        std::uint64_t start_height = 0;
        std::uint64_t end_height = 0;
        std::uint64_t price_per_epoch = 0;
        iss >> client_addr >> content_root >> chunk_count >> replication_factor >> start_height >> end_height >> price_per_epoch;
        if (client_addr.empty() || content_root.empty() || chunk_count == 0 || replication_factor == 0 || end_height == 0) {
            return "error: usage pouw_storage_create_deal <client_addr> <content_root> <chunk_count> <replication_factor> <start_height> <end_height> <price_per_epoch>";
        }
        std::string deal_id;
        std::string error;
        if (!pouw_storage_.create_deal(client_addr,
                                       content_root,
                                       chunk_count,
                                       replication_factor,
                                       start_height,
                                       end_height,
                                       price_per_epoch,
                                       deal_id,
                                       error)) {
            return "error: " + error;
        }
        return "ok:deal_id=" + deal_id;
    }

    if (cmd == "pouw_storage_commit") {
        std::string deal_id;
        std::string worker_addr;
        std::string sealed_commitment;
        std::uint64_t collateral = 0;
        iss >> deal_id >> worker_addr >> sealed_commitment >> collateral;
        if (deal_id.empty() || worker_addr.empty() || sealed_commitment.empty() || collateral == 0) {
            return "error: usage pouw_storage_commit <deal_id> <worker_addr> <sealed_commitment> <collateral>";
        }
        std::string error;
        if (!pouw_storage_.register_commitment(deal_id, worker_addr, sealed_commitment, collateral, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "pouw_storage_challenge") {
        std::string deal_id;
        std::string worker_addr;
        std::uint64_t height = 0;
        iss >> deal_id >> worker_addr >> height;
        if (deal_id.empty() || worker_addr.empty() || height == 0) {
            return "error: usage pouw_storage_challenge <deal_id> <worker_addr> <height>";
        }
        std::string challenge_id;
        std::string error;
        if (!pouw_storage_.issue_challenge(deal_id, worker_addr, height, challenge_id, error)) {
            return "error: " + error;
        }
        return "ok:challenge_id=" + challenge_id;
    }

    if (cmd == "pouw_storage_submit_proof") {
        std::string challenge_id;
        std::string worker_addr;
        std::string proof_blob_hash;
        iss >> challenge_id >> worker_addr >> proof_blob_hash;
        if (challenge_id.empty() || worker_addr.empty() || proof_blob_hash.empty()) {
            return "error: usage pouw_storage_submit_proof <challenge_id> <worker_addr> <proof_blob_hash>";
        }
        std::string verdict;
        std::string error;
        if (!pouw_storage_.submit_proof(challenge_id, worker_addr, proof_blob_hash, verdict, error)) {
            return "error: " + error;
        }
        return "ok:verdict=" + verdict;
    }

    if (cmd == "pouw_storage_deal_status") {
        std::string deal_id;
        iss >> deal_id;
        if (deal_id.empty()) {
            return "error: usage pouw_storage_deal_status <deal_id>";
        }
        std::string out;
        std::string error;
        if (!pouw_storage_.deal_status(deal_id, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "pouw_storage_worker_status") {
        std::string worker_addr;
        iss >> worker_addr;
        if (worker_addr.empty()) {
            return "error: usage pouw_storage_worker_status <worker_addr>";
        }
        std::string out;
        std::string error;
        if (!pouw_storage_.worker_status(worker_addr, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "pouw_compute_submit_job") {
        std::string requester_addr;
        std::string job_type;
        std::string input_ref;
        std::string determinism_profile;
        std::uint64_t max_latency_sec = 0;
        std::uint64_t reward_budget = 0;
        std::uint64_t min_reputation = 0;
        iss >> requester_addr >> job_type >> input_ref >> determinism_profile >> max_latency_sec >> reward_budget >> min_reputation;
        if (requester_addr.empty() || job_type.empty() || input_ref.empty() || max_latency_sec == 0 || reward_budget == 0) {
            return "error: usage pouw_compute_submit_job <requester_addr> <job_type> <input_ref> <determinism_profile> <max_latency_sec> <reward_budget> <min_reputation>";
        }
        std::string job_id;
        std::string error;
        if (!pouw_compute_.submit_job(requester_addr,
                                      job_type,
                                      input_ref,
                                      determinism_profile,
                                      max_latency_sec,
                                      reward_budget,
                                      min_reputation,
                                      job_id,
                                      error)) {
            return "error: " + error;
        }
        return "ok:job_id=" + job_id;
    }

    if (cmd == "pouw_compute_assign_job") {
        std::string job_id;
        std::string worker_addr;
        std::uint64_t collateral_locked = 0;
        iss >> job_id >> worker_addr >> collateral_locked;
        if (job_id.empty() || worker_addr.empty() || collateral_locked == 0) {
            return "error: usage pouw_compute_assign_job <job_id> <worker_addr> <collateral_locked>";
        }
        std::string error;
        if (!pouw_compute_.assign_job(job_id, worker_addr, collateral_locked, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "pouw_compute_submit_result") {
        std::string job_id;
        std::string worker_addr;
        std::string output_ref;
        std::string result_hash;
        std::string proof_ref;
        iss >> job_id >> worker_addr >> output_ref >> result_hash >> proof_ref;
        if (job_id.empty() || worker_addr.empty() || output_ref.empty() || result_hash.empty()) {
            return "error: usage pouw_compute_submit_result <job_id> <worker_addr> <output_ref> <result_hash> <proof_ref>";
        }
        std::string error;
        if (!pouw_compute_.submit_result(job_id, worker_addr, output_ref, result_hash, proof_ref, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "pouw_compute_validate") {
        std::string job_id;
        std::string validator_addr;
        std::string verdict;
        std::uint64_t score = 0;
        iss >> job_id >> validator_addr >> verdict >> score;
        if (job_id.empty() || validator_addr.empty() || verdict.empty()) {
            return "error: usage pouw_compute_validate <job_id> <validator_addr> <pass|fail> <score>";
        }
        std::string error;
        if (!pouw_compute_.submit_validation(job_id, validator_addr, verdict, score, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "pouw_compute_job_status") {
        std::string job_id;
        iss >> job_id;
        if (job_id.empty()) {
            return "error: usage pouw_compute_job_status <job_id>";
        }
        std::string out;
        std::string error;
        if (!pouw_compute_.job_status(job_id, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "pouw_compute_worker_status") {
        std::string worker_addr;
        iss >> worker_addr;
        if (worker_addr.empty()) {
            return "error: usage pouw_compute_worker_status <worker_addr>";
        }
        std::string out;
        std::string error;
        if (!pouw_compute_.worker_status(worker_addr, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "pm_send_ttl") {
        std::string sender;
        std::string recipient;
        std::string ciphertext_ref;
        std::uint64_t ttl_sec = 0;
        std::string policy;
        iss >> sender >> recipient >> ciphertext_ref >> ttl_sec >> policy;
        if (sender.empty() || recipient.empty() || ciphertext_ref.empty() || ttl_sec == 0) {
            return "error: usage pm_send_ttl <sender> <recipient> <ciphertext_ref> <ttl_sec> [policy]";
        }

        auto tip_block = chain_.tip();
        const auto anchor_height = tip_block.header.height;
        const auto anchor_block_hash = hash_block_header(tip_block.header);

        std::string msg_id;
        std::string error;
        if (!private_messaging_.send_ttl(sender,
                                        recipient,
                                        ciphertext_ref,
                                        ttl_sec,
                                        anchor_height,
                                        anchor_block_hash,
                                        policy,
                                        msg_id,
                                        error)) {
            return "error: " + error;
        }
        return "ok:msg_id=" + msg_id +
               " anchor_height=" + std::to_string(anchor_height) +
               " anchor_block_hash=" + anchor_block_hash;
    }

    if (cmd == "pm_inbox") {
        std::string recipient;
        iss >> recipient;
        if (recipient.empty()) {
            return "error: usage pm_inbox <recipient>";
        }
        std::string out;
        std::string error;
        if (!private_messaging_.inbox(recipient, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "pm_status") {
        std::string msg_id;
        iss >> msg_id;
        if (msg_id.empty()) {
            return "error: usage pm_status <msg_id>";
        }
        std::string out;
        std::string error;
        if (!private_messaging_.status(msg_id, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "pm_fetch") {
        std::string msg_id;
        std::string requester;
        iss >> msg_id >> requester;
        if (msg_id.empty() || requester.empty()) {
            return "error: usage pm_fetch <msg_id> <requester>";
        }
        std::string out;
        std::string error;
        if (!private_messaging_.fetch(msg_id, requester, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "pm_destroy") {
        std::string msg_id;
        std::string requester;
        iss >> msg_id >> requester;
        if (msg_id.empty() || requester.empty()) {
            return "error: usage pm_destroy <msg_id> <requester>";
        }
        std::string error;
        if (!private_messaging_.destroy(msg_id, requester, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "pm_purge") {
        return "ok:purged=" + std::to_string(private_messaging_.purge_expired());
    }

    if (cmd == "ai_status") {
        return ai_optimizer_.status();
    }

    if (cmd == "stake_claimable") {
        std::string addr;
        iss >> addr;
        if (addr.empty()) {
            return "error: usage stake_claimable <address>";
        }
        return std::to_string(staking_.claimable_of(addr));
    }

    return "error: unknown command";
}

} // namespace addition
