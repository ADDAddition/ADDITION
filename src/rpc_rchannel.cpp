// Cross-node payment channels (RpcServer side). Protocol over P2P "CHAN|":
//   OPEN|tmp|b_wallet|a_endpoint|addr_a|pub_a|capacity|push|a_h0|a_h1 -> addr_b|pub_b|b_h0|b_h1
//   FUND|tmp|id|txid|vout|sig_a                                     -> sig_b
//   UPD|id|state|sig[|kind|extra]                                   -> sig
//       kind PAY | ADD (extra = route,fee,delta) | SETTLE (extra = preimage) | FAIL
//   REV|id|revoked_n|secret|next_hash                               -> revoked_n|secret|next_hash
//   ABORT|id|n      drop the receiver's pending (never revoked) state n
//   SYNC|id         -> cur_n|out_phase|pending_n   (recovery probe)
//   CLOSE|id                                                        -> coop close sig
// The receiver classifies every update (channel::classify_peer_update): plain
// payments may not lower its balance; the proposer may add an HTLC it funds;
// only the HTLC receiver may remove it (settle needs the preimage).
// Multi-hop: the ADD extra carries the remaining channel route (source-routed,
// cleartext to each hop). Forwarding/settling runs from rchan_tick, never from
// inside an inbound handler (avoids cross-node lock waits).
// REV-round failure: the proposer persists its outbox (phase upd/rev) and
// rchan_tick resumes it with backoff; UPD and REV are idempotent on the
// receiver; a never-revoked pending state can be rolled back (ABORT) and is
// kept for penalty matching (revocation is per state number). Channel files
// never contain the wallet private key.
#include "addition/rpc_server.hpp"

#include "addition/crypto.hpp"
#include "addition/script.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

namespace addition {
namespace {

std::vector<std::string> split_bar(const std::string& s, char d = '|') {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, d)) out.push_back(cur);
    return out;
}

bool safe_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool safe_token(const std::string& t) {
    if (t.empty() || t.size() > 4096) return false;
    for (char c : t)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == ',' || c == '+' || c == '-' || c == '_' || c == '.'))
            return false;
    return true;
}

std::int64_t now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::uint64_t& side_bal(channel::State& s, bool a) { return a ? s.bal_a : s.bal_b; }

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
        out << "wallet " << rc.wallet << "\npeer " << rc.peer << "\nstatus " << rc.status << "\n";
        if (!rc.out_phase.empty())
            out << "out " << rc.out_phase << " " << rc.out_kind << " " << rc.out_state << " "
                << (rc.out_extra.empty() ? "-" : rc.out_extra) << "\nout_action " << rc.out_action << "\n";
        if (!rc.in_effect.empty()) out << "in_effect " << rc.in_effect << "\n";
        out << "---\n" << rc.ch->serialize();
        if (!out) { error = "cannot persist channel"; return false; }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { error = "cannot persist channel"; return false; }
    return true;
}

void RpcServer::rchan_save_meta() {
    std::error_code ec;
    const auto dir = rchan_dir();
    std::filesystem::create_directories(dir, ec);
    ::chmod(dir.c_str(), 0700);
    const auto path = (std::filesystem::path(dir) / "meta").string();
    const auto tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        ::chmod(tmp.c_str(), 0600);
        for (auto& [k, v] : rchan_invoices_) out << "inv " << k << " " << v << "\n";
        for (auto& [k, v] : rchan_links_) out << "link " << k << " " << v << "\n";
        for (auto& [k, v] : rchan_payments_) out << "pay " << k << " " << v << "\n";
        for (auto& a : rchan_actions_) out << "act " << a << "\n";
    }
    std::filesystem::rename(tmp, path, ec);
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
        RemoteChannel rc;
        while (std::getline(in, line) && line != "---") {
            if (line.rfind("wallet ", 0) == 0) wallet = line.substr(7);
            else if (line.rfind("peer ", 0) == 0) peer = line.substr(5);
            else if (line.rfind("status ", 0) == 0) status = line.substr(7);
            else if (line.rfind("out_action ", 0) == 0) rc.out_action = line.substr(11);
            else if (line.rfind("out ", 0) == 0) {
                std::istringstream ls(line.substr(4));
                ls >> rc.out_phase >> rc.out_kind >> rc.out_state >> rc.out_extra;
                if (rc.out_extra == "-") rc.out_extra.clear();
            } else if (line.rfind("in_effect ", 0) == 0) rc.in_effect = line.substr(10);
        }
        body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        WalletKeys k;
        std::string err;
        if (!load_wallet_keys(wallet, k, err)) continue;
        auto ch = channel::Channel::deserialize(body, k, err);
        if (!ch) continue;
        rc.wallet = wallet;
        rc.peer = peer;
        rc.status = status;
        rc.ch = std::move(ch);
        if (rc.ch->has_pending()) rc.pending_since = now_s();
        rchannels_[id] = std::move(rc);
    }
    std::ifstream meta(std::filesystem::path(rchan_dir()) / "meta");
    for (std::string line; std::getline(meta, line);) {
        const auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const auto k = line.substr(0, sp), rest = line.substr(sp + 1);
        if (k == "act") { rchan_actions_.push_back(rest); continue; }
        const auto sp2 = rest.find(' ');
        if (sp2 == std::string::npos) continue;
        const auto key = rest.substr(0, sp2), val = rest.substr(sp2 + 1);
        if (k == "inv") rchan_invoices_[key] = val;
        else if (k == "link") rchan_links_[key] = val;
        else if (k == "pay") rchan_payments_[key] = val;
    }
}

// Drop our in-flight proposal (never revoked by us). The peer may hold our
// signature on it: remember it for penalty matching, and re-queue the action.
void RpcServer::rchan_abandon_out(RemoteChannel& rc, const std::string& why) {
    if (rc.out_phase != "upd") return;
    channel::State x;
    if (channel::decode_state(rc.out_state, x)) rc.ch->note_signed_abandoned(x);
    if (!rc.out_action.empty() && rc.out_action != "-") rchan_actions_.push_back(rc.out_action);
    else if (rc.out_kind == "ADD") {
        const auto ph = x.htlcs.empty() ? std::string() : x.htlcs.back().payment_hash;
        if (!ph.empty() && rchan_payments_.count(ph)) rchan_payments_[ph] = "failed(" + why + ")";
    }
    rc.out_phase.clear();
    rc.out_state.clear();
    rc.out_kind.clear();
    rc.out_extra.clear();
    rc.out_action.clear();
    rc.attempts = 0;
    rchan_save_meta();
}

// Effects of a peer update once it is revoked-in on our side.
void RpcServer::rchan_apply_effect(const std::string& id, RemoteChannel& rc) {
    const auto eff = split_bar(rc.in_effect, ' ');
    rc.in_effect.clear();
    if (eff.size() < 3) return;
    const auto& kind = eff[0];
    std::uint64_t hid = 0;
    try { hid = std::stoull(eff[1]); } catch (...) { return; }
    const std::string key = id + ":" + std::to_string(hid);
    if (kind == "ADD") {
        const channel::Htlc* h = nullptr;
        for (auto& x : rc.ch->state().htlcs) if (x.id == hid) h = &x;
        if (!h) return;
        const auto ex = split_bar(eff[2], ',');  // route,fee,delta
        if (ex.size() != 3 || ex[0] == "-") {
            auto inv = rchan_invoices_.find(h->payment_hash);
            bool ok = false;
            if (inv != rchan_invoices_.end()) {
                const auto iv = split_bar(inv->second, ' ');  // wallet amount preimage status
                std::uint64_t amt = 0;
                try { amt = std::stoull(iv.at(1)); } catch (...) {}
                if (iv.size() >= 3 && iv[0] == rc.wallet && h->amount >= amt) {
                    rchan_actions_.push_back("SETTLE " + id + " " + std::to_string(hid) + " " + iv[2]);
                    inv->second = iv[0] + " " + iv[1] + " " + iv[2] + " paid";
                    ok = true;
                }
            }
            if (!ok) rchan_actions_.push_back("FAIL " + id + " " + std::to_string(hid));
        } else {
            const auto route = split_bar(ex[0], '+');
            std::uint64_t fee = 0, delta = 0;
            try { fee = std::stoull(ex[1]); delta = std::stoull(ex[2]); } catch (...) {}
            std::string rest;
            for (std::size_t i = 1; i < route.size(); ++i) rest += (i > 1 ? "+" : "") + route[i];
            if (rest.empty()) rest = "-";
            if (route.empty() || !rchannels_.count(route[0]) || h->amount <= fee || delta == 0 || h->expiry <= delta) {
                rchan_actions_.push_back("FAIL " + id + " " + std::to_string(hid));
            } else {
                rchan_actions_.push_back("FWD " + route[0] + " " + key + " " + std::to_string(h->amount - fee) + " " +
                                         h->payment_hash + " " + std::to_string(h->expiry - delta) + " " + rest + "," +
                                         ex[1] + "," + ex[2]);
            }
        }
    } else if (kind == "SETTLE" || kind == "FAIL") {
        auto l = rchan_links_.find(key);
        if (l != rchan_links_.end()) {
            const auto in = split_bar(l->second, ':');
            if (in.size() == 2)
                rchan_actions_.push_back(kind + " " + in[0] + " " + in[1] + (kind == "SETTLE" ? " " + eff[2] : ""));
            rchan_links_.erase(l);
        } else {
            const auto ph = kind == "SETTLE" ? script::hash_preimage_hex(eff[2]) : eff[2];
            if (rchan_payments_.count(ph)) rchan_payments_[ph] = kind == "SETTLE" ? "settled" : "failed";
        }
    }
    rchan_save_meta();
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
    const auto& id = f[1];
    if (rc.busy) { error = "channel busy"; return false; }
    if (op == "SYNC") {
        reply = "CHANOK|" + std::to_string(rc.ch->state().n) + "|" + (rc.out_phase.empty() ? "-" : rc.out_phase) + "|" +
                (rc.ch->has_pending() ? std::to_string(rc.ch->pending_state()->n) : "-");
        return true;
    }
    if (rc.status != "open") { error = "channel not open"; return false; }
    auto& ch = *rc.ch;
    if (op == "UPD") {
        channel::State next;
        if ((f.size() != 4 && f.size() != 6) || !channel::decode_state(f[2], next)) { error = "bad UPD"; return false; }
        const std::string kind = f.size() == 6 ? f[4] : "PAY";
        const std::string extra = f.size() == 6 ? f[5] : "-";
        if (ch.has_pending()) {
            if (channel::encode_state(*ch.pending_state()) == f[2]) {  // idempotent re-send
                reply = "CHANOK|" + ch.sign_remote_commitment(next).sig;
                return true;
            }
            // Conflicting proposals for the same number: side A's proposal wins.
            if (ch.is_a()) { error = "conflicting pending update"; return false; }
            ch.drop_pending();
            rc.in_effect.clear();
        }
        if (rc.out_phase == "rev") { error = "conflicting update: awaiting revocation"; return false; }
        if (rc.out_phase == "upd") {
            if (ch.is_a()) { error = "conflicting pending update"; return false; }
            rchan_abandon_out(rc, "conflict");
        }
        const auto cls = channel::classify_peer_update(ch, next, kind == "SETTLE" ? extra : std::string(), chain_.height());
        if (cls.kind == channel::PeerUpdate::Reject) { error = cls.err; return false; }
        if (cls.kind == channel::PeerUpdate::AddHtlc && !safe_token(extra)) { error = "bad route"; return false; }
        if (!ch.receive_commit_sig(next, {next.n, f[3]}, error)) return false;
        const auto hid = std::to_string(cls.htlc.id);
        switch (cls.kind) {
            case channel::PeerUpdate::AddHtlc: rc.in_effect = "ADD " + hid + " " + extra; break;
            case channel::PeerUpdate::SettleHtlc: rc.in_effect = "SETTLE " + hid + " " + extra; break;
            case channel::PeerUpdate::FailHtlc: rc.in_effect = "FAIL " + hid + " " + cls.htlc.payment_hash; break;
            default: rc.in_effect.clear();
        }
        rc.pending_since = now_s();
        reply = "CHANOK|" + ch.sign_remote_commitment(next).sig;
        return rchan_save(id, rc, error);
    }
    if (op == "REV") {
        if (f.size() != 5) { error = "bad REV"; return false; }
        channel::Revoke r;
        try { r.revoked_n = std::stoull(f[2]); } catch (...) { error = "bad REV"; return false; }
        r.secret = f[3];
        r.next_hash = f[4];
        if (!ch.has_pending()) {
            // Our reply to an earlier REV was lost: answer again (deterministic).
            if (r.revoked_n + 1 == ch.state().n && ch.has_remote_secret(r.revoked_n, r.secret)) {
                const auto mine = ch.last_revoke();
                reply = "CHANOK|" + std::to_string(mine.revoked_n) + "|" + mine.secret + "|" + mine.next_hash;
                return true;
            }
            error = "no pending update";
            return false;
        }
        if (rchan_fault_ == "drop_rev") { rchan_fault_.clear(); error = "fault-injected: REV dropped"; return false; }
        if (!ch.receive_revoke(r, error)) return false;
        const auto mine = ch.revoke_and_advance();
        rc.pending_since = 0;
        rchan_apply_effect(id, rc);
        if (!rchan_save(id, rc, error)) return false;
        if (rchan_fault_ == "drop_rev_reply") { rchan_fault_.clear(); error = "fault-injected: REV reply dropped"; return false; }
        reply = "CHANOK|" + std::to_string(mine.revoked_n) + "|" + mine.secret + "|" + mine.next_hash;
        return true;
    }
    if (op == "ABORT") {
        std::uint64_t n = 0;
        try { n = std::stoull(f.at(2)); } catch (...) { error = "bad ABORT"; return false; }
        if (ch.has_pending() && ch.pending_state()->n == n) {
            ch.drop_pending();
            rc.in_effect.clear();
            rc.pending_since = 0;
            if (!rchan_save(id, rc, error)) return false;
        }
        reply = "CHANOK|" + std::to_string(ch.state().n);
        return true;
    }
    if (op == "CLOSE") {
        if (!ch.state().htlcs.empty() || ch.has_pending() || !rc.out_phase.empty()) { error = "cannot close now"; return false; }
        reply = "CHANOK|" + ch.sign_coop_close();
        rc.status = "closing_coop";
        return rchan_save(id, rc, error);
    }
    error = "unknown channel op";
    return false;
}

bool RpcServer::rchan_propose(const std::string& id, const channel::State& next, const std::string& kind,
                              const std::string& extra, const std::string& action, std::string& error) {
    auto& rc = rchannels_.at(id);
    if (!rc.out_phase.empty()) { error = "update in flight (rchan_recover)"; return false; }
    if (!rc.ch->validate_next(next, error)) return false;
    rc.out_phase = "upd";
    rc.out_state = channel::encode_state(next);
    rc.out_kind = kind;
    rc.out_extra = extra;
    rc.out_action = action.empty() ? "-" : action;
    rc.attempts = 0;
    rc.next_try = 0;
    if (!rchan_save(id, rc, error)) { rc.out_phase.clear(); return false; }
    return rchan_drive(id, error);
}

// Advance our outbox as far as possible. False = not finished (retry later).
bool RpcServer::rchan_drive(const std::string& id, std::string& error) {
    auto& rc = rchannels_.at(id);
    auto& ch = *rc.ch;
    if (rc.busy) { error = "channel busy"; return false; }
    rc.busy = true;
    struct Clear { bool& b; ~Clear() { b = false; } } clear{rc.busy};
    auto ok_body = [](const std::string& resp, std::string& out) {
        if (resp.rfind("CHANOK|", 0) != 0) return false;
        out = resp.substr(7);
        return true;
    };
    std::string resp, body;
    channel::State x;
    if (!channel::decode_state(rc.out_state, x)) { rc.out_phase.clear(); error = "corrupt outbox"; return false; }
    auto fail = [&](const std::string& e) {
        error = e;
        ++rc.attempts;
        rc.next_try = now_s() + std::min<std::int64_t>(60, 1LL << std::min(rc.attempts, 6u));
        std::string e2;
        rchan_save(id, rc, e2);
        return false;
    };
    if (rc.out_phase == "upd") {
        if (x.n != ch.state().n + 1) {  // channel moved on (peer's update won)
            rchan_abandon_out(rc, "superseded");
            rchan_save(id, rc, error);
            error = "proposal superseded";
            return false;
        }
        if (ch.has_pending() && channel::encode_state(*ch.pending_state()) != rc.out_state) {
            if (!ch.is_a()) {  // peer (A) proposal wins
                rchan_abandon_out(rc, "conflict");
                rchan_save(id, rc, error);
                error = "conflict: peer update wins";
                return false;
            }
            ch.drop_pending();
            rc.in_effect.clear();
        }
        if (!ch.has_pending()) {
            const auto sig = ch.sign_remote_commitment(x).sig;
            const std::string extra = rc.out_extra.empty() ? "-" : rc.out_extra;
            if (!node_.request_peer(rc.peer, "CHAN|UPD|" + id + "|" + rc.out_state + "|" + sig + "|" + rc.out_kind + "|" + extra,
                                    resp, error)) {
                if (error.find("conflicting pending update") != std::string::npos) {
                    // Peer is A with its own proposal pending, or holds a stale one.
                    if (ch.is_a()) node_.request_peer(rc.peer, "CHAN|ABORT|" + id + "|" + std::to_string(x.n), resp, error);
                    else { rchan_abandon_out(rc, "conflict"); rchan_save(id, rc, error); return false; }
                }
                return fail("peer: " + error);
            }
            if (!ok_body(resp, body)) return fail("bad peer reply");
            if (!ch.receive_commit_sig(x, {x.n, body}, error)) return fail(error);
        }
        ch.revoke_and_advance();
        rc.out_phase = "rev";
        if (!rchan_save(id, rc, error)) return false;
    }
    if (rc.out_phase == "rev") {
        const auto rev = ch.last_revoke();
        const std::string msg = "CHAN|REV|" + id + "|" + std::to_string(rev.revoked_n) + "|" + rev.secret + "|" + rev.next_hash;
        if (!node_.request_peer(rc.peer, msg, resp, error)) {
            if (error.find("no pending update") == std::string::npos) return fail("peer revoke: " + error);
            // Peer lost the UPD: re-send it for our (already current) state, then REV.
            const auto sig = ch.sign_remote_commitment(ch.state()).sig;
            const std::string extra = rc.out_extra.empty() ? "-" : rc.out_extra;
            if (!node_.request_peer(rc.peer, "CHAN|UPD|" + id + "|" + channel::encode_state(ch.state()) + "|" + sig + "|" +
                                                 rc.out_kind + "|" + extra, resp, error))
                return fail("peer re-UPD: " + error);
            if (!node_.request_peer(rc.peer, msg, resp, error)) return fail("peer revoke: " + error);
        }
        if (!ok_body(resp, body)) return fail("bad peer reply");
        const auto r = split_bar(body);
        channel::Revoke theirs;
        try { theirs.revoked_n = std::stoull(r.at(0)); theirs.secret = r.at(1); theirs.next_hash = r.at(2); }
        catch (...) { return fail("bad peer revoke"); }
        if (!ch.receive_revoke(theirs, error)) return fail(error);
        rc.out_phase.clear();
        rc.out_state.clear();
        rc.out_kind.clear();
        rc.out_extra.clear();
        rc.out_action.clear();
        rc.attempts = 0;
        return rchan_save(id, rc, error);
    }
    return true;
}

// One queued action. keep=true leaves it queued (channel busy).
bool RpcServer::rchan_run_action(const std::string& action, bool& keep) {
    keep = false;
    const auto a = split_bar(action, ' ');
    if (a.size() < 3) return false;
    const auto& kind = a[0];
    auto it = rchannels_.find(a[1]);
    if (it == rchannels_.end() || it->second.status != "open") {
        if (kind == "FWD") rchan_actions_.push_back("FAIL " + split_bar(a[2], ':').at(0) + " " + split_bar(a[2], ':').back());
        return false;
    }
    auto& rc = it->second;
    auto& ch = *rc.ch;
    if (!rc.out_phase.empty() || ch.has_pending() || rc.busy || ch.awaiting_remote_revoke()) { keep = true; return false; }
    std::string error;
    channel::State n = ch.state();
    ++n.n;
    if (kind == "FWD" && a.size() == 7) {
        std::uint64_t amt = 0, exp = 0;
        try { amt = std::stoull(a[3]); exp = std::stoull(a[5]); } catch (...) { return false; }
        auto& mine = side_bal(n, ch.is_a());
        const std::uint64_t reserve = ch.is_a() ? ch.info().fee : 0;
        const auto in = split_bar(a[2], ':');
        if (amt == 0 || mine < amt + reserve || exp <= chain_.height() + 1) {
            rchan_actions_.push_back("FAIL " + in.at(0) + " " + in.at(1));
            return false;
        }
        mine -= amt;
        const auto hid = n.next_htlc_id++;
        n.htlcs.push_back(channel::Htlc{hid, ch.is_a(), amt, a[4], exp});
        rchan_links_[a[1] + ":" + std::to_string(hid)] = a[2];
        rchan_save_meta();
        if (!rchan_propose(a[1], n, "ADD", a[6], action, error) && rc.out_phase.empty()) {
            rchan_links_.erase(a[1] + ":" + std::to_string(hid));
            rchan_actions_.push_back("FAIL " + in.at(0) + " " + in.at(1));
        }
        return true;
    }
    if (kind == "SETTLE" || kind == "FAIL") {
        std::uint64_t hid = 0;
        try { hid = std::stoull(a[2]); } catch (...) { return false; }
        auto h = std::find_if(n.htlcs.begin(), n.htlcs.end(), [&](const channel::Htlc& x) { return x.id == hid; });
        if (h == n.htlcs.end() || h->offered_by_a == ch.is_a()) return false;  // must be offered by the peer
        if (kind == "SETTLE" && (a.size() != 4 || script::hash_preimage_hex(a[3]) != h->payment_hash)) return false;
        side_bal(n, kind == "SETTLE" ? ch.is_a() : !ch.is_a()) += h->amount;
        n.htlcs.erase(h);
        rchan_propose(a[1], n, kind, kind == "SETTLE" ? a[3] : "-", action, error);
        return true;
    }
    return false;
}

void RpcServer::rchan_tick() {
    rchan_load();
    const auto now = now_s();
    std::string error;
    // 1) resume interrupted proposals (REV/UPD lost), with backoff.
    for (auto& [id, rc] : rchannels_) {
        if (rc.status != "open" || rc.out_phase.empty() || rc.next_try > now) continue;
        rchan_drive(id, error);
    }
    // 2) receiver side: a pending state the proposer abandoned (it has no outbox
    //    and is still at our current number) is safely dropped (we never revoked).
    for (auto& [id, rc] : rchannels_) {
        if (rc.status != "open" || !rc.ch->has_pending() || !rc.out_phase.empty() || rc.pending_since == 0 ||
            now - rc.pending_since < 90)
            continue;
        std::string resp;
        if (!node_.request_peer(rc.peer, "CHAN|SYNC|" + id, resp, error) || resp.rfind("CHANOK|", 0) != 0) {
            rc.pending_since = now;
            continue;
        }
        const auto r = split_bar(resp.substr(7));
        if (r.size() == 3 && r[0] == std::to_string(rc.ch->state().n) && r[1] == "-") {
            rc.ch->drop_pending();
            rc.in_effect.clear();
            rc.pending_since = 0;
            rchan_save(id, rc, error);
        } else {
            rc.pending_since = now;
        }
    }
    // 3) queued HTLC forwards / settles / fails.
    auto actions = std::move(rchan_actions_);
    rchan_actions_.clear();
    for (const auto& a : actions) {
        bool keep = false;
        rchan_run_action(a, keep);
        if (keep) rchan_actions_.push_back(a);
    }
    rchan_save_meta();
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
    if (cmd == "rchan_invoice") {
        // rchan_invoice <wallet> <amount>  -> payment_hash (preimage stays on this node)
        std::string w;
        std::uint64_t amount = 0;
        iss >> w >> amount;
        WalletKeys k;
        if (w.empty() || amount == 0 || !load_wallet_keys(w, k, error)) return "error: usage rchan_invoice <wallet> <amount>";
        std::string pre;
        if (!random_hex(32, pre, error)) return "error: " + error;
        const auto ph = script::hash_preimage_hex(pre);
        rchan_invoices_[ph] = w + " " + std::to_string(amount) + " " + pre + " open";
        rchan_save_meta();
        return "ok payment_hash=" + ph + " amount=" + std::to_string(amount);
    }
    if (cmd == "rchan_payment") {
        std::string ph;
        iss >> ph;
        if (auto p = rchan_payments_.find(ph); p != rchan_payments_.end()) return "ok role=payer status=" + p->second;
        if (auto i = rchan_invoices_.find(ph); i != rchan_invoices_.end()) return "ok role=payee status=" + split_bar(i->second, ' ').back();
        return "error: unknown payment";
    }
    if (cmd == "rchan_tick") {
        rchan_tick();
        return "ok actions=" + std::to_string(rchan_actions_.size());
    }
    if (cmd == "rchan_fault") {
        // Test-only fault injection: drop_rev | drop_rev_reply | none. Refused on mainnet.
        std::string mode;
        iss >> mode;
        if (chain_.config().network_mode == "mainnet") return "error: fault injection disabled on mainnet";
        if (mode != "drop_rev" && mode != "drop_rev_reply" && mode != "none") return "error: usage rchan_fault <drop_rev|drop_rev_reply|none>";
        rchan_fault_ = mode == "none" ? "" : mode;
        return "ok fault=" + mode;
    }
    if (cmd == "rchan_list") {
        std::ostringstream out;
        out << "rchannels=" << rchannels_.size();
        for (const auto& [id, rc] : rchannels_) {
            const auto& st = rc.ch->state();
            out << "\n" << id << " status=" << rc.status << " wallet=" << rc.wallet << " side=" << (rc.ch->is_a() ? "a" : "b")
                << " peer=" << rc.peer << " capacity=" << rc.ch->info().capacity << " state=" << st.n
                << " bal_a=" << st.bal_a << " bal_b=" << st.bal_b << " local=" << rc.ch->local_balance()
                << " htlcs=" << st.htlcs.size() << " pending=" << (rc.ch->has_pending() ? 1 : 0)
                << " outbox=" << (rc.out_phase.empty() ? "-" : rc.out_phase) << " attempts=" << rc.attempts;
        }
        return out.str();
    }
    std::string id;
    iss >> id;
    auto it = rchannels_.find(id);
    if (it == rchannels_.end()) return "error: unknown channel";
    auto& rc = it->second;
    if (rc.busy) return "error: channel busy";
    auto& ch = *rc.ch;
    if (cmd == "rchan_recover") {
        // rchan_recover <id> [retry|rollback|force]
        std::string mode = "retry";
        iss >> mode;
        if (mode == "retry") {
            if (rc.out_phase.empty()) return "ok nothing_in_flight state=" + std::to_string(ch.state().n);
            rc.next_try = 0;
            if (!rchan_drive(id, error)) return "error: still in flight (" + rc.out_phase + "): " + error;
            return "ok recovered state=" + std::to_string(ch.state().n) + " bal_a=" + std::to_string(ch.state().bal_a) +
                   " bal_b=" + std::to_string(ch.state().bal_b);
        }
        if (mode == "rollback") {
            if (rc.out_phase == "rev") return "error: already revoked our previous state; retry or force close";
            std::string resp;
            if (rc.out_phase == "upd") {
                channel::State x;
                channel::decode_state(rc.out_state, x);
                node_.request_peer(rc.peer, "CHAN|ABORT|" + id + "|" + std::to_string(x.n), resp, error);  // best effort
                rc.out_action.clear();  // operator rollback: do not re-queue
                rchan_abandon_out(rc, "rolled back");
            }
            if (ch.has_pending()) { ch.drop_pending(); rc.in_effect.clear(); rc.pending_since = 0; }
            if (!rchan_save(id, rc, error)) return "error: " + error;
            return "ok rolled_back state=" + std::to_string(ch.state().n) + " abandoned_signed=" + std::to_string(ch.abandoned_count());
        }
        if (mode == "force") {
            if (rc.status != "open") return "error: channel not open";
            const auto tx = ch.signed_local_commitment();
            if (!node_.submit_transaction(tx, error)) return "error: " + error;
            rc.status = "closing_force";
            rchan_save(id, rc, error);
            return "ok force_close_txid=" + hash_transaction(tx) + " state=" + std::to_string(ch.state().n);
        }
        return "error: usage rchan_recover <id> [retry|rollback|force]";
    }
    if (rc.status != "open") return "error: channel not open";
    if (cmd == "rchan_htlc_pay") {
        // rchan_htlc_pay <first_chan> <amount> <payment_hash> [route c2+c3|-] [fee_per_hop] [delta]
        std::uint64_t amount = 0, fee = 0, delta = 6;
        std::string ph, route = "-";
        iss >> amount >> ph >> route >> fee >> delta;
        if (route.empty()) route = "-";
        if (amount == 0 || ph.size() != 128 || !safe_token(route) || delta == 0)
            return "error: usage rchan_htlc_pay <chan> <amount> <payment_hash> [route c2+c3|-] [fee] [delta]";
        const std::size_t hops = route == "-" ? 1 : 1 + split_bar(route, '+').size();
        const std::uint64_t amt = amount + fee * (hops - 1);
        const std::uint64_t exp = chain_.height() + 20 + delta * hops;
        channel::State next = ch.state();
        ++next.n;
        auto& mine = side_bal(next, ch.is_a());
        const std::uint64_t reserve = ch.is_a() ? ch.info().fee : 0;
        if (mine < amt + reserve) return "error: insufficient channel balance";
        mine -= amt;
        const auto hid = next.next_htlc_id++;
        next.htlcs.push_back(channel::Htlc{hid, ch.is_a(), amt, ph, exp});
        rchan_payments_[ph] = "pending";
        rchan_save_meta();
        if (!rchan_propose(id, next, "ADD", route + "," + std::to_string(fee) + "," + std::to_string(delta), "", error)) {
            if (rc.out_phase.empty()) { rchan_payments_[ph] = "failed"; rchan_save_meta(); return "error: " + error; }
            return "ok in_flight=" + rc.out_phase + " htlc=" + std::to_string(hid) + " (resumed by rchan_tick): " + error;
        }
        return "ok channel=" + id + " htlc=" + std::to_string(hid) + " amount=" + std::to_string(amt) + " expiry=" +
               std::to_string(exp) + " hops=" + std::to_string(hops) + " state=" + std::to_string(ch.state().n);
    }
    if (cmd == "rchan_pay") {
        std::uint64_t amount = 0;
        iss >> amount;
        channel::State next = ch.state();
        ++next.n;
        auto& mine = side_bal(next, ch.is_a());
        auto& theirs = side_bal(next, !ch.is_a());
        const std::uint64_t reserve = ch.is_a() ? ch.info().fee : 0;
        if (amount == 0 || mine < amount + reserve) return "error: insufficient channel balance";
        mine -= amount;
        theirs += amount;
        if (!rchan_propose(id, next, "PAY", "-", "", error)) {
            if (rc.out_phase.empty()) return "error: " + error;
            return "error: in_flight=" + rc.out_phase + " (resumed by rchan_tick / rchan_recover): " + error;
        }
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
