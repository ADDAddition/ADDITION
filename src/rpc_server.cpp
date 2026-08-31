#include "addition/rpc_server.hpp"

#include "addition/block.hpp"
#include "addition/btc_hygiene.hpp"
#include "addition/config.hpp"
#include "addition/crypto.hpp"
#include "addition/rpc_access.hpp"
#include "addition/wallet.hpp"
#include "addition/wallet_keys.hpp"
#include "addition/wallet_store.hpp"

#include <exception>
#include <algorithm>
#include <sstream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>

namespace addition {

namespace {

constexpr double kObjectiveTps = 100000.0;

std::string join_csv(const std::vector<std::string>& items) {
    std::ostringstream out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << items[i];
    }
    return out.str();
}

std::uint64_t recommended_min_fee(std::size_t mempool_size, std::uint64_t last_block_fees) {
    std::uint64_t base = 0;
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

bool is_all_digits(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

bool parse_u64_token(std::istringstream& iss, std::uint64_t& out) {
    std::string tok;
    if (!(iss >> tok) || !is_all_digits(tok)) {
        return false;
    }
    try {
        out = static_cast<std::uint64_t>(std::stoull(tok));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool no_trailing_token(std::istringstream& iss) {
    std::string extra;
    return !(iss >> extra);
}

std::string format_block(const Block& b) {
    std::ostringstream out;
    out << "height=" << b.header.height
        << " hash=" << hash_block_header(b.header)
        << " previous_hash=" << b.header.previous_hash
        << " timestamp=" << b.header.timestamp
        << " nonce=" << b.header.nonce
        << " difficulty_target=" << b.header.difficulty_target
        << " merkle_root=" << b.header.merkle_root
        << " tx_count=" << b.transactions.size();
    if (!b.transactions.empty()) {
        out << " tx_hashes=";
        for (std::size_t i = 0; i < b.transactions.size(); ++i) {
            if (i > 0) {
                out << ',';
            }
            out << hash_transaction(b.transactions[i]);
        }
    }
    return out.str();
}

std::string derive_address_from_pubkey(const std::string& pubkey_hex) {
    return hash_committed_address_hex(kMlDsa87SchemeId, pubkey_hex);
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

// Drop the RPC mutex while PoW hashes. getinfo/getblock take the same mutex,
// so holding it for memory_hard (or any long search) hangs public-read HTTP.
struct UnlockWhileHashing {
    std::unique_lock<std::mutex>& lock;
    explicit UnlockWhileHashing(std::unique_lock<std::mutex>& lock) : lock(lock) {
        lock.unlock();
    }
    ~UnlockWhileHashing() {
        lock.lock();
    }
};

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
                                         bool strict_admin_mode,
                                         std::string wallet_dir)
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
            strict_admin_mode_(strict_admin_mode),
            wallets_(std::move(wallet_dir)) {}

void RpcServer::set_auto_mine_status(bool enabled, std::uint32_t interval_sec) {
    auto_mine_enabled_ = enabled;
    auto_mine_interval_sec_ = interval_sec == 0 ? 60 : interval_sec;
}

void RpcServer::set_advertised_p2p(std::string endpoint) {
    advertised_p2p_ = std::move(endpoint);
}

std::uint64_t RpcServer::unlocked_balance(const std::string& address) const {
    const auto confirmed = chain_.balance_of(address);
    const auto staked = staking_.staked_of(address);
    return confirmed > staked ? (confirmed - staked) : 0ULL;
}

std::string RpcServer::public_rpc_banner() const {
    return public_rpc_banner_text(chain_.config().network_mode);
}

std::string RpcServer::handle_command(const std::string& line, bool trusted) {
    std::unique_lock<std::mutex> lock(mu_);
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
        const auto& net = chain_.config();
        const auto all_peers = peers_.peers();
        const auto public_peers = public_advertised_peers(all_peers, advertised_p2p_);
        const auto listed_peers = trusted ? all_peers : public_peers;
        const auto listed_bootstrap = trusted ? net.bootstrap_peers
                                              : public_advertised_peers(net.bootstrap_peers, {});
        std::ostringstream out;
        out << "network=" << net.network_mode
            << " network_name=" << net.network_name
            << " network_id=" << net.network_id
            << " height=" << chain_.height()
            << " mempool=" << mempool_.size()
            << " total_staked=" << staking_.total_staked()
            << " peers=" << listed_peers.size()
            << " bootstrap_peers=" << join_csv(listed_bootstrap);
        if (trusted) {
            out << " local_peers=" << peers_.loopback_peer_count();
        }
        if (!advertised_p2p_.empty()) {
            out << " advertised_p2p=" << advertised_p2p_;
        }
        out << " difficulty_target=" << chain_.current_difficulty_target()
            << " next_reward=" << chain_.current_block_reward()
            << " fees_last_block=" << chain_.total_fees_last_block()
            << " dynamic_min_fee=" << dyn_fee
            << " max_supply=" << chain_.max_supply()
            << " last_mine_ms=" << miner_.last_mine_ms()
            << " last_mined_txs=" << miner_.last_mined_txs()
            << " last_tps=" << std::fixed << std::setprecision(2) << miner_.last_tps()
            << " last_verify_ms=" << chain_.last_batch_verify_ms()
            << " last_verify_count=" << chain_.last_batch_verify_count()
            << " last_verify_per_sec=" << std::fixed << std::setprecision(2) << chain_.last_batch_verify_per_sec()
            << " last_dropped_mempool_txs=" << miner_.last_dropped_junk()
            << " pq_mode=strict"
            << " allowed_sig_algs=" << allowed_sig_algs_list()
            << " pow_algorithm=" << pow_algorithm_label(net.pow_algorithm)
            << " pow_profile=" << net.pow_profile
            << " confirmations_policy=" << net.confirmations_policy
            << " economic_security=" << net.economic_security
            << " mine_deadline_sec=" << mine_deadline_seconds(net)
            << " mine_threads=" << default_mine_thread_count()
            << " privacy_verifier=sha3_opening"
            << " privacy_mode=sha3_opening"
            << " privacy_claim=opening_not_zk"
            << " privacy_ok=true"
            << " require_privacy_pool=" << (net.require_privacy_pool ? "true" : "false")
            << " auto_mine=" << (auto_mine_enabled_ ? "on" : "off")
            << " auto_mine_interval_sec=" << auto_mine_interval_sec_;
        if (trusted) {
            out << " privacy_master_key=" << (PrivacyPool::master_key_configured() ? "set" : "missing");
        }
        return out.str();
    }

    if (cmd == "protocol_status") {
        std::ostringstream out;
        out << "measured_tps=" << std::fixed << std::setprecision(2) << miner_.last_tps()
            << " measured_last_mine_ms=" << miner_.last_mine_ms()
            << " measured_last_mined_txs=" << miner_.last_mined_txs()
            << " measured_verify_ms=" << chain_.last_batch_verify_ms()
            << " measured_verify_count=" << chain_.last_batch_verify_count()
            << " measured_verify_per_sec=" << std::fixed << std::setprecision(2)
            << chain_.last_batch_verify_per_sec()
            << " measured_dropped_mempool_txs=" << miner_.last_dropped_junk()
            << " privacy_mode=sha3_opening"
            << " privacy_ok=true"
            << " privacy_verifier=sha3_opening"
            << " privacy_claim=opening_not_zk"
            << " verifier_configured=" << (privacy_.verifier_configured() ? "true" : "false")
            << " pouw_storage_check=first_nibble_parity"
            << " research_goal_tps=" << std::fixed << std::setprecision(0) << kObjectiveTps
            << " research_goal_is_not_a_measurement=true";
        return out.str();
    }

    if (cmd == "fee_info") {
        const auto msz = mempool_.size();
        const auto last = chain_.total_fees_last_block();
        const auto dyn = recommended_min_fee(msz, last);
        const auto ai_floor = ai_optimizer_.recommended_fee_floor();
        std::ostringstream out;
        out << "base_min_fee=" << chain_.config().min_fee
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

    if (cmd == "hygiene_classify") {
        std::string path;
        iss >> path;
        if (path.empty()) {
            path = "fixtures/btc_hygiene_samples.json";
        }
        std::vector<BtcScriptSample> samples;
        std::string load_error;
        if (!load_btc_hygiene_fixtures(path, samples, load_error)) {
            return "error: " + load_error;
        }
        const auto reports = classify_btc_samples(samples);
        std::ostringstream out;
        out << "ok:hygiene_rehearsal samples=" << reports.size()
            << " moves_bitcoin=0 claim=attestation_not_bip360";
        for (const auto& r : reports) {
            out << " | " << format_hygiene_report(r);
        }
        return out.str();
    }

    if (cmd == "hygiene_verify") {
        std::string note;
        std::getline(iss, note);
        const auto first = note.find_first_not_of(" \t");
        if (first == std::string::npos) {
            return "error: usage hygiene_verify <receipt_note>";
        }
        note = note.substr(first);
        std::string body;
        std::string pub;
        std::string sig;
        std::string verify_error;
        if (!split_hygiene_receipt(note, body, pub, sig, verify_error)) {
            return "error: " + verify_error;
        }
        BtcHygieneReport parsed{};
        if (!parse_hygiene_receipt_body(body, parsed, verify_error)) {
            return "error: " + verify_error;
        }
        if (!verify_message_signature_hybrid(pub, body, sig)) {
            return "error: garbage hygiene receipt rejected";
        }
        std::string bind_error;
        const auto scheme = infer_sig_scheme_from_pubkey_hex(pub);
        std::string attestor = "unknown";
        if (scheme != SigScheme::Unknown) {
            attestor = hash_committed_address_hex(scheme, pub);
            if (!address_binds_pubkey(attestor, scheme, pub, bind_error)) {
                return "error: garbage hygiene receipt rejected";
            }
        }
        return std::string("ok:hygiene_receipt ") + format_hygiene_report(parsed) +
               " attestor=" + attestor + " moves_bitcoin=0 claim=attestation_not_bip360";
    }

    if (cmd == "hygiene_attest") {
        std::string name;
        std::string btc_addr;
        std::uint64_t height = 0;
        std::string class_name;
        int reuse = 0;
        int pubkey_on_chain = 0;
        iss >> name >> btc_addr >> height >> class_name >> reuse >> pubkey_on_chain;
        if (name.empty() || btc_addr.empty() || class_name.empty()) {
            return "error: usage hygiene_attest <wallet> <btc_addr> <height> <class> [reuse] [pubkey_on_chain]";
        }
        StoredWallet stored{};
        std::string attest_error;
        if (!wallets_.load(name, stored, attest_error, true)) {
            return "error: " + attest_error;
        }
        BtcHygieneReport report{};
        report.address = btc_addr;
        report.height = height;
        report.class_name = class_name;
        report.address_reuse = reuse != 0;
        report.pubkey_already_on_chain = pubkey_on_chain != 0;
        const auto body = hygiene_receipt_body(report);
        std::string sig;
        try {
            sig = sign_message_hybrid(stored.private_key, body);
        } catch (const std::exception& e) {
            return std::string("error: hygiene sign failed: ") + e.what();
        }
        const auto note = assemble_hygiene_receipt(body, stored.public_key, sig);
        std::ostringstream out;
        out << "ok:hygiene_receipt"
            << " " << format_hygiene_report(report)
            << " attestor=" << stored.address
            << " moves_bitcoin=0"
            << " claim=attestation_not_bip360"
            << " note=" << note;
        return out.str();
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
        std::size_t sent = 0;
        std::string err;
        if (!node_.flush_outbound_gossip(sent, err)) {
            return "error: " + (err.empty() ? std::string("gossip flush failed") : err);
        }
        std::ostringstream out;
        out << "ok:messages=" << sent;
        if (!err.empty()) {
            out << " note=" << err;
        }
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
        const auto public_peers = public_advertised_peers(peers_.peers(), advertised_p2p_);
        if (!trusted) {
            return join_csv(public_peers);
        }
        std::ostringstream out;
        out << join_csv(public_peers);
        const auto local = peers_.loopback_peers();
        if (!local.empty()) {
            if (!public_peers.empty()) {
                out << ' ';
            }
            out << "local=" << join_csv(local);
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
        std::string name;
        std::string scheme;
        iss >> name >> scheme;
        if (name.empty()) {
            name = "default";
        }
        if (scheme.empty()) {
            scheme = kMlDsa87SchemeId;
        }
        if (!wallets_.configured()) {
            return "error: wallet store not configured";
        }
        WalletKeys keys{};
        try {
            keys = generate_wallet_keys(scheme);
        } catch (const std::exception& e) {
            return std::string("error: wallet generation failed: ") + e.what();
        }
        StoredWallet stored{};
        std::string error;
        if (!wallets_.create(name, keys, stored, error)) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "address=" << stored.address
            << " address_chars=" << stored.address.size()
            << " pub=" << stored.public_key
            << " algo=" << stored.algorithm
            << " name=" << stored.name
            << " path=" << stored.path
            << " priv_printed=0";
        return out.str();
    }

    if (cmd == "wallet_list") {
        std::string error;
        const auto listed = wallets_.list(error);
        if (!error.empty() && listed.empty()) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "wallets=" << listed.size();
        for (const auto& w : listed) {
            out << " name=" << w.name << " address=" << w.address << " algo=" << w.algorithm;
        }
        return out.str();
    }

    if (cmd == "wallet_info") {
        std::string name;
        iss >> name;
        if (name.empty()) {
            return "error: usage wallet_info <name>";
        }
        StoredWallet stored{};
        std::string error;
        if (!wallets_.load(name, stored, error, false)) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "name=" << stored.name
            << " address=" << stored.address
            << " pub=" << stored.public_key
            << " algo=" << stored.algorithm
            << " path=" << stored.path
            << " next_nonce=" << chain_.next_nonce(stored.address)
            << " confirmed=" << unlocked_balance(stored.address)
            << " staked=" << staking_.staked_of(stored.address);
        return out.str();
    }

    if (cmd == "wallet_balance") {
        std::string name;
        iss >> name;
        if (name.empty()) {
            return "error: usage wallet_balance <name>";
        }
        StoredWallet stored{};
        std::string error;
        if (!wallets_.load(name, stored, error, false)) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "name=" << stored.name
            << " address=" << stored.address
            << " confirmed=" << unlocked_balance(stored.address)
            << " staked=" << staking_.staked_of(stored.address);
        return out.str();
    }

    if (cmd == "wallet_sign") {
        std::string name;
        std::string message_hex;
        iss >> name >> message_hex;
        if (name.empty() || message_hex.empty()) {
            return "error: usage wallet_sign <name> <message_hex_utf8>";
        }
        StoredWallet stored{};
        std::string error;
        if (!wallets_.load(name, stored, error, true)) {
            return "error: " + error;
        }
        std::vector<std::uint8_t> msg_bytes;
        if (!hex_to_bytes(message_hex, msg_bytes, error)) {
            return "error: invalid message_hex: " + error;
        }
        if (msg_bytes.empty() || msg_bytes.size() > 8192) {
            return "error: message size invalid";
        }
        const std::string msg(reinterpret_cast<const char*>(msg_bytes.data()), msg_bytes.size());
        try {
            return sign_message_hybrid(stored.private_key, msg);
        } catch (const std::exception& e) {
            return std::string("error: signing failed: ") + e.what();
        }
    }

    if (cmd == "wallet_send") {
        std::string name;
        std::string to;
        std::uint64_t amount = 0;
        std::uint64_t fee = 0;
        iss >> name >> to >> amount >> fee;
        if (name.empty() || to.empty() || amount == 0) {
            return "error: usage wallet_send <name> <to_addr> <amount> [fee]";
        }
        StoredWallet stored{};
        std::string error;
        if (!wallets_.load(name, stored, error, true)) {
            return "error: " + error;
        }
        const auto required_fee = std::max(recommended_min_fee(mempool_.size(), chain_.total_fees_last_block()),
                                           ai_optimizer_.recommended_fee_floor());
        if (fee == 0) {
            fee = required_fee;
        }
        if (fee < required_fee) {
            return "error: fee too low, required>=" + std::to_string(required_fee);
        }
        if (unlocked_balance(stored.address) < (amount + fee)) {
            return "error: insufficient unlocked balance";
        }

        Wallet wallet(stored.address, stored.public_key, stored.private_key);
        Transaction tx{};
        if (!wallet.build_signed_send(chain_, to, amount, fee, tx, error)) {
            return "error: " + error;
        }
        if (!node_.submit_transaction(tx, error)) {
            return "error: " + error;
        }
        {
            std::size_t sent = 0;
            std::string gossip_err;
            node_.flush_outbound_gossip(sent, gossip_err);
        }
        std::ostringstream out;
        out << "ok:gossiped"
            << " hash=" << hash_transaction(tx)
            << " from=" << stored.address
            << " to=" << to
            << " amount=" << amount
            << " fee=" << fee
            << " nonce=" << tx.nonce
            << " confirmations=" << chain_.tx_confirmations(hash_transaction(tx));
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
            threads = default_mine_thread_count();
        }

        std::string mined_hash;
        std::string error;
        bool ok = false;
        {
            UnlockWhileHashing yield(lock);
            ok = miner_.mine_next_block(reward_address, 500, threads, mined_hash, error);
        }
        if (!ok) {
            return "error: " + error;
        }
        {
            std::string announce_err;
            node_.announce_tip(announce_err);
            std::size_t sent = 0;
            std::string gossip_err;
            node_.flush_outbound_gossip(sent, gossip_err);
        }

        std::ostringstream out;
        out << "mined block " << chain_.height() << " reward=" << reward_address << " threads=" << threads << " hash=" << mined_hash;
        return out.str();
    }

    if (cmd == "benchmark_objective") {
        std::size_t blocks = 0;
        std::size_t verify_samples = 0;
        iss >> blocks >> verify_samples;
        if (blocks == 0 || verify_samples == 0) {
            return "error: usage benchmark_objective <blocks> <verify_samples>";
        }
        if (verify_samples > 16) {
            verify_samples = 16;
        }

        const auto mempool_before = mempool_.size();
        const auto bench_start = std::chrono::steady_clock::now();

        std::vector<PqVerifyItem> jobs;
        jobs.reserve(verify_samples);
        try {
            for (std::size_t i = 0; i < verify_samples; ++i) {
                const auto keys = generate_wallet_keys();
                const auto msg = std::string("addition-bench|") + std::to_string(i);
                jobs.push_back(PqVerifyItem{keys.public_key, msg, sign_message_hybrid(keys.private_key, msg)});
            }
        } catch (const std::exception& e) {
            return std::string("error: benchmark keygen/sign failed: ") + e.what();
        }

        std::size_t verify_ok = 0;
        std::uint64_t verify_ms = 0;
        std::string verify_err;
        const auto hw = std::thread::hardware_concurrency();
        const std::size_t threads = hw > 0 ? static_cast<std::size_t>(hw) : 1;
        if (!pq_verify_messages_parallel(jobs, threads, verify_ok, verify_ms, verify_err)) {
            return "error: benchmark pq verify failed: " + verify_err;
        }
        const double verify_sec = static_cast<double>(verify_ms > 0 ? verify_ms : 1) / 1000.0;
        const double verify_per_sec =
            verify_sec > 0.0 ? static_cast<double>(verify_ok) / verify_sec : static_cast<double>(verify_ok);

        std::uint64_t mine_ms_total = 0;
        for (std::size_t b = 0; b < blocks; ++b) {
            std::string mined_hash;
            std::string error;
            bool ok = false;
            // max_txs=0: header PoW only. Do not pull or inject mempool txs.
            {
                UnlockWhileHashing yield(lock);
                ok = miner_.mine_next_block("bench_miner", 0, threads, mined_hash, error);
            }
            if (!ok) {
                return "error: benchmark mine failed: " + error;
            }
            mine_ms_total += miner_.last_mine_ms();
        }

        const auto bench_end = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(bench_end - bench_start).count();
        const auto mempool_after = mempool_.size();

        std::ostringstream out;
        out << "bench_blocks=" << blocks
            << " bench_verify_samples=" << verify_samples
            << " bench_verify_ok=" << verify_ok
            << " bench_verify_ms=" << verify_ms
            << " bench_verify_per_sec=" << std::fixed << std::setprecision(2) << verify_per_sec
            << " bench_mine_ms=" << mine_ms_total
            << " bench_mined_txs=0"
            << " bench_submitted=0"
            << " bench_elapsed_ms=" << elapsed_ms
            << " bench_mempool_before=" << mempool_before
            << " bench_mempool_after=" << mempool_after
            << " privacy_claim=opening_not_zk"
            << " research_goal_tps=" << std::fixed << std::setprecision(0) << kObjectiveTps
            << " research_goal_is_not_a_measurement=true";
        return out.str();
    }

    if (cmd == "getbalance") {
        std::string addr;
        iss >> addr;
        if (addr.empty()) {
            return "error: usage getbalance <address>";
        }
        return std::to_string(unlocked_balance(addr));
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
        const auto confirmed = unlocked_balance(addr);
        out << "confirmed=" << confirmed
            << " incoming_unconfirmed=" << incoming_unconfirmed
            << " instant_total=" << (confirmed + incoming_unconfirmed)
            << " staked=" << staking_.staked_of(addr);
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
        if (unlocked_balance(from) < (amount + fee)) {
            return "error: insufficient unlocked balance";
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
                return "status=mempool tx_hash=" + tx_hash + " confirmations=0";
            }
        }

        const auto confirms = chain_.tx_confirmations(tx_hash);
        const auto& bs = chain_.blocks();
        for (const auto& b : bs) {
            for (std::size_t i = 0; i < b.transactions.size(); ++i) {
                if (hash_transaction(b.transactions[i]) == tx_hash) {
                    return "status=mined tx_hash=" + tx_hash + " block_height=" +
                           std::to_string(b.header.height) + " tx_index=" + std::to_string(i) +
                           " confirmations=" + std::to_string(confirms);
                }
            }
        }

        return "status=unknown tx_hash=" + tx_hash + " confirmations=0";
    }

    if (cmd == "getblock") {
        std::string id;
        iss >> id;
        if (id.empty()) {
            return "error: usage getblock <height_or_hash>";
        }
        std::optional<Block> found;
        if (is_all_digits(id)) {
            try {
                const auto height = static_cast<std::uint64_t>(std::stoull(id));
                found = chain_.block_at(height);
            } catch (const std::exception&) {
                return "error: invalid block height";
            }
        } else {
            found = chain_.block_by_hash(id);
        }
        if (!found.has_value()) {
            return "error: block not found";
        }
        return format_block(*found);
    }

    if (cmd == "getblockraw") {
        std::string id;
        iss >> id;
        if (id.empty() || !is_all_digits(id)) {
            return "error: usage getblockraw <height>";
        }
        try {
            const auto height = static_cast<std::uint64_t>(std::stoull(id));
            std::string payload;
            std::string err;
            if (!node_.get_block_payload(height, payload, err)) {
                return "error: " + err;
            }
            return "ok:BLKDATA|" + payload;
        } catch (const std::exception&) {
            return "error: invalid block height";
        }
    }

    if (cmd == "getblockhash") {
        std::string id;
        iss >> id;
        if (id.empty() || !is_all_digits(id)) {
            return "error: usage getblockhash <height>";
        }
        try {
            const auto height = static_cast<std::uint64_t>(std::stoull(id));
            const auto found = chain_.block_at(height);
            if (!found.has_value()) {
                return "error: block not found";
            }
            return hash_block_header(found->header);
        } catch (const std::exception&) {
            return "error: invalid block height";
        }
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

    if (cmd == "token_sign_payload") {
        std::string symbol;
        std::string from;
        std::string to;
        std::uint64_t amount = 0;
        iss >> symbol >> from >> to >> amount;
        if (symbol.empty() || from.empty() || to.empty() || amount == 0) {
            return "error: usage token_sign_payload <symbol> <from> <to> <amount>";
        }
        return "token_transfer|" + symbol + "|" + from + "|" + to + "|" + std::to_string(amount);
    }

    if (cmd == "token_transfer_signed") {
        std::string symbol;
        std::string from;
        std::string to;
        std::uint64_t amount = 0;
        std::string pubkey;
        std::string sig;
        iss >> symbol >> from >> to >> amount >> pubkey >> sig;
        if (symbol.empty() || from.empty() || to.empty() || amount == 0 || pubkey.empty() || sig.empty()) {
            return "error: usage token_transfer_signed <symbol> <from> <to> <amount> <pubkey_hex> <sig_hex>";
        }
        const std::string payload = "token_transfer|" + symbol + "|" + from + "|" + to + "|" + std::to_string(amount);
        if (!verify_message_signature_hybrid(pubkey, payload, std::string("pq=") + sig)) {
            return "error: invalid token transfer signature";
        }
        if (derive_address_from_pubkey(pubkey) != from) {
            return "error: from address/pubkey mismatch";
        }
        std::string error;
        if (!tokens_.transfer(symbol, from, to, amount, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "token_transfer_wallet") {
        std::string name;
        std::string symbol;
        std::string to;
        std::uint64_t amount = 0;
        iss >> name >> symbol >> to >> amount;
        if (name.empty() || symbol.empty() || to.empty() || amount == 0) {
            return "error: usage token_transfer_wallet <wallet> <symbol> <to> <amount>";
        }
        StoredWallet stored{};
        std::string error;
        if (!wallets_.load(name, stored, error, true)) {
            return "error: " + error;
        }
        const std::string payload = "token_transfer|" + symbol + "|" + stored.address + "|" + to + "|" +
                                    std::to_string(amount);
        std::string sig;
        try {
            sig = sign_message_hybrid(stored.private_key, payload);
        } catch (const std::exception& e) {
            return std::string("error: token transfer sign failed: ") + e.what();
        }
        if (!verify_message_signature_hybrid(stored.public_key, payload, sig)) {
            return "error: token transfer signature verify failed";
        }
        if (!tokens_.transfer(symbol, stored.address, to, amount, error)) {
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
        iss >> token_a >> token_b;
        if (token_a.empty() || token_b.empty() || !parse_u64_token(iss, fee_bps) || !no_trailing_token(iss)) {
            return "error: usage swap_pool_create <token_a> <token_b> <fee_bps>";
        }
        std::string error;
        if (!tokens_.create_pool(token_a, token_b, fee_bps, error)) {
            return "error: " + error;
        }
        return "ok";
    }

    if (cmd == "swap_add_liquidity" || cmd == "add_liquidity") {
        std::string token_a;
        std::string token_b;
        std::string provider;
        std::uint64_t amount_a = 0;
        std::uint64_t amount_b = 0;
        iss >> token_a >> token_b >> provider;
        if (token_a.empty() || token_b.empty() || provider.empty() ||
            !parse_u64_token(iss, amount_a) || !parse_u64_token(iss, amount_b) ||
            amount_a == 0 || amount_b == 0 || !no_trailing_token(iss)) {
            return "error: usage add_liquidity <token_a> <token_b> <provider> <amount_a> <amount_b>";
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
        iss >> token_in >> token_out >> trader;
        if (token_in.empty() || token_out.empty() || trader.empty() ||
            !parse_u64_token(iss, amount_in) || !parse_u64_token(iss, min_out) ||
            amount_in == 0 || !no_trailing_token(iss)) {
            return "error: usage swap_exact_in <token_in> <token_out> <trader> <amount_in> <min_out>";
        }
        std::uint64_t amount_out = 0;
        std::string error;
        if (!tokens_.swap_exact_in(token_in, token_out, trader, amount_in, min_out, amount_out, error)) {
            return "error: " + error;
        }
        return std::string("ok:amount_out=") + std::to_string(amount_out);
    }

    if (cmd == "swap_exact_in_wallet") {
        std::string name;
        std::string token_in;
        std::string token_out;
        std::uint64_t amount_in = 0;
        std::uint64_t min_out = 0;
        iss >> name >> token_in >> token_out;
        if (name.empty() || token_in.empty() || token_out.empty() ||
            !parse_u64_token(iss, amount_in) || !parse_u64_token(iss, min_out) ||
            amount_in == 0 || !no_trailing_token(iss)) {
            return "error: usage swap_exact_in_wallet <wallet> <token_in> <token_out> <amount_in> <min_out>";
        }
        StoredWallet stored{};
        std::string error;
        if (!wallets_.load(name, stored, error, true)) {
            return "error: " + error;
        }
        const std::string payload = "swap_exact_in|" + token_in + "|" + token_out + "|" + stored.address + "|" +
                                    std::to_string(amount_in) + "|" + std::to_string(min_out);
        std::string sig;
        try {
            sig = sign_message_hybrid(stored.private_key, payload);
        } catch (const std::exception& e) {
            return std::string("error: swap sign failed: ") + e.what();
        }
        if (!verify_message_signature_hybrid(stored.public_key, payload, sig)) {
            return "error: swap signature verify failed";
        }
        std::uint64_t amount_out = 0;
        if (!tokens_.swap_exact_in(token_in, token_out, stored.address, amount_in, min_out, amount_out, error)) {
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

    if (cmd == "nft_info") {
        std::string collection;
        std::string token_id;
        iss >> collection >> token_id;
        if (collection.empty() || token_id.empty()) {
            return "error: usage nft_info <collection> <token_id>";
        }
        std::string out;
        std::string error;
        if (!tokens_.nft_info(collection, token_id, out, error)) {
            return "error: " + error;
        }
        return out;
    }

    if (cmd == "swap_tvl") {
        return "tvl=" + std::to_string(tokens_.swap_tvl());
    }

    if (cmd == "privacy_note_prepare") {
        std::uint64_t amount = 0;
        iss >> amount;
        if (amount == 0) {
            return "error: usage privacy_note_prepare <amount>";
        }
        OpeningNote note{};
        std::string error;
        if (!PrivacyPool::prepare_opening(amount, note, error)) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "ok:verifier=sha3_opening"
            << " amount=" << amount
            << " trapdoor=" << note.trapdoor
            << " commitment=" << note.commitment
            << " nullifier=" << note.nullifier
            << " claim=opening_not_zk";
        return out.str();
    }

    if (cmd == "privacy_mint_open") {
        std::string owner;
        std::uint64_t amount = 0;
        std::string commitment;
        std::string nullifier;
        std::string trapdoor;
        iss >> owner >> amount >> commitment >> nullifier >> trapdoor;
        if (owner.empty() || amount == 0 || commitment.empty() || nullifier.empty() || trapdoor.empty()) {
            return "error: usage privacy_mint_open <owner> <amount> <commitment_hex> <nullifier_hex> <trapdoor_hex>";
        }
        std::string error;
        const auto note_id = privacy_.mint_open(owner, amount, commitment, nullifier, trapdoor, error);
        if (!error.empty() || note_id.empty()) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "ok:note_id=" << note_id
            << " verifier=sha3_opening"
            << " commitment=" << commitment
            << " nullifier=" << nullifier
            << " claim=opening_not_zk";
        return out.str();
    }

    if (cmd == "privacy_spend_open") {
        std::string owner;
        std::string note_id;
        std::string recipient;
        std::uint64_t amount = 0;
        std::string trapdoor;
        iss >> owner >> note_id >> recipient >> amount >> trapdoor;
        if (owner.empty() || note_id.empty() || recipient.empty() || amount == 0 || trapdoor.empty()) {
            return "error: usage privacy_spend_open <owner> <note_id> <recipient> <amount> <trapdoor_hex>";
        }
        std::string new_note;
        OpeningNote recipient_opening{};
        std::string change_note;
        OpeningNote change_opening{};
        std::string error;
        if (!privacy_.spend_open(owner,
                                 note_id,
                                 recipient,
                                 amount,
                                 trapdoor,
                                 new_note,
                                 recipient_opening,
                                 change_note,
                                 change_opening,
                                 error)) {
            return "error: " + error;
        }
        std::ostringstream out;
        out << "ok:spent"
            << " verifier=sha3_opening"
            << " new_note_id=" << new_note
            << " new_commitment=" << recipient_opening.commitment
            << " new_nullifier=" << recipient_opening.nullifier
            << " new_trapdoor=" << recipient_opening.trapdoor
            << " claim=opening_not_zk";
        if (!change_note.empty()) {
            out << " change_note_id=" << change_note
                << " change_commitment=" << change_opening.commitment
                << " change_nullifier=" << change_opening.nullifier
                << " change_trapdoor=" << change_opening.trapdoor;
        }
        return out.str();
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
        return note_id + " claim=mldsa_wrap_not_zk";
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
        return new_note + " claim=mldsa_wrap_not_zk";
    }

    if (cmd == "privacy_status") {
        std::ostringstream out;
        out << "opening_verifier=sha3_opening"
            << " privacy_verifier=sha3_opening"
            << " privacy_mode=sha3_opening"
            << " privacy_ok=true"
            << " claim=opening_not_zk"
            << " legacy_mldsa_wrap=" << privacy_.native_verifier_mode()
            << " verifier_configured=" << (privacy_.verifier_configured() ? "true" : "false")
            << " native_verifier_mode=" << privacy_.native_verifier_mode()
            << " notes=" << privacy_.note_count()
            << " used_nullifiers=" << privacy_.used_nullifier_count()
            << " spent_commitments=" << privacy_.spent_commitment_count();
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
        return "ok:verdict=" + verdict + " check=first_nibble_parity";
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
