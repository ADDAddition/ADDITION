// Cross-node payment channels (RpcServer side). Protocol over P2P "CHAN|":
//   OPEN|tmp|b_wallet|a_endpoint|addr_a|pub_a|capacity|push|a_h0|a_h1 -> addr_b|pub_b|b_h0|b_h1
//   FUND|tmp|id|txid|vout|sig_a                                     -> sig_b
//   UPD|id|state|sig                                                -> sig
//   REV|id|revoked_n|secret|next_hash                               -> revoked_n|secret|next_hash
//   CLOSE|id                                                        -> coop close sig
// A receiver only accepts updates that do not lower its own balance (no HTLCs
// cross-node yet). Channel files never contain the wallet private key.
#include "addition/rpc_server.hpp"

#include "addition/crypto.hpp"
#include "addition/script.hpp"

#include <filesystem>
#include <fstream>
#include <sys/stat.h>

namespace addition {
namespace {

std::vector<std::string> split_bar(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, '|')) out.push_back(cur);
    return out;
}

bool safe_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

}  // namespace

std::string RpcServer::rchan_dir() const {
    const std::filesystem::path w(wallets_.dir());
    const auto parent = w.parent_path();
    return ((parent.empty() ? w : parent) / "rchannels").string();
}

bool RpcServer::load_wallet_keys(const std::string& name, WalletKeys& keys, std::string& error) {
    StoredWallet sw{};
    if (!wallets_.load(name, sw, error, true)) return false;
    keys = WalletKeys{sw.private_key, sw.public_key, sw.address, ""};
    return true;
}

bool RpcServer::rchan_save(const std::string& id, const RemoteChannel& rc, std::string& error) {
    std::error_code ec;
    const auto dir = rchan_dir();
    std::filesystem::create_directories(dir, ec);
    ::chmod(dir.c_str(), 0700);
    const auto path = (std::filesystem::path(dir) / (id + ".chan")).string();
    const auto tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        ::chmod(tmp.c_str(), 0600);
        out << "wallet " << rc.wallet << "\npeer " << rc.peer << "\nstatus " << rc.status << "\n---\n"
            << rc.ch->serialize();
        if (!out) { error = "cannot persist channel"; return false; }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { error = "cannot persist channel"; return false; }
    return true;
}

void RpcServer::rchan_load() {
    if (rchan_loaded_) return;
    rchan_loaded_ = true;
    std::error_code ec;
    for (const auto& ent : std::filesystem::directory_iterator(rchan_dir(), ec)) {
        if (ent.path().extension() != ".chan") continue;
        const auto id = ent.path().stem().string();
        if (!safe_id(id)) continue;
        std::ifstream in(ent.path());
        std::string line, wallet, peer, status = "open", body;
        while (std::getline(in, line) && line != "---") {
            if (line.rfind("wallet ", 0) == 0) wallet = line.substr(7);
            else if (line.rfind("peer ", 0) == 0) peer = line.substr(5);
            else if (line.rfind("status ", 0) == 0) status = line.substr(7);
        }
        body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        WalletKeys k;
        std::string err;
        if (!load_wallet_keys(wallet, k, err)) continue;
        auto ch = channel::Channel::deserialize(body, k, err);
        if (!ch) continue;
        RemoteChannel rc;
        rc.wallet = wallet;
        rc.peer = peer;
        rc.status = status;
        rc.ch = std::move(ch);
        rchannels_[id] = std::move(rc);
    }
}

bool RpcServer::handle_channel_message(const std::string& /*peer*/, const std::string& body,
                                       std::string& reply, std::string& error) {
    rchan_load();
    const auto f = split_bar(body);
    if (f.empty()) { error = "bad channel message"; return false; }
    const auto& op = f[0];
    if (op == "OPEN") {
        if (f.size() != 10 || !safe_id(f[1])) { error = "bad OPEN"; return false; }
        if (rchan_opening_.size() > 64) rchan_opening_.clear();
        WalletKeys kb;
        if (!load_wallet_keys(f[2], kb, error)) { error = "unknown wallet"; return false; }
        std::uint64_t cap = 0, push = 0;
        try { cap = std::stoull(f[6]); push = std::stoull(f[7]); } catch (...) { error = "bad OPEN"; return false; }
        if (cap < 2 || push > cap || cap - push < 1) { error = "bad capacity"; return false; }
        channel::Info info;
        info.addr_a = f[4];
        info.addr_b = kb.address;
        info.capacity = cap;
        std::string seed;
        if (!random_hex(32, seed, error)) return false;
        RemoteChannel rc;
        rc.wallet = f[2];
        rc.peer = f[3];
        rc.ch = std::make_unique<channel::Channel>(kb, f[5], false, info, seed);
        rc.ch->set_remote_initial_hashes(f[8], f[9]);
        const auto [h0, h1] = rc.ch->initial_hashes();
        reply = "CHANOK|" + kb.address + "|" + kb.public_key + "|" + h0 + "|" + h1 + "|" + std::to_string(push);
        rchan_opening_[f[1]] = std::move(rc);
        return true;
    }
    if (op == "FUND") {
        if (f.size() != 6) { error = "bad FUND"; return false; }
        auto it = rchan_opening_.find(f[1]);
        if (it == rchan_opening_.end() || !safe_id(f[2]) || rchannels_.count(f[2])) { error = "unknown channel open"; return false; }
        auto rc = std::move(it->second);
        rchan_opening_.erase(it);
        std::uint32_t vout = 0;
        try { vout = static_cast<std::uint32_t>(std::stoul(f[4])); } catch (...) { error = "bad FUND"; return false; }
        if (f[2] != f[3].substr(0, 16)) { error = "channel id mismatch"; return false; }
        rc.ch->set_funding(f[3], vout);
        // s0 is reconstructed from the OPEN terms (push to B).
        channel::State s0;
        s0.bal_b = 0;
        // push amount is implied by capacity split proposed by A; B accepts any push >= 0.
        // We recover it from the signature check: try the state A signed.
        // A sends it explicitly in its sig field as "<state>#<sig>".
        const auto hash_pos = f[5].find('#');
        if (hash_pos == std::string::npos || !channel::decode_state(f[5].substr(0, hash_pos), s0) || s0.n != 0 ||
            !s0.htlcs.empty() || s0.bal_a + s0.bal_b != rc.ch->info().capacity) {
            error = "bad initial state";
            return false;
        }
        if (!rc.ch->receive_commit_sig(s0, {0, f[5].substr(hash_pos + 1)}, error)) return false;
        reply = "CHANOK|" + rc.ch->sign_remote_commitment(s0).sig;
        if (!rchan_save(f[2], rc, error)) return false;
        rchannels_[f[2]] = std::move(rc);
        return true;
    }
    if (f.size() < 2) { error = "bad channel message"; return false; }
    auto it = rchannels_.find(f[1]);
    if (it == rchannels_.end()) { error = "unknown channel"; return false; }
    auto& rc = it->second;
    if (rc.busy) { error = "channel busy"; return false; }
    if (rc.status != "open") { error = "channel not open"; return false; }
    auto& ch = *rc.ch;
    if (op == "UPD") {
        channel::State next;
        if (f.size() != 4 || !channel::decode_state(f[2], next)) { error = "bad UPD"; return false; }
        const auto mine_now = ch.local_balance();
        const auto mine_next = ch.is_a() ? next.bal_a : next.bal_b;
        if (!next.htlcs.empty() || mine_next < mine_now) { error = "update would lower local balance"; return false; }
        if (!ch.receive_commit_sig(next, {next.n, f[3]}, error)) return false;
        reply = "CHANOK|" + ch.sign_remote_commitment(next).sig;
        return rchan_save(f[1], rc, error);
    }
    if (op == "REV") {
        if (f.size() != 5 || !ch.has_pending()) { error = "bad REV"; return false; }
        channel::Revoke r;
        try { r.revoked_n = std::stoull(f[2]); } catch (...) { error = "bad REV"; return false; }
        r.secret = f[3];
        r.next_hash = f[4];
        if (!ch.receive_revoke(r, error)) return false;
        const auto mine = ch.revoke_and_advance();
        reply = "CHANOK|" + std::to_string(mine.revoked_n) + "|" + mine.secret + "|" + mine.next_hash;
        return rchan_save(f[1], rc, error);
    }
    if (op == "CLOSE") {
        if (!ch.state().htlcs.empty() || ch.has_pending()) { error = "cannot close now"; return false; }
        reply = "CHANOK|" + ch.sign_coop_close();
        rc.status = "closing_coop";
        return rchan_save(f[1], rc, error);
    }
    error = "unknown channel op";
    return false;
}

std::string RpcServer::handle_rchan_command(const std::string& cmd, std::istringstream& iss) {
    rchan_load();
    std::string error;
    auto ok_body = [](const std::string& resp, std::string& out) {
        if (resp.rfind("CHANOK|", 0) != 0) return false;
        out = resp.substr(7);
        return true;
    };
    if (cmd == "rchan_open") {
        // rchan_open <funder_wallet> <peer_p2p_endpoint> <peer_wallet> <capacity> [push]
        std::string wa, peer, wb;
        std::uint64_t cap = 0, push = 0;
        iss >> wa >> peer >> wb >> cap >> push;
        if (wa.empty() || peer.empty() || wb.empty() || cap < 2 || push > cap - 1)
            return "error: usage rchan_open <funder_wallet> <peer_p2p_endpoint> <peer_wallet> <capacity> [push]";
        if (advertised_p2p_.empty()) return "error: node has no advertised p2p endpoint";
        WalletKeys ka;
        if (!load_wallet_keys(wa, ka, error)) return "error: " + error;
        if (unlocked_balance(ka.address) < cap + 1) return "error: insufficient unlocked balance";
        std::string tmp, seed;
        if (!random_hex(8, tmp, error) || !random_hex(32, seed, error)) return "error: " + error;
        channel::Info info;
        info.addr_a = ka.address;
        info.capacity = cap;
        auto probe = std::make_unique<channel::Channel>(ka, "", true, info, seed);
        const auto [a0, a1] = probe->initial_hashes();
        std::string resp, body;
        if (!node_.request_peer(peer, "CHAN|OPEN|" + tmp + "|" + wb + "|" + advertised_p2p_ + "|" + ka.address + "|" +
                                          ka.public_key + "|" + std::to_string(cap) + "|" + std::to_string(push) + "|" +
                                          a0 + "|" + a1, resp, error))
            return "error: peer: " + error;
        if (!ok_body(resp, body)) return "error: bad peer reply";
        const auto r = split_bar(body);
        if (r.size() != 5) return "error: bad peer reply";
        info.addr_b = r[0];
        RemoteChannel rc;
        rc.wallet = wa;
        rc.peer = peer;
        rc.ch = std::make_unique<channel::Channel>(ka, r[1], true, info, seed);
        rc.ch->set_remote_initial_hashes(r[2], r[3]);
        const auto fs = script::script_address(channel::funding_script(info.addr_a, info.addr_b));
        Transaction f;
        if (!chain_.build_transaction(ka.address, fs, cap, 1, chain_.next_nonce(ka.address), f, error)) return "error: " + error;
        f.signer = ka.address;
        f.signer_pubkey = ka.public_key;
        f.signature = sign_message_hybrid(ka.private_key, hash_transaction(f));
        std::uint32_t vout = 0;
        while (vout < f.outputs.size() && f.outputs[vout].recipient != fs) ++vout;
        const auto txid = hash_transaction(f);
        const auto id = txid.substr(0, 16);
        rc.ch->set_funding(txid, vout);
        channel::State s0;
        s0.bal_a = cap - push;
        s0.bal_b = push;
        const auto sig_a = rc.ch->sign_remote_commitment(s0).sig;
        if (!node_.request_peer(peer, "CHAN|FUND|" + tmp + "|" + id + "|" + txid + "|" + std::to_string(vout) + "|" +
                                          channel::encode_state(s0) + "#" + sig_a, resp, error))
            return "error: peer: " + error;
        if (!ok_body(resp, body)) return "error: bad peer reply";
        if (!rc.ch->receive_commit_sig(s0, {0, body}, error)) return "error: " + error;
        // Both commitments are signed: only now broadcast the funding tx.
        if (!rchan_save(id, rc, error)) return "error: " + error;
        if (!node_.submit_transaction(f, error)) return "error: funding submit: " + error;
        rchannels_[id] = std::move(rc);
        return "ok channel=" + id + " funding_txid=" + txid + " capacity=" + std::to_string(cap) + " peer=" + peer;
    }
    if (cmd == "rchan_list") {
        std::ostringstream out;
        out << "rchannels=" << rchannels_.size();
        for (const auto& [id, rc] : rchannels_) {
            const auto& st = rc.ch->state();
            out << "\n" << id << " status=" << rc.status << " wallet=" << rc.wallet << " side=" << (rc.ch->is_a() ? "a" : "b")
                << " peer=" << rc.peer << " capacity=" << rc.ch->info().capacity << " state=" << st.n
                << " bal_a=" << st.bal_a << " bal_b=" << st.bal_b << " local=" << rc.ch->local_balance();
        }
        return out.str();
    }
    std::string id;
    iss >> id;
    auto it = rchannels_.find(id);
    if (it == rchannels_.end()) return "error: unknown channel";
    auto& rc = it->second;
    if (rc.status != "open") return "error: channel not open";
    if (rc.busy) return "error: channel busy";
    auto& ch = *rc.ch;
    if (cmd == "rchan_pay") {
        std::uint64_t amount = 0;
        iss >> amount;
        channel::State next = ch.state();
        ++next.n;
        auto& mine = ch.is_a() ? next.bal_a : next.bal_b;
        auto& theirs = ch.is_a() ? next.bal_b : next.bal_a;
        const std::uint64_t reserve = ch.is_a() ? ch.info().fee : 0;
        if (amount == 0 || mine < amount + reserve) return "error: insufficient channel balance";
        mine -= amount;
        theirs += amount;
        if (!ch.validate_next(next, error)) return "error: " + error;
        rc.busy = true;
        struct Clear { bool& b; ~Clear() { b = false; } } clear{rc.busy};
        std::string resp, body;
        const auto sig = ch.sign_remote_commitment(next).sig;
        if (!node_.request_peer(rc.peer, "CHAN|UPD|" + id + "|" + channel::encode_state(next) + "|" + sig, resp, error))
            return "error: peer: " + error;
        if (!ok_body(resp, body)) return "error: bad peer reply";
        if (!ch.receive_commit_sig(next, {next.n, body}, error)) return "error: " + error;
        const auto rev = ch.revoke_and_advance();
        if (!rchan_save(id, rc, error)) return "error: " + error;
        if (!node_.request_peer(rc.peer, "CHAN|REV|" + id + "|" + std::to_string(rev.revoked_n) + "|" + rev.secret + "|" +
                                             rev.next_hash, resp, error))
            return "error: peer revoke: " + error;
        if (!ok_body(resp, body)) return "error: bad peer reply";
        const auto r = split_bar(body);
        channel::Revoke theirs_rev;
        try { theirs_rev.revoked_n = std::stoull(r.at(0)); theirs_rev.secret = r.at(1); theirs_rev.next_hash = r.at(2); }
        catch (...) { return "error: bad peer revoke"; }
        if (!ch.receive_revoke(theirs_rev, error)) return "error: " + error;
        if (!rchan_save(id, rc, error)) return "error: " + error;
        return "ok channel=" + id + " state=" + std::to_string(ch.state().n) + " bal_a=" +
               std::to_string(ch.state().bal_a) + " bal_b=" + std::to_string(ch.state().bal_b);
    }
    if (cmd == "rchan_close") {
        std::string mode;
        iss >> mode;
        Transaction tx;
        if (mode == "force") {
            tx = ch.signed_local_commitment();
        } else {
            std::string resp, body;
            if (!node_.request_peer(rc.peer, "CHAN|CLOSE|" + id, resp, error)) return "error: peer: " + error;
            if (!ok_body(resp, body)) return "error: bad peer reply";
            if (!ch.assemble_coop_close(body, tx, error)) return "error: " + error;
        }
        if (!node_.submit_transaction(tx, error)) return "error: " + error;
        rc.status = mode == "force" ? "closing_force" : "closing_coop";
        rchan_save(id, rc, error);
        return "ok close_txid=" + hash_transaction(tx);
    }
    return "error: unknown rchan command";
}

}  // namespace addition
