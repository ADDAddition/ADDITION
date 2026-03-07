#include "addition/decentralized_node.hpp"

#include "addition/block.hpp"
#include "addition/crypto.hpp"

#include <algorithm>
#include <exception>
#include <cctype>
#include <chrono>
#include <limits>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace addition {

namespace {

constexpr const char* kProtoVersion = "2";
constexpr const char* kNetworkId = "ADDITION_MAINNET_V1";
constexpr std::uint64_t kMsgSkewSec = 90;
constexpr std::size_t kMaxP2PLineBytes = 32768;
constexpr int kSocketTimeoutMs = 4000;
constexpr std::size_t kMaxPeerIdLen = 128;
constexpr std::size_t kMaxNonceLen = 128;
constexpr std::size_t kMaxPubKeyHexLen = 12000;
constexpr std::size_t kMaxSigHexLen = 40000;
constexpr std::size_t kMaxRotationIdLen = 128;

std::uint64_t now_seconds() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

#ifdef _WIN32
using SocketT = SOCKET;
constexpr SocketT kInvalidSocket = INVALID_SOCKET;
void close_socket(SocketT s) {
    if (s != INVALID_SOCKET) {
        closesocket(s);
    }
}
#else
using SocketT = int;
constexpr SocketT kInvalidSocket = -1;
void close_socket(SocketT s) {
    if (s >= 0) {
        close(s);
    }
}
#endif

bool parse_endpoint(const std::string& endpoint, std::string& ip, std::uint16_t& port) {
    const auto sep = endpoint.rfind(':');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= endpoint.size()) {
        return false;
    }
    ip = endpoint.substr(0, sep);
    const auto port_str = endpoint.substr(sep + 1);
    for (char c : port_str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    try {
        const auto p = std::stoul(port_str);
        if (p == 0 || p > 65535) {
            return false;
        }
        port = static_cast<std::uint16_t>(p);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool send_p2p_request(const std::string& endpoint, const std::string& request, std::string& response) {
    std::string ip;
    std::uint16_t port = 0;
    if (!parse_endpoint(endpoint, ip, port)) {
        return false;
    }

#ifdef _WIN32
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }
#endif

    SocketT sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

#ifdef _WIN32
    DWORD timeout_ms = static_cast<DWORD>(kSocketTimeoutMs);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval tv{};
    tv.tv_sec = kSocketTimeoutMs / 1000;
    tv.tv_usec = (kSocketTimeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        close_socket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    const std::string payload = request + "\n";
        if (payload.size() > kMaxP2PLineBytes) {
        close_socket(sock);
    #ifdef _WIN32
        WSACleanup();
    #endif
        return false;
        }
#ifdef _WIN32
    const auto sent = send(sock, payload.c_str(), static_cast<int>(payload.size()), 0);
#else
    const auto sent = send(sock, payload.c_str(), payload.size(), 0);
#endif
    if (sent <= 0) {
        close_socket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    char buffer[kMaxP2PLineBytes + 1]{};
#ifdef _WIN32
    const int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
#else
    const int received = static_cast<int>(recv(sock, buffer, sizeof(buffer) - 1, 0));
#endif
    if (received <= 0) {
        close_socket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

        response.assign(buffer, buffer + received);
        if (response.size() > kMaxP2PLineBytes) {
        close_socket(sock);
    #ifdef _WIN32
        WSACleanup();
    #endif
        return false;
        }
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
        response.pop_back();
    }

    close_socket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return true;
}

void write_u32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

void write_u64(std::string& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

bool read_u32(const std::string& in, std::size_t& off, std::uint32_t& v) {
    if (off + 4 > in.size()) {
        return false;
    }
    v = static_cast<std::uint32_t>(static_cast<unsigned char>(in[off])) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(in[off + 3])) << 24);
    off += 4;
    return true;
}

bool read_u64(const std::string& in, std::size_t& off, std::uint64_t& v) {
    if (off + 8 > in.size()) {
        return false;
    }
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= (static_cast<std::uint64_t>(static_cast<unsigned char>(in[off + i])) << (8 * i));
    }
    off += 8;
    return true;
}

bool write_string(std::string& out, const std::string& s) {
    if (s.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    write_u32(out, static_cast<std::uint32_t>(s.size()));
    out.append(s);
    return true;
}

bool read_string(const std::string& in, std::size_t& off, std::string& s) {
    std::uint32_t n = 0;
    if (!read_u32(in, off, n)) {
        return false;
    }
    if (off + n > in.size()) {
        return false;
    }
    s.assign(in.data() + off, in.data() + off + n);
    off += n;
    return true;
}

std::string to_hex(const std::string& bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        out.push_back(kHex[(b >> 4) & 0x0F]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

std::uint32_t payload_checksum32(const std::string& data) {
    const auto h = sha3_512_bytes(data);
    return static_cast<std::uint32_t>(h[0]) |
           (static_cast<std::uint32_t>(h[1]) << 8) |
           (static_cast<std::uint32_t>(h[2]) << 16) |
           (static_cast<std::uint32_t>(h[3]) << 24);
}

bool from_hex(const std::string& hex, std::string& out) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + (c - 'A');
        }
        return -1;
    };

    if ((hex.size() % 2) != 0) {
        return false;
    }
    out.clear();
    out.reserve(hex.size() / 2);

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

bool is_reasonable_field_size(const std::string& value, std::size_t max_len) {
    return !value.empty() && value.size() <= max_len;
}

} // namespace

DecentralizedNode::DecentralizedNode(std::string self_id,
                                                                         std::string node_public_key,
                                                                         std::string node_private_key,
                                     Chain& chain,
                                     Mempool& mempool,
                                     PeerNetwork& peers,
                                     ConsensusEngine& consensus)
    : self_id_(std::move(self_id)),
            node_public_key_(std::move(node_public_key)),
            node_private_key_(std::move(node_private_key)),
      chain_(chain),
      mempool_(mempool),
      peers_(peers),
      consensus_(consensus) {}

const std::string& DecentralizedNode::node_public_key() const {
    return node_public_key_;
}

const std::string& DecentralizedNode::node_private_key() const {
    return node_private_key_;
}

std::vector<std::pair<std::string, std::string>> DecentralizedNode::peer_pins() const {
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(pinned_peer_pubkeys_.size());
    for (const auto& [peer, pk] : pinned_peer_pubkeys_) {
        out.emplace_back(peer, pk);
    }
    return out;
}

void DecentralizedNode::load_peer_pins(const std::vector<std::pair<std::string, std::string>>& pins) {
    pinned_peer_pubkeys_.clear();
    for (const auto& [peer, pk] : pins) {
        if (!peer.empty() && !pk.empty()) {
            pinned_peer_pubkeys_[peer] = pk;
        }
    }
}

bool DecentralizedNode::relay_rotation_messages_to_peer(const std::string& peer, std::string& error) {
    auto& relayed = relayed_message_ids_by_peer_[peer];

    for (const auto& msg : outbound_) {
        const bool is_rotation = (msg.rfind("IDROTATE|", 0) == 0);
        const bool is_vote = (msg.rfind("IDVOTE|", 0) == 0);
        if (!is_rotation && !is_vote) {
            continue;
        }

        const auto msg_id = to_hex(sha3_512_bytes(msg));
        if (!relayed.insert(msg_id).second) {
            continue;
        }

        std::string resp;
        const std::string req = self_id_ + " " + msg;
        if (!send_p2p_request(peer, req, resp)) {
            error = "relay transport failed";
            return false;
        }
        if (resp.rfind("ok", 0) != 0) {
            error = "relay rejected";
            return false;
        }
    }

    return true;
}

bool DecentralizedNode::allow_peer_message(const std::string& peer, bool expensive, std::string& error) {
    constexpr std::uint64_t kGenericWindowSec = 10;
    constexpr std::uint64_t kExpensiveWindowSec = 10;
    constexpr std::size_t kGenericMax = 120;
    constexpr std::size_t kExpensiveMax = 24;

    const auto now = now_seconds();
    auto& bucket = rate_limits_[peer];

    while (!bucket.generic_hits.empty() && bucket.generic_hits.front() + kGenericWindowSec < now) {
        bucket.generic_hits.pop_front();
    }
    while (!bucket.expensive_hits.empty() && bucket.expensive_hits.front() + kExpensiveWindowSec < now) {
        bucket.expensive_hits.pop_front();
    }

    if (bucket.generic_hits.size() >= kGenericMax) {
        error = "rate limit exceeded (generic)";
        return false;
    }
    bucket.generic_hits.push_back(now);

    if (expensive) {
        if (bucket.expensive_hits.size() >= kExpensiveMax) {
            error = "rate limit exceeded (expensive)";
            return false;
        }
        bucket.expensive_hits.push_back(now);
    }

    return true;
}

bool DecentralizedNode::propose_identity_rotation(const std::string& new_public_key,
                                                  const std::string& new_private_key,
                                                  std::uint64_t grace_seconds,
                                                  std::string& error) {
    if (new_public_key.empty() || new_private_key.empty()) {
        error = "new identity keys required";
        return false;
    }
    if (grace_seconds < 30) {
        error = "grace too short";
        return false;
    }

    const auto now = now_seconds();
    const std::string rotation_body = std::string("rotate|") + node_public_key_ + "|" + new_public_key + "|" +
                                      std::to_string(now + grace_seconds);
    std::string proof;
    try {
        proof = sign_message_hybrid(node_private_key_, rotation_body);
    } catch (const std::exception&) {
        error = "rotation proof signing failed";
        return false;
    }

    PendingRotation r{};
    r.new_public_key = new_public_key;
    r.new_private_key = new_private_key;
    r.effective_after = now + grace_seconds;
    r.proof = proof;
    r.rotation_id = to_hex(sha3_512_bytes(rotation_body));
    r.votes.insert(self_id_);
    r.vote_signatures.insert(proof);

    std::ostringstream ann;
    ann << "IDROTATE|" << r.rotation_id << '|' << node_public_key_ << '|' << r.new_public_key
        << '|' << r.effective_after << '|' << r.proof;
    outbound_.push_back(ann.str());

    pending_rotation_ = std::move(r);
    return true;
}

bool DecentralizedNode::vote_identity_rotation(const std::string& peer_id, std::string& error) {
    if (!pending_rotation_.has_value()) {
        error = "no pending rotation";
        return false;
    }
    if (peer_id.empty()) {
        error = "peer id required";
        return false;
    }
    pending_rotation_->votes.insert(peer_id);
    return true;
}

bool DecentralizedNode::broadcast_identity_rotation_vote(std::string& error) {
    if (!pending_rotation_.has_value()) {
        error = "no pending rotation";
        return false;
    }

    const std::string vote_body = std::string("idvote|") + pending_rotation_->rotation_id + "|" + self_id_;
    std::string sig;
    try {
        sig = sign_message_hybrid(node_private_key_, vote_body);
    } catch (const std::exception&) {
        error = "vote signing failed";
        return false;
    }

    if (pending_rotation_->vote_signatures.insert(sig).second) {
        pending_rotation_->votes.insert(self_id_);
    }

    std::ostringstream msg;
    msg << "IDVOTE|" << pending_rotation_->rotation_id << '|' << self_id_ << '|' << node_public_key_ << '|' << sig;
    outbound_.push_back(msg.str());
    return true;
}

bool DecentralizedNode::commit_identity_rotation(std::string& error) {
    if (!pending_rotation_.has_value()) {
        error = "no pending rotation";
        return false;
    }
    const auto now = now_seconds();
    if (now < pending_rotation_->effective_after) {
        error = "rotation grace period not elapsed";
        return false;
    }

    const auto total = peers_.peer_count() + 1;
    const auto needed = (2 * total) / 3 + 1;
    if (pending_rotation_->votes.size() < needed) {
        error = "rotation quorum not reached";
        return false;
    }

    node_public_key_ = pending_rotation_->new_public_key;
    node_private_key_ = pending_rotation_->new_private_key;
    pending_rotation_.reset();
    return true;
}

std::string DecentralizedNode::identity_rotation_status() const {
    if (!pending_rotation_.has_value()) {
        return "none";
    }

    std::ostringstream out;
    out << "pending"
        << " id=" << pending_rotation_->rotation_id.substr(0, 16)
        << " effective_after=" << pending_rotation_->effective_after
        << " votes=" << pending_rotation_->votes.size();
    return out.str();
}

bool DecentralizedNode::submit_transaction(const Transaction& tx, std::string& error) {
    if (!chain_.validate_transaction(tx, error)) {
        return false;
    }

    const auto txid = hash_transaction(tx);
    if (seen_txids_.insert(txid).second) {
        if (!mempool_.submit(tx)) {
            seen_txids_.erase(txid);
            error = "duplicate mempool tx";
            return false;
        }
        outbound_.push_back(encode_tx_gossip(tx));
    }
    return true;
}

bool DecentralizedNode::ingest_block(const Block& block, std::string& error) {
    const auto bh = hash_block_header(block.header);
    if (!seen_block_hashes_.insert(bh).second) {
        return true;
    }

    if (!chain_.add_block(block, error)) {
        return false;
    }

    consensus_.submit_vote(self_id_, block.header.height, bh);
    outbound_.push_back(encode_block_announce(block));
    return true;
}

bool DecentralizedNode::ingest_block_payload(const std::string& payload, std::string& error) {
    Block block{};
    if (!decode_block_payload(payload, block, error)) {
        return false;
    }
    return ingest_block(block, error);
}

bool DecentralizedNode::get_block_payload(std::uint64_t height,
                                          std::string& payload,
                                          std::string& error) const {
    const auto& all = chain_.blocks();
    if (height >= all.size()) {
        error = "block height out of range";
        return false;
    }
    payload = encode_block_payload(all[static_cast<std::size_t>(height)]);
    return true;
}

bool DecentralizedNode::sync_once(std::string& error) {
    auto peers = peers_.peers();
    if (peers.empty()) {
        return true;
    }

    const auto local_work = chain_.cumulative_work();
    for (const auto& peer : peers) {
        if (peers_.is_banned(peer)) {
            continue;
        }

        std::ostringstream hello_req;
        const auto nonce_seed = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto hello_ts = now_seconds();
        const std::string hello_nonce = "sync-" + std::to_string(nonce_seed);
        const std::string hello_body = std::string(kProtoVersion) + "|" + kNetworkId + "|" +
                                       std::to_string(hello_ts) + "|" + hello_nonce + "|" +
                                       node_public_key_;
        std::string hello_sig;
        try {
            hello_sig = sign_message_hybrid(node_private_key_, hello_body);
        } catch (const std::exception&) {
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            continue;
        }
        hello_req << self_id_ << " HELLO|" << kProtoVersion << '|' << kNetworkId
                  << '|' << hello_ts << '|' << hello_nonce << '|' << node_public_key_ << '|' << hello_sig;
        std::string hello_resp;
        if (!send_p2p_request(peer, hello_req.str(), hello_resp)) {
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            continue;
        }

        if (hello_resp.rfind("ok:HELLO_ACK|", 0) != 0) {
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            continue;
        }

        {
            const auto ack = hello_resp.substr(13);
            std::istringstream as(ack);
            std::string v;
            std::string n;
            std::string ts;
            std::string nonce;
            std::string peer_pk;
            std::string sig;
            if (!std::getline(as, v, '|') ||
                !std::getline(as, n, '|') ||
                !std::getline(as, ts, '|') ||
                !std::getline(as, nonce, '|') ||
                !std::getline(as, peer_pk, '|') ||
                !std::getline(as, sig)) {
                peers_.mark_peer_bad(peer);
                handshake_ok_[peer] = false;
                continue;
            }

            if (v != kProtoVersion || n != kNetworkId || nonce != hello_nonce || peer_pk.empty() || sig.empty()) {
                peers_.mark_peer_bad(peer);
                handshake_ok_[peer] = false;
                continue;
            }

            if (!is_reasonable_field_size(nonce, kMaxNonceLen) ||
                !is_reasonable_field_size(peer_pk, kMaxPubKeyHexLen) ||
                !is_reasonable_field_size(sig, kMaxSigHexLen)) {
                peers_.mark_peer_bad(peer);
                handshake_ok_[peer] = false;
                continue;
            }

            const auto pit = pinned_peer_pubkeys_.find(peer);
            if (pit != pinned_peer_pubkeys_.end() && pit->second != peer_pk) {
                peers_.mark_peer_bad(peer);
                handshake_ok_[peer] = false;
                continue;
            }

            const std::string ack_body = std::string(kProtoVersion) + "|" + kNetworkId + "|" + ts + "|" +
                                         nonce + "|" + peer_pk;
            if (!verify_message_signature_hybrid(peer_pk, ack_body, sig)) {
                peers_.mark_peer_bad(peer);
                handshake_ok_[peer] = false;
                continue;
            }

            peer_pubkeys_[peer] = peer_pk;
            pinned_peer_pubkeys_.emplace(peer, peer_pk);
        }
        handshake_ok_[peer] = true;

        {
            std::string relay_err;
            if (!relay_rotation_messages_to_peer(peer, relay_err)) {
                peers_.mark_peer_bad(peer);
                continue;
            }
        }

        std::string remote_work_resp;
        if (!send_p2p_request(peer, self_id_ + " REQWORK|", remote_work_resp)) {
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (remote_work_resp.rfind("ok:HAVEWORK|", 0) != 0) {
            peers_.mark_peer_bad(peer);
            continue;
        }

        std::uint64_t remote_work = 0;
        try {
            remote_work = std::stoull(remote_work_resp.substr(12));
        } catch (const std::exception&) {
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (remote_work <= local_work) {
            peers_.mark_peer_good(peer);
            continue;
        }

        std::string remote_height_resp;
        if (!send_p2p_request(peer, self_id_ + " REQH|", remote_height_resp)) {
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (remote_height_resp.rfind("ok:HAVEH|", 0) != 0) {
            peers_.mark_peer_bad(peer);
            continue;
        }

        std::uint64_t remote_h = 0;
        try {
            remote_h = std::stoull(remote_height_resp.substr(9));
        } catch (const std::exception&) {
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (remote_h <= chain_.height()) {
            peers_.mark_peer_good(peer);
            continue;
        }

        std::ostringstream inv_req;
        const auto inv_start = (chain_.height() > 0) ? (chain_.height() - 1) : 0;
        inv_req << self_id_ << " REQINV|" << inv_start << "|24";

        std::string inv_resp;
        if (!send_p2p_request(peer, inv_req.str(), inv_resp)) {
            peers_.mark_peer_bad(peer);
            continue;
        }
        if (inv_resp.rfind("ok:INV|", 0) != 0) {
            peers_.mark_peer_bad(peer);
            continue;
        }

        std::vector<std::pair<std::uint64_t, std::string>> inventory;
        {
            const auto body = inv_resp.substr(7);
            std::istringstream iss(body);
            std::string item;
            while (std::getline(iss, item, ',')) {
                if (item.empty()) {
                    continue;
                }
                const auto sep = item.find(':');
                if (sep == std::string::npos) {
                    continue;
                }
                try {
                    auto h = std::stoull(item.substr(0, sep));
                    auto hash = item.substr(sep + 1);
                    if (!hash.empty()) {
                        inventory.emplace_back(h, std::move(hash));
                    }
                } catch (const std::exception&) {
                    continue;
                }
            }
        }

        bool reorg_needed = false;
        for (const auto& [h, hash] : inventory) {
            if (h > chain_.height()) {
                continue;
            }
            const auto local = chain_.block_at(h);
            if (!local.has_value()) {
                continue;
            }
            if (hash_block_header(local->header) != hash) {
                reorg_needed = true;
                break;
            }
        }

        if (!reorg_needed) {
            for (const auto& [h, hash] : inventory) {
                if (h <= chain_.height()) {
                    continue;
                }
                if (chain_.has_block_hash(hash)) {
                    continue;
                }

                std::ostringstream req;
                req << self_id_ << " REQBLK|" << h;
                std::string block_resp;
                if (!send_p2p_request(peer, req.str(), block_resp)) {
                    peers_.mark_peer_bad(peer);
                    break;
                }
                if (block_resp.rfind("ok:BLKDATA|", 0) != 0) {
                    peers_.mark_peer_bad(peer);
                    break;
                }

                std::string ingest_error;
                if (!ingest_block_payload(block_resp.substr(11), ingest_error)) {
                    peers_.mark_peer_bad(peer);
                    error = ingest_error;
                    break;
                }
                peers_.mark_peer_good(peer);
            }
            continue;
        }

        std::vector<Block> candidate;
        candidate.reserve(static_cast<std::size_t>(remote_h + 1));

        for (std::uint64_t h = 0; h <= remote_h; ++h) {
            std::ostringstream req;
            req << self_id_ << " REQBLK|" << h;

            std::string block_resp;
            if (!send_p2p_request(peer, req.str(), block_resp)) {
                peers_.mark_peer_bad(peer);
                break;
            }

            if (block_resp.rfind("ok:BLKDATA|", 0) != 0) {
                peers_.mark_peer_bad(peer);
                break;
            }

            Block b{};
            std::string decode_err;
            if (!decode_block_payload(block_resp.substr(11), b, decode_err)) {
                peers_.mark_peer_bad(peer);
                break;
            }
            candidate.push_back(std::move(b));
        }

        if (candidate.empty()) {
            peers_.mark_peer_bad(peer);
            continue;
        }

        std::string reorg_error;
        if (!chain_.replace_with_chain(candidate, reorg_error)) {
            peers_.mark_peer_bad(peer);
            error = reorg_error;
            continue;
        }

        for (const auto& b : candidate) {
            std::string ingest_error;
            const auto bh = hash_block_header(b.header);
            seen_block_hashes_.insert(bh);
            for (const auto& tx : b.transactions) {
                seen_txids_.insert(hash_transaction(tx));
            }
        }

        peers_.mark_peer_good(peer);
    }

    return true;
}

std::vector<std::string> DecentralizedNode::pull_outbound_messages() {
    auto out = outbound_;
    outbound_.clear();
    return out;
}

bool DecentralizedNode::handle_inbound_message(const std::string& peer,
                                               const std::string& message,
                                               std::string& error) {
    if (!peers_.has_peer(peer)) {
        if (!peers_.add_peer(peer)) {
            error = "unknown peer";
            return false;
        }
    }

    if (peers_.is_banned(peer)) {
        error = "peer banned";
        return false;
    }

    const bool expensive =
        (message.rfind("REQBLK|", 0) == 0) ||
        (message.rfind("REQINV|", 0) == 0) ||
        (message.rfind("BLKDATA|", 0) == 0) ||
        (message.rfind("IDROTATE|", 0) == 0) ||
        (message.rfind("IDVOTE|", 0) == 0);
    if (!allow_peer_message(peer, expensive, error)) {
        peers_.mark_peer_bad(peer);
        return false;
    }

    if (message.rfind("HELLO|", 0) == 0) {
        const auto body = message.substr(6);
        std::istringstream hs(body);
        std::string version;
        std::string network;
        std::string ts_s;
        std::string nonce;
        std::string peer_pk;
        std::string peer_sig;
        if (!std::getline(hs, version, '|') ||
            !std::getline(hs, network, '|') ||
            !std::getline(hs, ts_s, '|') ||
            !std::getline(hs, nonce, '|') ||
            !std::getline(hs, peer_pk, '|') ||
            !std::getline(hs, peer_sig)) {
            error = "invalid hello";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        if (version != kProtoVersion || network != kNetworkId) {
            error = "handshake mismatch";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        std::uint64_t ts = 0;
        try {
            ts = std::stoull(ts_s);
        } catch (const std::exception&) {
            error = "invalid hello timestamp";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        const auto now = now_seconds();
        if (ts + kMsgSkewSec < now || now + kMsgSkewSec < ts) {
            error = "hello timestamp skew";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        if (nonce.empty()) {
            error = "missing hello nonce";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        if (peer_pk.empty() || peer_sig.empty()) {
            error = "missing hello signature fields";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        if (!is_reasonable_field_size(nonce, kMaxNonceLen) ||
            !is_reasonable_field_size(peer_pk, kMaxPubKeyHexLen) ||
            !is_reasonable_field_size(peer_sig, kMaxSigHexLen)) {
            error = "hello field too large";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        const auto pit = pinned_peer_pubkeys_.find(peer);
        if (pit != pinned_peer_pubkeys_.end() && pit->second != peer_pk) {
            error = "peer pubkey pin mismatch";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        const std::string hello_body = version + "|" + network + "|" + ts_s + "|" + nonce + "|" + peer_pk;
        if (!verify_message_signature_hybrid(peer_pk, hello_body, peer_sig)) {
            error = "invalid hello signature";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        auto& seen = seen_nonces_[peer];
        if (!seen.insert(nonce).second) {
            error = "replayed hello nonce";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }
        if (seen.size() > 4096) {
            seen.clear();
            seen.insert(nonce);
        }

        peer_pubkeys_[peer] = peer_pk;
        pinned_peer_pubkeys_.emplace(peer, peer_pk);

        const std::string ack_body = std::string(kProtoVersion) + "|" + kNetworkId + "|" +
                                     std::to_string(now) + "|" + nonce + "|" + node_public_key_;
        std::string ack_sig;
        try {
            ack_sig = sign_message_hybrid(node_private_key_, ack_body);
        } catch (const std::exception&) {
            error = "ack signing failed";
            peers_.mark_peer_bad(peer);
            handshake_ok_[peer] = false;
            return false;
        }

        std::ostringstream ack;
        ack << "HELLO_ACK|" << kProtoVersion << '|' << kNetworkId << '|' << now << '|' << nonce
            << '|' << node_public_key_ << '|' << ack_sig;
        outbound_.push_back(ack.str());
        peers_.mark_peer_good(peer);
        handshake_ok_[peer] = true;
        return true;
    }

    const auto hs = handshake_ok_.find(peer);
    if (hs == handshake_ok_.end() || !hs->second) {
        error = "handshake required";
        peers_.mark_peer_bad(peer);
        return false;
    }

    if (message.rfind("TX|", 0) == 0) {
        Transaction tx{};
        if (!decode_tx_gossip(message.substr(3), tx, error)) {
            peers_.mark_peer_bad(peer);
            return false;
        }
        const auto ok = submit_transaction(tx, error);
        if (ok) {
            peers_.mark_peer_good(peer);
        } else {
            peers_.mark_peer_bad(peer);
        }
        return ok;
    }

    if (message.rfind("BLK|", 0) == 0) {
        Block b{};
        if (!decode_block_announce(message.substr(4), b, error)) {
            peers_.mark_peer_bad(peer);
            return false;
        }
        const auto ok = ingest_block(b, error);
        if (ok) {
            peers_.mark_peer_good(peer);
        } else {
            peers_.mark_peer_bad(peer);
        }
        return ok;
    }

    if (message.rfind("REQH|", 0) == 0) {
        std::ostringstream out;
        out << "HAVEH|" << chain_.height();
        outbound_.push_back(out.str());
        peers_.mark_peer_good(peer);
        return true;
    }

    if (message.rfind("REQWORK|", 0) == 0) {
        std::ostringstream out;
        out << "HAVEWORK|" << chain_.cumulative_work();
        outbound_.push_back(out.str());
        peers_.mark_peer_good(peer);
        return true;
    }

    if (message.rfind("REQBLK|", 0) == 0) {
        const auto body = message.substr(7);
        std::uint64_t h = 0;
        try {
            h = std::stoull(body);
        } catch (const std::exception&) {
            error = "invalid block request height";
            peers_.mark_peer_bad(peer);
            return false;
        }

        std::string payload;
        if (!get_block_payload(h, payload, error)) {
            peers_.mark_peer_bad(peer);
            return false;
        }

        outbound_.push_back("BLKDATA|" + payload);
        peers_.mark_peer_good(peer);
        return true;
    }

    if (message.rfind("REQINV|", 0) == 0) {
        const auto body = message.substr(7);
        const auto sep = body.find('|');
        if (sep == std::string::npos) {
            error = "invalid inventory request";
            peers_.mark_peer_bad(peer);
            return false;
        }

        std::uint64_t start = 0;
        std::uint64_t limit = 0;
        try {
            start = std::stoull(body.substr(0, sep));
            limit = std::stoull(body.substr(sep + 1));
        } catch (const std::exception&) {
            error = "invalid inventory request values";
            peers_.mark_peer_bad(peer);
            return false;
        }

        if (limit == 0) {
            limit = 1;
        }
        if (limit > 64) {
            limit = 64;
        }

        const auto& all = chain_.blocks();
        std::ostringstream out;
        out << "INV|";

        bool first = true;
        const auto end = std::min<std::uint64_t>(static_cast<std::uint64_t>(all.size()), start + limit);
        for (std::uint64_t h = start; h < end; ++h) {
            const auto hash = hash_block_header(all[static_cast<std::size_t>(h)].header);
            if (!first) {
                out << ',';
            }
            first = false;
            out << h << ':' << hash;
        }

        outbound_.push_back(out.str());
        peers_.mark_peer_good(peer);
        return true;
    }

    if (message.rfind("IDROTATE|", 0) == 0) {
        const auto body = message.substr(9);
        std::istringstream rs(body);
        std::string rid;
        std::string old_pk;
        std::string new_pk;
        std::string eff_s;
        std::string proof;
        if (!std::getline(rs, rid, '|') ||
            !std::getline(rs, old_pk, '|') ||
            !std::getline(rs, new_pk, '|') ||
            !std::getline(rs, eff_s, '|') ||
            !std::getline(rs, proof)) {
            error = "invalid rotation announce";
            peers_.mark_peer_bad(peer);
            return false;
        }

        if (!is_reasonable_field_size(rid, kMaxRotationIdLen) ||
            !is_reasonable_field_size(old_pk, kMaxPubKeyHexLen) ||
            !is_reasonable_field_size(new_pk, kMaxPubKeyHexLen) ||
            !is_reasonable_field_size(proof, kMaxSigHexLen)) {
            error = "rotation field too large";
            peers_.mark_peer_bad(peer);
            return false;
        }

        const std::string sign_body = std::string("rotate|") + old_pk + "|" + new_pk + "|" + eff_s;
        if (!verify_message_signature_hybrid(old_pk, sign_body, proof)) {
            error = "invalid rotation proof";
            peers_.mark_peer_bad(peer);
            return false;
        }

        std::uint64_t eff = 0;
        try {
            eff = std::stoull(eff_s);
        } catch (const std::exception&) {
            error = "invalid rotation effective time";
            peers_.mark_peer_bad(peer);
            return false;
        }

        PendingRotation r{};
        r.rotation_id = rid;
        r.new_public_key = new_pk;
        r.new_private_key.clear();
        r.effective_after = eff;
        r.proof = proof;

        if (!seen_rotation_ids_.insert(rid).second) {
            peers_.mark_peer_good(peer);
            return true;
        }

        pending_rotation_ = std::move(r);
        outbound_.push_back(message);
        peers_.mark_peer_good(peer);
        return true;
    }

    if (message.rfind("IDVOTE|", 0) == 0) {
        if (!pending_rotation_.has_value()) {
            error = "no pending rotation for vote";
            peers_.mark_peer_bad(peer);
            return false;
        }

        const auto body = message.substr(7);
        std::istringstream vs(body);
        std::string rid;
        std::string voter_id;
        std::string voter_pk;
        std::string sig;
        if (!std::getline(vs, rid, '|') ||
            !std::getline(vs, voter_id, '|') ||
            !std::getline(vs, voter_pk, '|') ||
            !std::getline(vs, sig)) {
            error = "invalid vote message";
            peers_.mark_peer_bad(peer);
            return false;
        }

        if (!is_reasonable_field_size(rid, kMaxRotationIdLen) ||
            !is_reasonable_field_size(voter_id, kMaxPeerIdLen) ||
            !is_reasonable_field_size(voter_pk, kMaxPubKeyHexLen) ||
            !is_reasonable_field_size(sig, kMaxSigHexLen)) {
            error = "vote field too large";
            peers_.mark_peer_bad(peer);
            return false;
        }

        if (rid != pending_rotation_->rotation_id) {
            error = "vote rotation id mismatch";
            peers_.mark_peer_bad(peer);
            return false;
        }

        const std::string vote_body = std::string("idvote|") + rid + "|" + voter_id;
        if (!verify_message_signature_hybrid(voter_pk, vote_body, sig)) {
            error = "invalid vote signature";
            peers_.mark_peer_bad(peer);
            return false;
        }

        if (pending_rotation_->vote_signatures.insert(sig).second) {
            pending_rotation_->votes.insert(voter_id);
        }

        if (seen_vote_signatures_.insert(sig).second) {
            outbound_.push_back(message);
        }

        peers_.mark_peer_good(peer);
        return true;
    }

    if (message.rfind("BLKDATA|", 0) == 0) {
        const auto ok = ingest_block_payload(message.substr(8), error);
        if (ok) {
            peers_.mark_peer_good(peer);
        } else {
            peers_.mark_peer_bad(peer);
        }
        return ok;
    }

    error = "unknown message type";
    peers_.mark_peer_bad(peer);
    return false;
}

std::string DecentralizedNode::encode_tx_payload(const Transaction& tx) const {
    std::string bin;
    bin.reserve(256);
    bin.append("TXB2", 4);

    write_string(bin, tx.signer);
    write_string(bin, tx.signer_pubkey);
    write_string(bin, tx.signature);
    write_u64(bin, tx.fee);
    write_u64(bin, tx.nonce);

    write_u32(bin, static_cast<std::uint32_t>(tx.inputs.size()));
    for (const auto& in : tx.inputs) {
        write_string(bin, in.previous_txid);
        write_u32(bin, in.output_index);
    }

    write_u32(bin, static_cast<std::uint32_t>(tx.outputs.size()));
    for (const auto& out : tx.outputs) {
        write_string(bin, out.recipient);
        write_u64(bin, out.amount);
    }

    write_u32(bin, payload_checksum32(bin));

    return to_hex(bin);
}

bool DecentralizedNode::decode_tx_payload(const std::string& payload,
                                          Transaction& tx,
                                          std::string& error) const {
    tx = Transaction{};

    std::string bin;
    if (!from_hex(payload, bin)) {
        error = "invalid tx payload hex";
        return false;
    }
    if (bin.size() < 4 || (bin.compare(0, 4, "TXB1") != 0 && bin.compare(0, 4, "TXB2") != 0)) {
        error = "invalid tx payload magic";
        return false;
    }
    const bool has_checksum = (bin.compare(0, 4, "TXB2") == 0);

    std::size_t off = 4;
    if (!read_string(bin, off, tx.signer) ||
        !read_string(bin, off, tx.signer_pubkey) ||
        !read_string(bin, off, tx.signature)) {
        error = "invalid tx payload header";
        return false;
    }
    if (!read_u64(bin, off, tx.fee) || !read_u64(bin, off, tx.nonce)) {
        error = "invalid tx payload numeric fields";
        return false;
    }

    std::uint32_t in_count = 0;
    if (!read_u32(bin, off, in_count)) {
        error = "invalid tx payload inputs count";
        return false;
    }
    tx.inputs.reserve(in_count);
    for (std::uint32_t i = 0; i < in_count; ++i) {
        TxInput in{};
        if (!read_string(bin, off, in.previous_txid) || !read_u32(bin, off, in.output_index)) {
            error = "invalid tx payload input";
            return false;
        }
        tx.inputs.push_back(std::move(in));
    }

    std::uint32_t out_count = 0;
    if (!read_u32(bin, off, out_count)) {
        error = "invalid tx payload outputs count";
        return false;
    }
    tx.outputs.reserve(out_count);
    for (std::uint32_t i = 0; i < out_count; ++i) {
        TxOutput out{};
        if (!read_string(bin, off, out.recipient) || !read_u64(bin, off, out.amount)) {
            error = "invalid tx payload output";
            return false;
        }
        tx.outputs.push_back(std::move(out));
    }

    if (has_checksum) {
        if (off + 4 != bin.size()) {
            error = "invalid tx payload checksum bounds";
            return false;
        }
        std::uint32_t expected = 0;
        if (!read_u32(bin, off, expected)) {
            error = "invalid tx payload checksum";
            return false;
        }
        const auto actual = payload_checksum32(bin.substr(0, bin.size() - 4));
        if (expected != actual) {
            error = "tx payload checksum mismatch";
            return false;
        }
    } else if (off != bin.size()) {
        error = "invalid tx payload trailing bytes";
        return false;
    }

    if (tx.outputs.empty()) {
        error = "tx payload has no outputs";
        return false;
    }
    return true;
}

std::string DecentralizedNode::encode_block_payload(const Block& block) const {
    std::string bin;
    bin.reserve(1024);
    bin.append("BLB2", 4);

    write_u64(bin, block.header.height);
    write_string(bin, block.header.previous_hash);
    write_u64(bin, block.header.timestamp);
    write_u64(bin, block.header.nonce);
    write_u64(bin, block.header.difficulty_target);
    write_string(bin, block.header.merkle_root);

    write_u32(bin, static_cast<std::uint32_t>(block.transactions.size()));
    for (const auto& tx : block.transactions) {
        const auto tx_payload_hex = encode_tx_payload(tx);
        std::string tx_payload_bin;
        if (!from_hex(tx_payload_hex, tx_payload_bin)) {
            return {};
        }
        write_u32(bin, static_cast<std::uint32_t>(tx_payload_bin.size()));
        bin.append(tx_payload_bin);
    }

    write_u32(bin, payload_checksum32(bin));

    return to_hex(bin);
}

bool DecentralizedNode::decode_block_payload(const std::string& payload,
                                             Block& block,
                                             std::string& error) const {
    block = Block{};

    std::string bin;
    if (!from_hex(payload, bin)) {
        error = "invalid block payload hex";
        return false;
    }
    if (bin.size() < 4 || (bin.compare(0, 4, "BLB1") != 0 && bin.compare(0, 4, "BLB2") != 0)) {
        error = "invalid block payload magic";
        return false;
    }
    const bool has_checksum = (bin.compare(0, 4, "BLB2") == 0);

    std::size_t off = 4;
    if (!read_u64(bin, off, block.header.height) ||
        !read_string(bin, off, block.header.previous_hash) ||
        !read_u64(bin, off, block.header.timestamp) ||
        !read_u64(bin, off, block.header.nonce) ||
        !read_u64(bin, off, block.header.difficulty_target) ||
        !read_string(bin, off, block.header.merkle_root)) {
        error = "invalid block payload header";
        return false;
    }

    std::uint32_t tx_count = 0;
    if (!read_u32(bin, off, tx_count)) {
        error = "invalid block payload tx count";
        return false;
    }

    block.transactions.reserve(tx_count);
    for (std::uint32_t i = 0; i < tx_count; ++i) {
        std::uint32_t tx_sz = 0;
        if (!read_u32(bin, off, tx_sz)) {
            error = "invalid block payload tx size";
            return false;
        }
        if (off + tx_sz > bin.size()) {
            error = "invalid block payload tx bounds";
            return false;
        }

        const std::string tx_bin = std::string(bin.data() + off, bin.data() + off + tx_sz);
        off += tx_sz;

        Transaction tx{};
        if (!decode_tx_payload(to_hex(tx_bin), tx, error)) {
            return false;
        }
        block.transactions.push_back(std::move(tx));
    }

    if (has_checksum) {
        if (off + 4 != bin.size()) {
            error = "invalid block payload checksum bounds";
            return false;
        }
        std::uint32_t expected = 0;
        if (!read_u32(bin, off, expected)) {
            error = "invalid block payload checksum";
            return false;
        }
        const auto actual = payload_checksum32(bin.substr(0, bin.size() - 4));
        if (expected != actual) {
            error = "block payload checksum mismatch";
            return false;
        }
    } else if (off != bin.size()) {
        error = "invalid block payload trailing bytes";
        return false;
    }

    if (block.transactions.empty()) {
        error = "block payload has no transactions";
        return false;
    }
    return true;
}

std::string DecentralizedNode::encode_tx_gossip(const Transaction& tx) const {
    return "TX|" + encode_tx_payload(tx);
}

std::string DecentralizedNode::encode_block_announce(const Block& block) const {
    return "BLK|" + encode_block_payload(block);
}

bool DecentralizedNode::decode_tx_gossip(const std::string& payload,
                                         Transaction& tx,
                                         std::string& error) const {
    return decode_tx_payload(payload, tx, error);
}

bool DecentralizedNode::decode_block_announce(const std::string& payload,
                                               Block& block,
                                               std::string& error) const {
    return decode_block_payload(payload, block, error);
}

} // namespace addition
