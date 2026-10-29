#include "addition/script.hpp"

#include "addition/crypto.hpp"

#include <cctype>
#include <set>

namespace addition::script {
namespace {

struct Parser {
    const std::string& s;
    std::size_t i{0};
    std::size_t nodes{0};
    std::string err;

    bool atom(std::string& out) {
        const auto st = i;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
        out = s.substr(st, i - st);
        if (out.empty()) { err = "expected atom at " + std::to_string(st); return false; }
        return true;
    }
    bool expect(char c) {
        if (i >= s.size() || s[i] != c) { err = std::string("expected '") + c + "' at " + std::to_string(i); return false; }
        ++i;
        return true;
    }
    bool node(Node& n, std::size_t depth) {
        if (depth > kMaxDepth) { err = "script too deep"; return false; }
        if (++nodes > kMaxNodes) { err = "script too many nodes"; return false; }
        std::string name;
        if (!atom(name) || !expect('(')) return false;
        if (name == "and" || name == "or") {
            n.kind = name == "and" ? Node::Kind::And : Node::Kind::Or;
            n.a = std::make_unique<Node>();
            n.b = std::make_unique<Node>();
            return node(*n.a, depth + 1) && expect(',') && node(*n.b, depth + 1) && expect(')');
        }
        std::string arg;
        if (!atom(arg) || !expect(')')) return false;
        if (name == "pk") {
            n.kind = Node::Kind::Pk;
            n.arg = arg;
            return true;
        }
        if (name == "sha3") {
            if (arg.size() != 128) { err = "sha3 arg must be 128 hex chars"; return false; }
            for (char c : arg) if (!std::isxdigit(static_cast<unsigned char>(c)) || std::isupper(static_cast<unsigned char>(c))) { err = "sha3 arg must be lowercase hex"; return false; }
            n.kind = Node::Kind::Sha3;
            n.arg = arg;
            return true;
        }
        if (name == "after" || name == "older") {
            if (arg.empty() || arg.size() > 12 || (arg.size() > 1 && arg[0] == '0')) { err = "bad number"; return false; }
            for (char c : arg) if (!std::isdigit(static_cast<unsigned char>(c))) { err = "bad number"; return false; }
            n.kind = name == "after" ? Node::Kind::After : Node::Kind::Older;
            n.n = std::stoull(arg);
            return true;
        }
        err = "unknown opcode " + name;
        return false;
    }
};

struct Eval {
    const SpendContext& ctx;
    const Witness& w;
    std::vector<bool> sig_used, sig_ok_cached, sig_checked;
    std::vector<bool> pre_used;
    std::vector<std::string> pre_hash;
    std::string err;

    bool sig_valid(std::size_t k) {
        if (!sig_checked[k]) {
            sig_checked[k] = true;
            sig_ok_cached[k] = verify_message_signature_hybrid(w.sigs[k].first, ctx.sighash, w.sigs[k].second);
        }
        return sig_ok_cached[k];
    }
    bool eval(const Node& n) {
        switch (n.kind) {
            case Node::Kind::Pk:
                for (std::size_t k = 0; k < w.sigs.size(); ++k) {
                    std::string e;
                    const auto scheme = infer_sig_scheme_from_pubkey_hex(w.sigs[k].first);
                    if (address_binds_pubkey(n.arg, scheme, w.sigs[k].first, e) && sig_valid(k)) {
                        sig_used[k] = true;
                        return true;
                    }
                }
                return false;
            case Node::Kind::Sha3:
                for (std::size_t k = 0; k < w.preimages.size(); ++k) {
                    if (pre_hash[k] == n.arg) { pre_used[k] = true; return true; }
                }
                return false;
            case Node::Kind::After: return ctx.spend_height >= n.n;
            case Node::Kind::Older: return ctx.spend_height >= ctx.utxo_height && ctx.spend_height - ctx.utxo_height >= n.n;
            case Node::Kind::And: { const bool x = eval(*n.a); const bool y = eval(*n.b); return x && y; }
            case Node::Kind::Or:  { const bool x = eval(*n.a); const bool y = eval(*n.b); return x || y; }
        }
        return false;
    }
};

bool is_lower_hex(const std::string& h, std::size_t max_len) {
    if (h.empty() || h.size() > max_len || h.size() % 2) return false;
    for (char c : h) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

} // namespace

bool parse(const std::string& text, Node& out, std::string& error) {
    if (text.empty() || text.size() > kMaxScriptBytes) { error = "script size"; return false; }
    Parser p{text};
    if (!p.node(out, 0)) { error = p.err; return false; }
    if (p.i != text.size()) { error = "trailing script bytes"; return false; }
    return true;
}

std::string script_address(const std::string& t) { return "sh1" + to_hex(sha3_512_bytes("ADD/script/v1|" + t)); }
bool is_script_address(const std::string& a) { return a.size() == 3 + 128 && a.rfind("sh1", 0) == 0; }

std::string hash_preimage_hex(const std::string& preimage_hex) {
    std::vector<std::uint8_t> b;
    std::string e;
    if (!hex_to_bytes(preimage_hex, b, e)) return {};
    return to_hex(sha3_512_bytes(b));
}

// Witness encoding: script ';' n_sigs ';' (pub ':' sig ';')* n_pre ';' (pre ';')*
std::string encode_witness(const Witness& w) {
    std::string o = w.script + ";" + std::to_string(w.sigs.size()) + ";";
    for (auto& [p, s] : w.sigs) o += p + ":" + s + ";";
    o += std::to_string(w.preimages.size()) + ";";
    for (auto& x : w.preimages) o += x + ";";
    return o;
}

bool decode_witness(const std::string& s, Witness& out, std::string& error) {
    out = {};
    std::vector<std::string> parts;
    std::size_t st = 0;
    for (std::size_t i = 0; i < s.size(); ++i) if (s[i] == ';') { parts.push_back(s.substr(st, i - st)); st = i + 1; }
    if (st != s.size() || parts.size() < 3) { error = "malformed witness"; return false; }
    out.script = parts[0];
    std::size_t k = 1;
    auto num = [&](std::size_t max, std::size_t& v) {
        if (k >= parts.size() || parts[k].size() != 1 || !std::isdigit(static_cast<unsigned char>(parts[k][0]))) return false;
        v = static_cast<std::size_t>(parts[k][0] - '0');
        ++k;
        return v <= max;
    };
    std::size_t ns = 0, np = 0;
    if (!num(kMaxSigs, ns)) { error = "bad sig count"; return false; }
    for (std::size_t j = 0; j < ns; ++j, ++k) {
        if (k >= parts.size()) { error = "malformed witness"; return false; }
        const auto c = parts[k].find(':');
        if (c == std::string::npos) { error = "malformed sig"; return false; }
        out.sigs.emplace_back(parts[k].substr(0, c), parts[k].substr(c + 1));
    }
    if (!num(kMaxPreimages, np)) { error = "bad preimage count"; return false; }
    for (std::size_t j = 0; j < np; ++j, ++k) {
        if (k >= parts.size() || !is_lower_hex(parts[k], 2 * kMaxPreimageBytes)) { error = "bad preimage"; return false; }
        out.preimages.push_back(parts[k]);
    }
    if (k != parts.size()) { error = "trailing witness items"; return false; }
    return true;
}

std::string sighash(const Transaction& tx) { return "ADD/sighash/v1|" + hash_transaction(tx); }

bool verify_input(const std::string& owner, const std::string& witness, const SpendContext& ctx, std::string& error) {
    if (!is_script_address(owner)) { error = "input is not a script output"; return false; }
    Witness w;
    if (!decode_witness(witness, w, error)) return false;
    if (script_address(w.script) != owner) { error = "witness script does not match output"; return false; }
    Node root;
    if (!parse(w.script, root, error)) return false;
    Eval ev{ctx, w,
            std::vector<bool>(w.sigs.size()), std::vector<bool>(w.sigs.size()), std::vector<bool>(w.sigs.size()),
            std::vector<bool>(w.preimages.size()), {}, {}};
    for (auto& p : w.preimages) ev.pre_hash.push_back(hash_preimage_hex(p));
    if (!ev.eval(root)) { error = "script not satisfied"; return false; }
    // Non-malleability / DoS: every witness item must be used.
    for (bool u : ev.sig_used) if (!u) { error = "unused witness signature"; return false; }
    for (bool u : ev.pre_used) if (!u) { error = "unused witness preimage"; return false; }
    return true;
}

std::string sign(const std::string& private_key_hex, const Transaction& tx) {
    return sign_message_hybrid(private_key_hex, sighash(tx));
}

} // namespace addition::script
