#include "addition/channel.hpp"

#include "addition/chain.hpp"
#include "addition/crypto.hpp"
#include "addition/script.hpp"

#include <algorithm>
#include <sstream>

namespace addition::channel {

std::string funding_script(const std::string& a, const std::string& b) {
    return "and(pk(" + a + "),pk(" + b + "))";
}
std::string to_local_script(const std::string& holder, const std::string& other, const std::string& rh, std::uint64_t delay) {
    return "or(and(pk(" + other + "),sha3(" + rh + ")),and(pk(" + holder + "),older(" + std::to_string(delay) + ")))";
}
std::string htlc_script(const std::string& revoker, const std::string& offerer, const std::string& receiver,
                        const std::string& rh, const std::string& ph, std::uint64_t expiry) {
    return "or(and(pk(" + revoker + "),sha3(" + rh + ")),or(and(pk(" + receiver + "),sha3(" + ph + ")),and(pk(" + offerer +
           "),after(" + std::to_string(expiry) + "))))";
}

static std::uint64_t htlc_sum(const State& s) {
    std::uint64_t t = 0;
    for (auto& h : s.htlcs) t += h.amount;
    return t;
}

Transaction commitment_tx(const Info& info, bool holder_a, const State& s, const std::string& rh) {
    Transaction t;
    t.inputs.push_back(TxInput{info.funding_txid, info.funding_vout, {}});
    const std::string& holder = holder_a ? info.addr_a : info.addr_b;
    const std::string& other = holder_a ? info.addr_b : info.addr_a;
    const std::uint64_t a_amt = s.bal_a - info.fee;  // funder pays fee
    const std::uint64_t holder_amt = holder_a ? a_amt : s.bal_b;
    const std::uint64_t other_amt = holder_a ? s.bal_b : a_amt;
    if (holder_amt) t.outputs.push_back({script::script_address(to_local_script(holder, other, rh, info.to_self_delay)), holder_amt});
    if (other_amt) t.outputs.push_back({other, other_amt});
    for (auto& h : s.htlcs) {
        const auto& off = h.offered_by_a ? info.addr_a : info.addr_b;
        const auto& rcv = h.offered_by_a ? info.addr_b : info.addr_a;
        t.outputs.push_back({script::script_address(htlc_script(other, off, rcv, rh, h.payment_hash, h.expiry)), h.amount});
    }
    t.fee = info.fee;
    t.nonce = s.n;
    return t;
}

Channel::Channel(WalletKeys local, std::string remote_pubkey, bool local_is_a, Info info, std::string seed_hex)
    : local_(std::move(local)), remote_pub_(std::move(remote_pubkey)), is_a_(local_is_a), info_(std::move(info)),
      seed_(std::move(seed_hex)) {}

std::string Channel::rev_secret(std::uint64_t n) const {
    return to_hex(sha3_512_bytes("ADD/chan/rev|" + seed_ + "|" + std::to_string(n)));
}
std::string Channel::rev_hash(std::uint64_t n) const { return script::hash_preimage_hex(rev_secret(n)); }

std::pair<std::string, std::string> Channel::initial_hashes() const { return {rev_hash(0), rev_hash(1)}; }
void Channel::set_remote_initial_hashes(const std::string& h0, const std::string& h1) {
    remote_hashes_[0] = h0;
    remote_hashes_[1] = h1;
}
void Channel::set_funding(const std::string& txid, std::uint32_t vout) {
    info_.funding_txid = txid;
    info_.funding_vout = vout;
}

bool Channel::validate_next(const State& next, std::string& err) const {
    if (pending_) { err = "update already in flight"; return false; }
    if (next.n != cur_.n + 1) { err = "bad state number"; return false; }
    if (next.bal_a < info_.fee) { err = "funder cannot pay commitment fee"; return false; }
    if (next.htlcs.size() > 16) { err = "too many htlcs"; return false; }
    if (next.bal_a + next.bal_b + htlc_sum(next) != info_.capacity) { err = "state does not conserve capacity"; return false; }
    for (auto& h : next.htlcs) {
        if (h.amount == 0 || h.payment_hash.size() != 128) { err = "bad htlc"; return false; }
    }
    return true;
}

CommitSig Channel::sign_remote_commitment(const State& next) const {
    const auto it = remote_hashes_.find(next.n);
    const std::string rh = it == remote_hashes_.end() ? std::string(128, '0') : it->second;
    return {next.n, script::sign(local_.private_key, commitment_tx(info_, !is_a_, next, rh))};
}

bool Channel::receive_commit_sig(const State& next, const CommitSig& cs, std::string& err) {
    const bool initial = next.n == 0 && remote_sig_cur_.empty();
    if (!initial && !validate_next(next, err)) return false;
    const auto mine = commitment_tx(info_, is_a_, next, rev_hash(next.n));
    if (cs.n != next.n || !verify_message_signature_hybrid(remote_pub_, script::sighash(mine), cs.sig)) {
        err = "invalid commitment signature";
        return false;
    }
    if (initial) {
        cur_ = next;
        remote_sig_cur_ = cs.sig;
    } else {
        pending_ = next;
        remote_sig_pending_ = cs.sig;
    }
    return true;
}

Revoke Channel::revoke_and_advance() {
    Revoke r{cur_.n, rev_secret(cur_.n), rev_hash(cur_.n + 2)};
    history_[cur_.n] = cur_;
    cur_ = *pending_;
    remote_sig_cur_ = *remote_sig_pending_;
    pending_.reset();
    remote_sig_pending_.reset();
    return r;
}

bool Channel::receive_revoke(const Revoke& r, std::string& err) {
    const auto it = remote_hashes_.find(r.revoked_n);
    if (it == remote_hashes_.end() || script::hash_preimage_hex(r.secret) != it->second) {
        err = "revocation secret does not match";
        return false;
    }
    if (r.next_hash.size() != 128) { err = "bad next hash"; return false; }
    remote_secrets_[r.revoked_n] = r.secret;
    remote_hashes_[r.revoked_n + 2] = r.next_hash;
    return true;
}

static void add_funding_witness(Transaction& t, const Info& info, const std::string& pub_a, const std::string& sig_a,
                                const std::string& pub_b, const std::string& sig_b) {
    script::Witness w;
    w.script = funding_script(info.addr_a, info.addr_b);
    w.sigs = {{pub_a, sig_a}, {pub_b, sig_b}};
    t.inputs[0].witness = script::encode_witness(w);
}

Transaction Channel::signed_local_commitment() const {
    auto t = commitment_tx(info_, is_a_, cur_, rev_hash(cur_.n));
    const auto my = script::sign(local_.private_key, t);
    if (is_a_) add_funding_witness(t, info_, local_.public_key, my, remote_pub_, remote_sig_cur_);
    else add_funding_witness(t, info_, remote_pub_, remote_sig_cur_, local_.public_key, my);
    return t;
}

Transaction Channel::coop_close_tx() const {
    Transaction t;
    t.inputs.push_back(TxInput{info_.funding_txid, info_.funding_vout, {}});
    if (cur_.bal_a > info_.fee) t.outputs.push_back({info_.addr_a, cur_.bal_a - info_.fee});
    if (cur_.bal_b) t.outputs.push_back({info_.addr_b, cur_.bal_b});
    t.fee = info_.fee;
    t.nonce = (1ull << 40) + cur_.n;  // distinct from any commitment
    return t;
}
std::string Channel::sign_coop_close() const { return script::sign(local_.private_key, coop_close_tx()); }
bool Channel::assemble_coop_close(const std::string& remote_sig, Transaction& out, std::string& err) const {
    if (!cur_.htlcs.empty()) { err = "cannot coop-close with pending htlcs"; return false; }
    out = coop_close_tx();
    if (!verify_message_signature_hybrid(remote_pub_, script::sighash(out), remote_sig)) { err = "bad close sig"; return false; }
    const auto my = script::sign(local_.private_key, out);
    if (is_a_) add_funding_witness(out, info_, local_.public_key, my, remote_pub_, remote_sig);
    else add_funding_witness(out, info_, remote_pub_, remote_sig, local_.public_key, my);
    return true;
}

std::optional<std::pair<std::uint32_t, std::string>> Channel::find_output(const Transaction& t, const std::string& s) const {
    const auto addr = script::script_address(s);
    for (std::uint32_t i = 0; i < t.outputs.size(); ++i)
        if (t.outputs[i].recipient == addr) return std::make_pair(i, s);
    return std::nullopt;
}

Transaction Channel::sweep(const Transaction& parent, std::uint32_t vout, const std::string& s,
                           const std::vector<std::string>& pre, std::uint64_t nonce) const {
    Transaction t;
    t.inputs.push_back(TxInput{hash_transaction(parent), vout, {}});
    t.outputs.push_back({local_.address, parent.outputs[vout].amount - info_.fee});
    t.fee = info_.fee;
    t.nonce = nonce;
    script::Witness w{s, {{local_.public_key, script::sign(local_.private_key, t)}}, pre};
    t.inputs[0].witness = script::encode_witness(w);
    return t;
}

std::optional<Transaction> Channel::penalty_for(const Transaction& b) const {
    const std::uint64_t n = b.nonce;
    const auto sec = remote_secrets_.find(n);
    const auto hs = history_.find(n);
    if (sec == remote_secrets_.end() || hs == history_.end()) return std::nullopt;
    const auto rh = remote_hashes_.at(n);
    if (hash_transaction(commitment_tx(info_, !is_a_, hs->second, rh)) != hash_transaction(b)) return std::nullopt;
    const auto remote = remote_address();
    std::vector<std::pair<std::uint32_t, std::string>> outs;
    if (auto o = find_output(b, to_local_script(remote, local_.address, rh, info_.to_self_delay))) outs.push_back(*o);
    for (auto& h : hs->second.htlcs) {
        const auto& off = h.offered_by_a ? info_.addr_a : info_.addr_b;
        const auto& rcv = h.offered_by_a ? info_.addr_b : info_.addr_a;
        const auto hs_script = htlc_script(local_.address, off, rcv, rh, h.payment_hash, h.expiry);
        const auto addr = script::script_address(hs_script);
        for (std::uint32_t i = 0; i < b.outputs.size(); ++i) {
            bool dup = false;
            for (auto& x : outs) dup |= x.first == i;
            if (!dup && b.outputs[i].recipient == addr) { outs.emplace_back(i, hs_script); break; }
        }
    }
    if (outs.empty()) return std::nullopt;
    Transaction t;
    const auto txid = hash_transaction(b);
    std::uint64_t total = 0;
    for (auto& [v, s] : outs) {
        t.inputs.push_back(TxInput{txid, v, {}});
        total += b.outputs[v].amount;
    }
    if (total <= info_.fee) return std::nullopt;
    t.outputs.push_back({local_.address, total - info_.fee});
    t.fee = info_.fee;
    t.nonce = (2ull << 40) + n;
    const auto sig = script::sign(local_.private_key, t);  // one sighash covers all inputs
    for (std::size_t i = 0; i < outs.size(); ++i)
        t.inputs[i].witness = script::encode_witness({outs[i].second, {{local_.public_key, sig}}, {sec->second}});
    return t;
}

std::optional<Transaction> Channel::sweep_to_local(const Transaction& c) const {
    const auto s = to_local_script(local_.address, remote_address(), rev_hash(c.nonce), info_.to_self_delay);
    auto o = find_output(c, s);
    if (!o || c.outputs[o->first].amount <= info_.fee) return std::nullopt;
    return sweep(c, o->first, s, {}, (3ull << 40) + c.nonce);
}

static const Htlc* find_htlc(const State& s, std::uint64_t id) {
    for (auto& h : s.htlcs) if (h.id == id) return &h;
    return nullptr;
}

std::optional<Transaction> Channel::sweep_htlc_timeout(const Transaction& c, bool mine, std::uint64_t id) const {
    return sweep_htlc_success(c, mine, id, "");
}

std::optional<Transaction> Channel::sweep_htlc_success(const Transaction& c, bool mine, std::uint64_t id,
                                                       const std::string& preimage) const {
    const std::uint64_t n = c.nonce;
    const State* st = n == cur_.n ? &cur_ : (history_.count(n) ? &history_.at(n) : nullptr);
    if (!st) return std::nullopt;
    const Htlc* h = find_htlc(*st, id);
    if (!h) return std::nullopt;
    std::string rh;
    if (mine) rh = rev_hash(n);
    else if (remote_hashes_.count(n)) rh = remote_hashes_.at(n);
    else return std::nullopt;
    const auto& off = h->offered_by_a ? info_.addr_a : info_.addr_b;
    const auto& rcv = h->offered_by_a ? info_.addr_b : info_.addr_a;
    const auto revoker = mine ? remote_address() : local_.address;
    const auto s = htlc_script(revoker, off, rcv, rh, h->payment_hash, h->expiry);
    // HTLCs with equal hash/expiry share a script, so locate the output by its
    // deterministic position: [to_local?][to_remote?][htlcs in state order].
    const bool holder_a = mine ? is_a_ : !is_a_;
    const auto expect = commitment_tx(info_, holder_a, *st, rh);
    if (hash_transaction(expect) != hash_transaction(c)) return std::nullopt;
    std::uint32_t base = static_cast<std::uint32_t>(expect.outputs.size() - st->htlcs.size());
    std::optional<std::pair<std::uint32_t, std::string>> o;
    for (std::size_t i = 0; i < st->htlcs.size(); ++i)
        if (st->htlcs[i].id == id) o = std::make_pair(base + static_cast<std::uint32_t>(i), s);
    if (!o || c.outputs[o->first].recipient != script::script_address(s)) return std::nullopt;
    std::vector<std::string> pre;
    if (!preimage.empty()) pre.push_back(preimage);
    return sweep(c, o->first, s, pre, (4ull << 40) + (n << 8) + id);
}

// ---------------- persistence ----------------

std::string encode_state(const State& s) {
    std::ostringstream o;
    o << s.n << ":" << s.bal_a << ":" << s.bal_b << ":" << s.next_htlc_id << ":";
    for (std::size_t i = 0; i < s.htlcs.size(); ++i) {
        const auto& h = s.htlcs[i];
        if (i) o << ";";
        o << h.id << "," << (h.offered_by_a ? 1 : 0) << "," << h.amount << "," << h.payment_hash << "," << h.expiry;
    }
    return o.str();
}

bool decode_state(const std::string& t, State& s) {
    s = State{};
    std::vector<std::string> f;
    std::string cur;
    std::istringstream is(t);
    while (std::getline(is, cur, ':')) f.push_back(cur);
    if (f.size() == 4) f.emplace_back();
    if (f.size() != 5) return false;
    try {
        s.n = std::stoull(f[0]);
        s.bal_a = std::stoull(f[1]);
        s.bal_b = std::stoull(f[2]);
        s.next_htlc_id = std::stoull(f[3]);
        std::istringstream hs(f[4]);
        std::string one;
        while (std::getline(hs, one, ';')) {
            std::istringstream fs(one);
            std::string id, off, amt, ph, exp;
            if (!std::getline(fs, id, ',') || !std::getline(fs, off, ',') || !std::getline(fs, amt, ',') ||
                !std::getline(fs, ph, ',') || !std::getline(fs, exp, ','))
                return false;
            s.htlcs.push_back(Htlc{std::stoull(id), off == "1", std::stoull(amt), ph, std::stoull(exp)});
        }
    } catch (...) {
        return false;
    }
    return true;
}

std::string Channel::serialize() const {
    std::ostringstream o;
    o << "v 1\nis_a " << (is_a_ ? 1 : 0) << "\naddr_a " << info_.addr_a << "\naddr_b " << info_.addr_b
      << "\ncapacity " << info_.capacity << "\nfunding " << (info_.funding_txid.empty() ? "-" : info_.funding_txid)
      << " " << info_.funding_vout << "\ndelay " << info_.to_self_delay << "\nfee " << info_.fee
      << "\nremote_pub " << remote_pub_ << "\nseed " << seed_ << "\ncur " << encode_state(cur_)
      << "\nremote_sig_cur " << (remote_sig_cur_.empty() ? "-" : remote_sig_cur_) << "\n";
    if (pending_) o << "pending " << encode_state(*pending_) << "\n";
    if (remote_sig_pending_) o << "remote_sig_pending " << *remote_sig_pending_ << "\n";
    for (const auto& [n, h] : remote_hashes_) o << "rhash " << n << " " << h << "\n";
    for (const auto& [n, h] : remote_secrets_) o << "rsecret " << n << " " << h << "\n";
    for (const auto& [n, st] : history_) o << "hist " << n << " " << encode_state(st) << "\n";
    return o.str();
}

std::unique_ptr<Channel> Channel::deserialize(const std::string& data, WalletKeys local, std::string& err) {
    std::istringstream in(data);
    std::string line;
    Info info;
    bool is_a = false;
    std::string remote_pub, seed;
    State cur;
    std::optional<State> pending;
    std::string sig_cur;
    std::optional<std::string> sig_pending;
    std::map<std::uint64_t, std::string> rh, rs;
    std::map<std::uint64_t, State> hist;
    try {
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string k;
            ls >> k;
            if (k == "is_a") { int v; ls >> v; is_a = v == 1; }
            else if (k == "addr_a") ls >> info.addr_a;
            else if (k == "addr_b") ls >> info.addr_b;
            else if (k == "capacity") ls >> info.capacity;
            else if (k == "funding") { ls >> info.funding_txid >> info.funding_vout; if (info.funding_txid == "-") info.funding_txid.clear(); }
            else if (k == "delay") ls >> info.to_self_delay;
            else if (k == "fee") ls >> info.fee;
            else if (k == "remote_pub") ls >> remote_pub;
            else if (k == "seed") ls >> seed;
            else if (k == "cur") { std::string t; ls >> t; if (!decode_state(t, cur)) throw 1; }
            else if (k == "pending") { std::string t; ls >> t; State s; if (!decode_state(t, s)) throw 1; pending = s; }
            else if (k == "remote_sig_cur") { ls >> sig_cur; if (sig_cur == "-") sig_cur.clear(); }
            else if (k == "remote_sig_pending") { std::string t; ls >> t; sig_pending = t; }
            else if (k == "rhash") { std::uint64_t n; std::string h; ls >> n >> h; rh[n] = h; }
            else if (k == "rsecret") { std::uint64_t n; std::string h; ls >> n >> h; rs[n] = h; }
            else if (k == "hist") { std::uint64_t n; std::string t; ls >> n >> t; State s; if (!decode_state(t, s)) throw 1; hist[n] = s; }
        }
    } catch (...) {
        err = "corrupt channel file";
        return nullptr;
    }
    if (remote_pub.empty() || seed.empty() || info.addr_a.empty() || info.addr_b.empty()) {
        err = "corrupt channel file";
        return nullptr;
    }
    if (local.address != (is_a ? info.addr_a : info.addr_b)) {
        err = "channel file does not belong to this wallet";
        return nullptr;
    }
    auto c = std::make_unique<Channel>(std::move(local), remote_pub, is_a, info, seed);
    c->cur_ = cur;
    c->pending_ = pending;
    c->remote_sig_cur_ = sig_cur;
    c->remote_sig_pending_ = sig_pending;
    c->remote_hashes_ = rh;
    c->remote_secrets_ = rs;
    c->history_ = hist;
    return c;
}

// ---------------- in-process drivers ----------------

bool open_channel(Chain& chain, const WalletKeys& a, const WalletKeys& b, std::uint64_t capacity, std::uint64_t push,
                  std::unique_ptr<Channel>& ca, std::unique_ptr<Channel>& cb, std::vector<Transaction>& txs, std::string& err) {
    if (push > capacity || capacity - push < 1) { err = "bad push amount"; return false; }
    Info info;
    info.addr_a = a.address;
    info.addr_b = b.address;
    info.capacity = capacity;
    std::string sa, sb;
    if (!random_hex(32, sa, err) || !random_hex(32, sb, err)) return false;
    ca = std::make_unique<Channel>(a, b.public_key, true, info, sa);
    cb = std::make_unique<Channel>(b, a.public_key, false, info, sb);
    const auto fs = script::script_address(funding_script(a.address, b.address));
    Transaction f;
    if (!chain.build_transaction(a.address, fs, capacity, 1, chain.next_nonce(a.address), f, err)) return false;
    f.signer = a.address;
    f.signer_pubkey = a.public_key;
    f.signature = sign_message_hybrid(a.private_key, hash_transaction(f));
    std::uint32_t vout = 0;
    while (vout < f.outputs.size() && f.outputs[vout].recipient != fs) ++vout;
    const auto txid = hash_transaction(f);
    ca->set_funding(txid, vout);
    cb->set_funding(txid, vout);
    auto [a0, a1] = ca->initial_hashes();
    auto [b0, b1] = cb->initial_hashes();
    ca->set_remote_initial_hashes(b0, b1);
    cb->set_remote_initial_hashes(a0, a1);
    State s0;
    s0.bal_a = capacity - push;
    s0.bal_b = push;
    // Exchange signatures on initial commitments BEFORE the funding tx is broadcast.
    if (!cb->receive_commit_sig(s0, ca->sign_remote_commitment(s0), err)) return false;
    if (!ca->receive_commit_sig(s0, cb->sign_remote_commitment(s0), err)) return false;
    txs = {f};
    return true;
}

bool update(Channel& p, Channel& q, const State& next, std::string& err) {
    if (!p.validate_next(next, err) || !q.validate_next(next, err)) return false;
    if (!q.receive_commit_sig(next, p.sign_remote_commitment(next), err)) return false;
    if (!p.receive_commit_sig(next, q.sign_remote_commitment(next), err)) return false;
    if (!q.receive_revoke(p.revoke_and_advance(), err)) return false;
    if (!p.receive_revoke(q.revoke_and_advance(), err)) return false;
    return true;
}

static std::uint64_t& bal(State& s, bool a) { return a ? s.bal_a : s.bal_b; }

bool pay(Channel& payer, Channel& payee, std::uint64_t amount, std::string& err) {
    State n = payer.state();
    ++n.n;
    auto& pb = bal(n, payer.is_a());
    const std::uint64_t reserve = payer.is_a() ? payer.info().fee : 0;
    if (amount == 0 || pb < amount + reserve) { err = "insufficient channel balance"; return false; }
    pb -= amount;
    bal(n, payee.is_a()) += amount;
    return update(payer, payee, n, err);
}

bool add_htlc(Channel& payer, Channel& payee, std::uint64_t amount, const std::string& ph, std::uint64_t expiry,
              std::uint64_t& id, std::string& err) {
    State n = payer.state();
    ++n.n;
    auto& pb = bal(n, payer.is_a());
    const std::uint64_t reserve = payer.is_a() ? payer.info().fee : 0;
    if (amount == 0 || pb < amount + reserve) { err = "insufficient channel balance"; return false; }
    pb -= amount;
    id = n.next_htlc_id++;
    n.htlcs.push_back(Htlc{id, payer.is_a(), amount, ph, expiry});
    return update(payer, payee, n, err);
}

static bool remove_htlc(Channel& proposer, Channel& peer, std::uint64_t id, bool to_receiver, std::string& err) {
    State n = proposer.state();
    ++n.n;
    auto it = std::find_if(n.htlcs.begin(), n.htlcs.end(), [&](const Htlc& h) { return h.id == id; });
    if (it == n.htlcs.end()) { err = "unknown htlc"; return false; }
    const bool credit_a = to_receiver ? !it->offered_by_a : it->offered_by_a;
    bal(n, credit_a) += it->amount;
    n.htlcs.erase(it);
    return update(proposer, peer, n, err);
}

bool settle_htlc(Channel& payee, Channel& payer, std::uint64_t id, const std::string& preimage, std::string& err) {
    const Htlc* h = find_htlc(payer.state(), id);
    // Payer only accepts removal-to-receiver against a valid preimage.
    if (!h || script::hash_preimage_hex(preimage) != h->payment_hash) { err = "bad preimage"; return false; }
    return remove_htlc(payee, payer, id, true, err);
}

bool fail_htlc(Channel& payee, Channel& payer, std::uint64_t id, std::string& err) {
    return remove_htlc(payee, payer, id, false, err);
}

bool route_payment(const std::vector<Hop>& route, std::uint64_t amount, const std::string& preimage,
                   std::uint64_t final_expiry, std::uint64_t delta, std::uint64_t fee, std::string& err) {
    const auto ph = script::hash_preimage_hex(preimage);
    if (ph.empty()) { err = "bad preimage"; return false; }
    const std::size_t k = route.size();
    std::vector<std::uint64_t> ids;
    for (std::size_t i = 0; i < k; ++i) {
        std::uint64_t id = 0;
        const std::uint64_t amt = amount + fee * (k - 1 - i);
        const std::uint64_t exp = final_expiry + delta * (k - 1 - i);
        if (!add_htlc(*route[i].payer, *route[i].payee, amt, ph, exp, id, err)) {
            for (std::size_t j = i; j-- > 0;) {
                std::string e2;
                fail_htlc(*route[j].payee, *route[j].payer, ids[j], e2);
            }
            err = "hop " + std::to_string(i) + ": " + err;
            return false;
        }
        ids.push_back(id);
    }
    // Final payee reveals the preimage; it propagates backwards.
    for (std::size_t i = k; i-- > 0;) {
        if (!settle_htlc(*route[i].payee, *route[i].payer, ids[i], preimage, err)) {
            err = "settle hop " + std::to_string(i) + ": " + err;
            return false;
        }
    }
    return true;
}

}  // namespace addition::channel
