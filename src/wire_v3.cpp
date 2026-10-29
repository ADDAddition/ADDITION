#include "addition/wire_v3.hpp"

#include "addition/crypto.hpp"

namespace addition {
namespace wire {
namespace {

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void put_u8(std::string& o, std::uint8_t v) { o.push_back(static_cast<char>(v)); }
void put_u32(std::string& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put_u64(std::string& o, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) o.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
bool get_u8(const std::string& in, std::size_t& off, std::uint8_t& v) {
    if (off + 1 > in.size()) return false;
    v = static_cast<std::uint8_t>(in[off]);
    off += 1;
    return true;
}
bool get_u32(const std::string& in, std::size_t& off, std::uint32_t& v) {
    if (off + 4 > in.size()) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[off + i])) << (8 * i);
    off += 4;
    return true;
}
bool get_u64(const std::string& in, std::size_t& off, std::uint64_t& v) {
    if (off + 8 > in.size()) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(in[off + i])) << (8 * i);
    off += 8;
    return true;
}
bool is_lower_hex(const std::string& s, std::size_t from) {
    if (s.size() <= from || ((s.size() - from) % 2) != 0) return false;
    for (std::size_t i = from; i < s.size(); ++i) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
int nib(char c) { return (c >= '0' && c <= '9') ? c - '0' : c - 'a' + 10; }
void hex_to_raw(const std::string& s, std::size_t from, std::string& out) {
    out.reserve(out.size() + (s.size() - from) / 2);
    for (std::size_t i = from; i < s.size(); i += 2) out.push_back(static_cast<char>((nib(s[i]) << 4) | nib(s[i + 1])));
}
void raw_to_hex(const std::string& raw, std::size_t off, std::size_t n, std::string& out) {
    static const char* d = "0123456789abcdef";
    out.reserve(out.size() + n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        const auto b = static_cast<std::uint8_t>(raw[off + i]);
        out.push_back(d[b >> 4]);
        out.push_back(d[b & 15]);
    }
}
std::uint32_t checksum32(const std::string& data) {
    const auto h = sha3_512_bytes(data);
    return static_cast<std::uint32_t>(h[0]) | (static_cast<std::uint32_t>(h[1]) << 8) |
           (static_cast<std::uint32_t>(h[2]) << 16) | (static_cast<std::uint32_t>(h[3]) << 24);
}
bool check_tail(const std::string& bin, std::size_t off, std::string& error, const char* what) {
    if (off + 4 != bin.size()) {
        error = std::string("invalid ") + what + " checksum bounds";
        return false;
    }
    std::uint32_t expected = 0;
    get_u32(bin, off, expected);
    if (expected != checksum32(bin.substr(0, bin.size() - 4))) {
        error = std::string(what) + " checksum mismatch";
        return false;
    }
    return true;
}

} // namespace

std::string base64_encode(const std::string& bin) {
    std::string out;
    out.reserve(((bin.size() + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 2 < bin.size(); i += 3) {
        const std::uint32_t n = (static_cast<std::uint8_t>(bin[i]) << 16) |
                                (static_cast<std::uint8_t>(bin[i + 1]) << 8) | static_cast<std::uint8_t>(bin[i + 2]);
        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out.push_back(kB64[(n >> 6) & 63]);
        out.push_back(kB64[n & 63]);
    }
    const auto rem = bin.size() - i;
    if (rem == 1) {
        const std::uint32_t n = static_cast<std::uint8_t>(bin[i]) << 16;
        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out += "==";
    } else if (rem == 2) {
        const std::uint32_t n = (static_cast<std::uint8_t>(bin[i]) << 16) | (static_cast<std::uint8_t>(bin[i + 1]) << 8);
        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out.push_back(kB64[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

bool base64_decode(const std::string& text, std::string& bin) {
    bin.clear();
    if ((text.size() % 4) != 0) return false;
    struct Rev {
        int t[256];
        Rev() {
            for (int& r : t) r = -1;
            for (int k = 0; k < 64; ++k) t[static_cast<unsigned char>(kB64[k])] = k;
        }
    };
    static const Rev rv;  // thread-safe magic static
    const int* rev = rv.t;
    bin.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + k];
            if (c == '=') {
                if (i + 4 != text.size() || k < 2) return false;
                v[k] = 0;
                ++pad;
            } else {
                if (pad) return false;
                v[k] = rev[static_cast<unsigned char>(c)];
                if (v[k] < 0) return false;
            }
        }
        const std::uint32_t n = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        bin.push_back(static_cast<char>((n >> 16) & 0xFF));
        if (pad < 2) bin.push_back(static_cast<char>((n >> 8) & 0xFF));
        if (pad < 1) bin.push_back(static_cast<char>(n & 0xFF));
    }
    return true;
}

void write_tagged(std::string& out, const std::string& s) {
    std::string raw;
    if (s.rfind("pq=", 0) == 0 && is_lower_hex(s, 3)) {
        put_u8(out, 2);
        hex_to_raw(s, 3, raw);
    } else if (is_lower_hex(s, 0)) {
        put_u8(out, 1);
        hex_to_raw(s, 0, raw);
    } else {
        put_u8(out, 0);
        raw = s;
    }
    put_u32(out, static_cast<std::uint32_t>(raw.size()));
    out += raw;
}

bool read_tagged(const std::string& in, std::size_t& off, std::string& s) {
    std::uint8_t tag = 0;
    std::uint32_t n = 0;
    if (!get_u8(in, off, tag) || !get_u32(in, off, n)) return false;
    if (n > kMaxFieldBytes || off + n > in.size()) return false;
    s.clear();
    if (tag == 0) {
        s.assign(in, off, n);
    } else if (tag == 1 || tag == 2) {
        if (n == 0) return false;  // encoder never emits empty hex => keep 1:1 canonical
        if (tag == 2) s = "pq=";
        raw_to_hex(in, off, n, s);
    } else {
        return false;
    }
    off += n;
    return true;
}

constexpr std::uint8_t kTxSectionWitness = 1;

std::string encode_tx_v3(const Transaction& tx) {
    std::string b;
    b.reserve(64 + tx.signer_pubkey.size() / 2 + tx.signature.size() / 2 + tx.shielded.size() / 2 +
              tx.inputs.size() * 72 + tx.outputs.size() * 80);
    b.append("TXV3", 4);
    write_tagged(b, tx.signer);
    write_tagged(b, tx.signer_pubkey);  // empty => key resolved from on-chain registry
    write_tagged(b, tx.signature);
    put_u64(b, tx.fee);
    put_u64(b, tx.nonce);
    put_u32(b, static_cast<std::uint32_t>(tx.inputs.size()));
    for (const auto& in : tx.inputs) {
        write_tagged(b, in.previous_txid);
        put_u32(b, in.output_index);
    }
    put_u32(b, static_cast<std::uint32_t>(tx.outputs.size()));
    for (const auto& o : tx.outputs) {
        write_tagged(b, o.recipient);
        put_u64(b, o.amount);
    }
    write_tagged(b, tx.shielded);
    // Optional witness section (proto 4). Absent => byte-identical to the
    // proto-3 TXV3 encoding, so witness-free txs/weights are unchanged.
    bool has_witness = false;
    for (const auto& in : tx.inputs) has_witness |= !in.witness.empty();
    if (has_witness) {
        put_u8(b, kTxSectionWitness);
        for (const auto& in : tx.inputs) {
            put_u32(b, static_cast<std::uint32_t>(in.witness.size()));
            b += in.witness;
        }
    }
    put_u32(b, checksum32(b));
    return b;
}

bool decode_tx_v3(const std::string& bin, Transaction& tx, std::string& error) {
    tx = Transaction{};
    if (bin.size() < 8 || bin.compare(0, 4, "TXV3") != 0) {
        error = "invalid tx v3 magic";
        return false;
    }
    std::size_t off = 4;
    if (!read_tagged(bin, off, tx.signer) || !read_tagged(bin, off, tx.signer_pubkey) ||
        !read_tagged(bin, off, tx.signature) || !get_u64(bin, off, tx.fee) || !get_u64(bin, off, tx.nonce)) {
        error = "invalid tx v3 header";
        return false;
    }
    std::uint32_t n = 0;
    if (!get_u32(bin, off, n) || n > kMaxTxIo) {
        error = "invalid tx v3 inputs count";
        return false;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        TxInput in{};
        if (!read_tagged(bin, off, in.previous_txid) || !get_u32(bin, off, in.output_index)) {
            error = "invalid tx v3 input";
            return false;
        }
        tx.inputs.push_back(std::move(in));
    }
    if (!get_u32(bin, off, n) || n > kMaxTxIo) {
        error = "invalid tx v3 outputs count";
        return false;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        TxOutput o{};
        if (!read_tagged(bin, off, o.recipient) || !get_u64(bin, off, o.amount)) {
            error = "invalid tx v3 output";
            return false;
        }
        tx.outputs.push_back(std::move(o));
    }
    if (!read_tagged(bin, off, tx.shielded)) {
        error = "invalid tx v3 shielded";
        return false;
    }
    if (bin.size() >= 4 && off + 4 < bin.size()) {
        std::uint8_t sec = 0;
        if (!get_u8(bin, off, sec) || sec != kTxSectionWitness) {
            error = "unknown tx v3 section";
            return false;
        }
        bool any = false;
        for (auto& in : tx.inputs) {
            std::uint32_t wl = 0;
            if (!get_u32(bin, off, wl) || wl > kMaxFieldBytes || off + wl > bin.size()) {
                error = "invalid tx v3 witness";
                return false;
            }
            in.witness = bin.substr(off, wl);
            off += wl;
            any |= wl != 0;
        }
        if (!any) {
            error = "non-canonical empty tx v3 witness section";
            return false;
        }
    }
    if (!check_tail(bin, off, error, "tx v3")) return false;
    if (tx.outputs.empty()) {
        error = "tx payload has no outputs";
        return false;
    }
    return true;
}

void put_vote(std::string& b, const StakeVote& v) {
    write_tagged(b, v.ticket_id);
    put_u64(b, v.voted_height);
    write_tagged(b, v.voted_hash);
    put_u8(b, v.approve ? 1 : 0);
    write_tagged(b, v.signature);
}

bool get_vote(const std::string& in, std::size_t& off, StakeVote& v) {
    std::uint8_t a = 0;
    if (!read_tagged(in, off, v.ticket_id) || !get_u64(in, off, v.voted_height) ||
        !read_tagged(in, off, v.voted_hash) || !get_u8(in, off, a) || a > 1 ||
        !read_tagged(in, off, v.signature)) {
        return false;
    }
    v.approve = (a == 1);
    return true;
}

constexpr std::uint8_t kExtVotes = 1;
constexpr std::uint8_t kExtSlashes = 2;
constexpr std::uint32_t kMaxExtItems = 256;

std::string encode_block_v3(const Block& block) {
    std::string b;
    b.append("BLV3", 4);
    put_u64(b, block.header.height);
    write_tagged(b, block.header.previous_hash);
    put_u64(b, block.header.timestamp);
    put_u64(b, block.header.nonce);
    put_u64(b, block.header.difficulty_target);
    write_tagged(b, block.header.merkle_root);
    put_u32(b, static_cast<std::uint32_t>(block.transactions.size()));
    for (const auto& tx : block.transactions) {
        const auto t = encode_tx_v3(tx);
        put_u32(b, static_cast<std::uint32_t>(t.size()));
        b += t;
    }
    // Extension sections (count, then per section: u8 kind, u32 len, bytes).
    // kind 1 = stake votes, kind 2 = slash evidence (pairs). Blocks without
    // staking data keep ext count 0 (byte-identical to pre-staking BLV3).
    std::uint32_t ext = (block.votes.empty() ? 0u : 1u) + (block.slashes.empty() ? 0u : 1u);
    put_u32(b, ext);
    if (!block.votes.empty()) {
        std::string sec;
        put_u32(sec, static_cast<std::uint32_t>(block.votes.size()));
        for (const auto& v : block.votes) put_vote(sec, v);
        put_u8(b, kExtVotes);
        put_u32(b, static_cast<std::uint32_t>(sec.size()));
        b += sec;
    }
    if (!block.slashes.empty()) {
        std::string sec;
        put_u32(sec, static_cast<std::uint32_t>(block.slashes.size()));
        for (const auto& e : block.slashes) { put_vote(sec, e.a); put_vote(sec, e.b); }
        put_u8(b, kExtSlashes);
        put_u32(b, static_cast<std::uint32_t>(sec.size()));
        b += sec;
    }
    put_u32(b, checksum32(b));
    return b;
}

bool decode_block_v3(const std::string& bin, Block& block, std::string& error) {
    block = Block{};
    if (bin.size() < 8 || bin.compare(0, 4, "BLV3") != 0) {
        error = "invalid block v3 magic";
        return false;
    }
    std::size_t off = 4;
    if (!get_u64(bin, off, block.header.height) || !read_tagged(bin, off, block.header.previous_hash) ||
        !get_u64(bin, off, block.header.timestamp) || !get_u64(bin, off, block.header.nonce) ||
        !get_u64(bin, off, block.header.difficulty_target) || !read_tagged(bin, off, block.header.merkle_root)) {
        error = "invalid block v3 header";
        return false;
    }
    std::uint32_t n = 0;
    if (!get_u32(bin, off, n) || n > kMaxBlockTxs) {
        error = "invalid block v3 tx count";
        return false;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint32_t sz = 0;
        if (!get_u32(bin, off, sz) || off + sz > bin.size()) {
            error = "invalid block v3 tx bounds";
            return false;
        }
        Transaction tx{};
        if (!decode_tx_v3(bin.substr(off, sz), tx, error)) return false;
        off += sz;
        block.transactions.push_back(std::move(tx));
    }
    std::uint32_t ext = 0;
    if (!get_u32(bin, off, ext) || ext > 2) {
        error = "unsupported block v3 extension section";
        return false;
    }
    std::uint8_t last_kind = 0;
    for (std::uint32_t e = 0; e < ext; ++e) {
        std::uint8_t kind = 0;
        std::uint32_t len = 0;
        if (!get_u8(bin, off, kind) || !get_u32(bin, off, len) || off + len > bin.size() ||
            kind <= last_kind || (kind != kExtVotes && kind != kExtSlashes)) {
            error = "invalid block v3 extension";
            return false;
        }
        last_kind = kind;
        const std::string sec = bin.substr(off, len);
        off += len;
        std::size_t so = 0;
        std::uint32_t cnt = 0;
        if (!get_u32(sec, so, cnt) || cnt == 0 || cnt > kMaxExtItems) {
            error = "invalid block v3 extension count";
            return false;
        }
        for (std::uint32_t i = 0; i < cnt; ++i) {
            if (kind == kExtVotes) {
                StakeVote v{};
                if (!get_vote(sec, so, v)) { error = "invalid block v3 vote"; return false; }
                block.votes.push_back(std::move(v));
            } else {
                SlashEvidence ev{};
                if (!get_vote(sec, so, ev.a) || !get_vote(sec, so, ev.b)) { error = "invalid block v3 slash"; return false; }
                block.slashes.push_back(std::move(ev));
            }
        }
        if (so != sec.size()) { error = "invalid block v3 extension trailing bytes"; return false; }
    }
    if (!check_tail(bin, off, error, "block v3")) return false;
    if (block.transactions.empty() && block.header.height != 0) {
        error = "block payload has no transactions";
        return false;
    }
    return true;
}

} // namespace wire
} // namespace addition
