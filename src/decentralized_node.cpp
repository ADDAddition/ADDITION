#include "addition/decentralized_node.hpp"

#include "addition/block.hpp"
#include "addition/config.hpp"
#include "addition/wire_v3.hpp"
#include "addition/crypto.hpp"
#include "addition/net_io.hpp"
#include "addition/net_transport.hpp"
#include "addition/dandelion.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <cctype>
#include <chrono>
#include <iostream>
#include <thread>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>
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

// v3: binary keys/sigs (TXV3/BLV3, base64). v2 peers still accepted (transcoded).
// Proto 4 = wire v3 + TXV3 script witnesses (payment channels). 3 = wire v3
// without witnesses (feat/net-tor nodes). 2 = legacy hex (TXB2/BLB*).
constexpr const char* kProtoVersion = "4";
constexpr const char* kProtoVersionLegacy = "2";
bool proto_supported(const std::string& v) {
    return v == "4" || v == "3" || v == "2";
}
constexpr std::uint64_t kMsgSkewSec = 90;
constexpr int kSocketTimeoutMs = 20000;
constexpr int kConnectTimeoutMs = 3000;
constexpr std::size_t kMaxPeerIdLen = 128;
constexpr std::size_t kMaxNonceLen = 128;
constexpr std::size_t kMaxPubKeyHexLen = 12000;
constexpr std::size_t kMaxSigHexLen = 40000;
constexpr std::size_t kMaxRotationIdLen = 128;

std::string active_network_id(const Chain& chain) {
    if (!chain.config().network_id.empty()) {
        return chain.config().network_id;
    }
    return "ADDITION_TESTNET_V1";
}

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

// Tor: fresh onion circuits (HS descriptor fetch + rendezvous) routinely take
// 30-90 s, and a ~15 KiB ML-DSA-87 HELLO is paced over a slow circuit.
constexpr int kOnionConnectTimeoutMs = 120000;
constexpr int kOnionIoTimeoutMs = 180000;
constexpr int kOnionHelloTransportRetries = 5;

// Persistent outbound connections. A Tor circuit to an onion service costs
// 5-60 s to build; reusing the socket turns every later request into one
// round trip. Peers that close after each reply (older nodes) simply fail the
// reuse attempt and fall back to a fresh connection.
struct PooledConn {
    SocketT sock;
    std::string carry;
    std::chrono::steady_clock::time_point last_used;
};
constexpr auto kPoolMaxIdle = std::chrono::seconds(60);   // < server keepalive (90 s)
constexpr std::size_t kPoolPerEndpoint = 4;
std::mutex g_pool_mu;
std::unordered_map<std::string, std::vector<PooledConn>> g_pool;

bool pool_take(const std::string& endpoint, PooledConn& out) {
    std::lock_guard<std::mutex> lk(g_pool_mu);
    auto it = g_pool.find(endpoint);
    if (it == g_pool.end()) return false;
    auto& v = it->second;
    const auto now = std::chrono::steady_clock::now();
    while (!v.empty()) {
        PooledConn c = std::move(v.back());
        v.pop_back();
        if (now - c.last_used < kPoolMaxIdle) { out = std::move(c); return true; }
        close_socket(c.sock);
    }
    return false;
}

void pool_put(const std::string& endpoint, PooledConn c) {
    c.last_used = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(g_pool_mu);
    auto& v = g_pool[endpoint];
    if (v.size() >= kPoolPerEndpoint) { close_socket(c.sock); return; }
    v.push_back(std::move(c));
}

bool open_p2p_conn(const std::string& endpoint, PooledConn& out) {
    std::string ip;
    std::uint16_t port = 0;
    if (!parse_endpoint(endpoint, ip, port)) return false;
    std::uintptr_t raw_sock = 0;
    std::string terr;
    // transport_connect honors the Tor/SOCKS policy: .onion via SOCKS5 (ATYP
    // domain, no local DNS), clearnet refused in onion_only mode.
    if (!transport_connect(ip, port, is_onion_v3_host(ip) ? kOnionConnectTimeoutMs : kConnectTimeoutMs, raw_sock, terr)) {
        return false;
    }
    SocketT sock = static_cast<SocketT>(raw_sock);
    socket_apply_wan_opts(static_cast<std::uintptr_t>(sock));
    const int io_ms = is_onion_v3_host(ip) ? kOnionIoTimeoutMs : kSocketTimeoutMs;
#ifdef _WIN32
    DWORD timeout_ms = static_cast<DWORD>(io_ms);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
    timeval tv{};
    tv.tv_sec = io_ms / 1000;
    tv.tv_usec = (io_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    out.sock = sock;
    out.carry.clear();
    return true;
}

// Write all requests, then read one reply line per request (pipelining).
// Returns the number of replies read; the connection is pooled only if all came back.
std::size_t exchange_on(const std::string& endpoint, PooledConn& c,
                        const std::vector<std::string>& requests, std::vector<std::string>& responses) {
    std::string payload;
    for (const auto& r : requests) { payload += r; payload.push_back('\n'); }
    // Pace ~1 KiB writes. A residential first-burst of the ~15 KiB ML-DSA
    // HELLO often dies even when the peer loop-reads.
    const bool sent_ok = socket_send_paced(static_cast<std::uintptr_t>(c.sock), payload.c_str(), payload.size());
    std::size_t got = 0;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        std::string line;
        if (!socket_recv_line_buffered(static_cast<std::uintptr_t>(c.sock), c.carry, line, kMaxLineBytes)) break;
        responses.push_back(std::move(line));
        ++got;
    }
    if (sent_ok && got == requests.size() && c.carry.empty()) {
        pool_put(endpoint, std::move(c));
    } else {
        close_socket(c.sock);
    }
    return got;
}

std::size_t send_p2p_pipeline(const std::string& endpoint, const std::vector<std::string>& requests,
                              std::vector<std::string>& responses) {
    responses.clear();
    for (const auto& r : requests) {
        if (r.size() + 1 > kMaxLineBytes) return 0;
    }
    if (requests.empty()) return 0;
#ifdef _WIN32
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return 0;
#endif
    StateUnlockDuringIo unlocked;  // never hold node state across a Tor round trip
    std::size_t got = 0;
    PooledConn c;
    if (pool_take(endpoint, c)) {
        got = exchange_on(endpoint, c, requests, responses);
        if (got == 0) responses.clear();  // stale pooled socket: retry fresh
    }
    if (got == 0) {
        PooledConn fresh;
        if (open_p2p_conn(endpoint, fresh)) {
            // Old peers close after one reply; send the rest one per connection.
            got = exchange_on(endpoint, fresh, {requests.front()}, responses);
            if (got == 1 && requests.size() > 1) {
                std::vector<std::string> rest(requests.begin() + 1, requests.end());
                PooledConn again;
                if (pool_take(endpoint, again) || open_p2p_conn(endpoint, again)) {
                    got += exchange_on(endpoint, again, rest, responses);
                }
            }
        }
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return got;
}

bool send_p2p_request(const std::string& endpoint, const std::string& request, std::string& response) {
    std::vector<std::string> out;
    if (send_p2p_pipeline(endpoint, {request}, out) != 1) return false;
    response = std::move(out.front());
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

std::string derive_node_id(const std::string& requested, const std::string& pubkey) {
    if (!requested.empty() && requested != "self") {
        return requested;
    }
    if (pubkey.empty()) {
        return requested.empty() ? std::string("self") : requested;
    }
    return "n-" + addition::to_hex(sha3_512_bytes("node-id|" + pubkey)).substr(0, 32);
}

// Persist-era seed (3a83a32): one recv, reply is ok:+outbound.front().
// "self" is often banned after failed home HELLOs. Leftover BLK| can shadow HELLO_ACK.
bool persist_leftover_reply(const std::string& resp) {
    return resp == "ok" ||
           resp.rfind("ok:BLK|", 0) == 0 ||
           resp.rfind("ok:TX|", 0) == 0 ||
           resp.rfind("ok:HAVE", 0) == 0 ||
           resp.rfind("ok:INV|", 0) == 0 ||
           resp.rfind("ok:IDROTATE|", 0) == 0 ||
           resp.rfind("ok:IDVOTE|", 0) == 0 ||
           resp.rfind("ok:SVOTE|", 0) == 0;
}

bool persist_truncated_hello(const std::string& resp) {
    // Last-field (sig) cuts still parse as six '|' fields, then fail verify.
    return resp == "error: invalid hello" || resp == "error: invalid hello signature";
}

bool parse_info_u64(const std::string& text, const char* key, std::uint64_t& out) {
    const std::string needle = std::string(key) + "=";
    const auto pos = text.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    try {
        out = std::stoull(text.substr(pos + needle.size()));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parse_info_token(const std::string& text, const char* key, std::string& out) {
    const std::string needle = std::string(key) + "=";
    const auto pos = text.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    std::size_t i = pos + needle.size();
    while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' &&
           text[i] != ',') {
        ++i;
    }
    out = text.substr(pos + needle.size(), i - (pos + needle.size()));
    return !out.empty();
}

std::string url_encode_cmd(const std::string& cmd) {
    static const char* hexd = "0123456789ABCDEF";
    std::string out;
    out.reserve(cmd.size() * 3);
    for (unsigned char c : cmd) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
            c == '-') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out += "%20";
        } else {
            out.push_back('%');
            out.push_back(hexd[c >> 4]);
            out.push_back(hexd[c & 15]);
        }
    }
    return out;
}

std::string http_response_body(const std::string& raw) {
    auto pos = raw.find("\r\n\r\n");
    if (pos != std::string::npos) {
        return raw.substr(pos + 4);
    }
    pos = raw.find("\n\n");
    if (pos != std::string::npos) {
        return raw.substr(pos + 2);
    }
    return raw;
}

std::uint16_t configured_public_http_port() {
    if (const char* raw = std::getenv("ADDITION_PUBLIC_HTTP_PORT")) {
        try {
            const auto parsed = std::stoul(raw);
            if (parsed > 0 && parsed <= 65535) {
                return static_cast<std::uint16_t>(parsed);
            }
        } catch (const std::exception&) {
        }
    }
    return 80;
}

void push_unique_endpoint(std::vector<std::string>& out, const std::string& ip, std::uint16_t port) {
    const std::string ep = ip + ":" + std::to_string(port);
    if (std::find(out.begin(), out.end(), ep) == out.end()) {
        out.push_back(ep);
    }
}

std::vector<std::string> public_rpc_endpoints(const std::string& p2p_peer) {
    std::string ip;
    std::uint16_t port = 0;
    std::vector<std::string> out;
    if (!parse_endpoint(p2p_peer, ip, port)) {
        return out;
    }
    // Prefer the canonical public-read port for this P2P port (28546→38546,
    // 28545→38545). :80 serves the public testnet only — never use it for
    // mainnet IBD (would ingest ADDITION_TESTNET_V1 into MAINNET_V1).
    if (port <= 55535) {
        push_unique_endpoint(out, ip, static_cast<std::uint16_t>(port + 10000U));
    }
    const bool mainnet_p2p = (port == 28546);
    if (mainnet_p2p) {
        push_unique_endpoint(out, ip, 38546);
    } else {
        push_unique_endpoint(out, ip, configured_public_http_port());
        push_unique_endpoint(out, ip, 38545);
        push_unique_endpoint(out, ip, 38546);
    }
    return out;
}

bool send_http_rpc(const std::string& endpoint, const std::string& cmd, std::string& response) {
    StateUnlockDuringIo unlocked;  // HTTP round trip: don't hold node state
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
    std::uintptr_t raw_sock = 0;
    std::string terr;
    if (!transport_connect(ip, port, is_onion_v3_host(ip) ? kOnionConnectTimeoutMs : kConnectTimeoutMs, raw_sock, terr)) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    SocketT sock = static_cast<SocketT>(raw_sock);
    socket_apply_wan_opts(static_cast<std::uintptr_t>(sock));
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
    std::ostringstream req;
    req << "GET /rpc?cmd=" << url_encode_cmd(cmd) << " HTTP/1.0\r\n"
        << "Host: " << ip << "\r\n"
        << "Connection: close\r\n\r\n";
    const auto raw = req.str();
    if (!socket_send_paced(static_cast<std::uintptr_t>(sock), raw.c_str(), raw.size())) {
        close_socket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    std::string http;
    if (!socket_recv_http_response(static_cast<std::uintptr_t>(sock), http, kMaxLineBytes) ||
        http.empty()) {
        close_socket(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
    close_socket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    response = http_response_body(http);
    while (!response.empty() && (response.back() == '\n' || response.back() == '\r')) {
        response.pop_back();
    }
    return !response.empty();
}

bool send_public_rpc(const std::string& endpoint, const std::string& cmd, std::string& response) {
    std::string ip;
    std::uint16_t port = 0;
    if (parse_endpoint(endpoint, ip, port) &&
        (port == 80 || port == configured_public_http_port())) {
        return send_http_rpc(endpoint, cmd, response);
    }
    if (send_p2p_request(endpoint, cmd, response) && !response.empty() &&
        response.rfind("error: usage <peer>", 0) != 0) {
        return true;
    }
    return send_http_rpc(endpoint, cmd, response);
}

} // namespace

DecentralizedNode::DecentralizedNode(std::string self_id,
                                                                         std::string node_public_key,
                                                                         std::string node_private_key,
                                     Chain& chain,
                                     Mempool& mempool,
                                     PeerNetwork& peers,
                                     ConsensusEngine& consensus)
    : self_id_(derive_node_id(self_id, node_public_key)),
            node_public_key_(std::move(node_public_key)),
            node_private_key_(std::move(node_private_key)),
      chain_(chain),
      mempool_(mempool),
      peers_(peers),
      consensus_(consensus) {}

const std::string& DecentralizedNode::self_id() const {
    return self_id_;
}

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
    const auto pending = outbound_;  // copy: the state lock is dropped during sends

    for (const auto& msg : pending) {
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
    constexpr std::size_t kGenericMax = 2048;
    constexpr std::size_t kExpensiveMax = 512;

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
    if (dandelion_) {
        return stem_transaction(tx, "", error);
    }
    return fluff_transaction(tx, error);
}

void DecentralizedNode::set_dandelion(bool enabled) {
    dandelion_ = enabled;
}

bool DecentralizedNode::dandelion_enabled() const {
    return dandelion_;
}

std::size_t DecentralizedNode::stempool_size() const {
    return stempool_.size();
}

DandelionRouter& DecentralizedNode::dandelion_router() {
    return dandelion_router_;
}

void DecentralizedNode::set_advertised_p2p(const std::string& endpoint) {
    advertised_p2p_ = endpoint;
}

std::vector<std::string> DecentralizedNode::outbound_relay_peers() const {
    std::vector<std::string> out;
    for (const auto& p : peers_.select_peers(0)) {
        if (is_peer_endpoint(p) && !is_self_peer_label(p) && !peers_.is_banned(p)) {
            out.push_back(p);
        }
    }
    return out;
}

bool DecentralizedNode::stem_transaction(const Transaction& tx, const std::string& source, std::string& error) {
    if (!chain_.validate_transaction(tx, error)) {
        return false;
    }
    const auto txid = hash_transaction(tx);
    if (seen_txids_.count(txid) != 0 || stempool_.count(txid) != 0) {
        return true;  // already known (stem loop or already fluffed)
    }
    const auto d = dandelion_router_.route(source, outbound_relay_peers());
    if (d.fluff || stempool_.size() >= kMaxStempool) {
        dandelion_router_.mark_fluffed(txid);
        return fluff_transaction(tx, error);
    }
    stempool_.emplace(txid, tx);
    dandelion_router_.embargo(txid);
    const auto gossip = encode_tx_gossip(tx);  // "TX|<payload>"
    stem_outbound_.emplace_back(d.relay, "STEM|" + gossip.substr(3));
    return true;
}

std::size_t DecentralizedNode::flush_stem(std::string& last) {
    std::size_t sent = 0;
    // Embargo expired without seeing the tx fluffed: fluff it ourselves.
    for (const auto& txid : dandelion_router_.expired_embargoes()) {
        auto it = stempool_.find(txid);
        if (it == stempool_.end()) {
            continue;
        }
        const auto tx = it->second;
        stempool_.erase(it);
        std::string ferr;
        fluff_transaction(tx, ferr);
    }
    auto queue = std::move(stem_outbound_);
    stem_outbound_.clear();
    for (const auto& item : queue) {
        const auto& relay = item.first;
        std::string wire_id;
        std::string hs_err;
        std::string resp;
        bool ok = handshake_with_peer(relay, wire_id, hs_err);
        if (ok) {
            if (wire_id.empty()) {
                wire_id = self_id_;
            }
            const auto adapted = adapt_for_peer(item.second, peer_proto(relay));
            ok = !adapted.empty() && send_p2p_request(relay, wire_id + " " + adapted, resp) &&
                 resp.rfind("error:", 0) != 0;
        }
        if (ok) {
            ++sent;
            peers_.mark_peer_good(relay);
            continue;
        }
        last = "stem relay failed to " + relay;
        peers_.mark_peer_bad(relay);
        // Relay unreachable: fall back to fluffing (embargo semantics).
        Transaction tx{};
        std::string derr;
        if (decode_tx_gossip(item.second.substr(5), tx, derr)) {
            const auto txid = hash_transaction(tx);
            stempool_.erase(txid);
            dandelion_router_.mark_fluffed(txid);
            fluff_transaction(tx, derr);
        }
    }
    return sent;
}

bool DecentralizedNode::fluff_transaction(const Transaction& tx, std::string& error) {
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
    for (const auto& v : block.votes) {
        observe_vote_for_equivocation(v);  // a vote in a competing block counts too
    }

    if (!chain_.add_block(block, error)) {
        return false;
    }

    consensus_.submit_vote(self_id_, block.header.height, bh);
    outbound_.push_back(encode_block_announce(block));
    refresh_local_votes(); // our selected tickets vote on the new tip
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

bool DecentralizedNode::fetch_blocks_from_peer(const std::string& peer,
                                              const std::string& wire_id,
                                              std::string& error) {
    const std::string id = wire_id.empty() ? self_id_ : wire_id;
    std::ostringstream inv_req;
    const auto inv_start = (chain_.height() > 0) ? (chain_.height() - 1) : 0;
    inv_req << id << " REQINV|" << inv_start << "|24";

    std::string inv_resp;
    if (!send_p2p_request(peer, inv_req.str(), inv_resp)) {
        error = "REQINV transport failed";
        peers_.mark_peer_bad(peer);
        return false;
    }
    if (inv_resp.rfind("ok:INV|", 0) != 0) {
        error = "REQINV rejected: " + inv_resp;
        peers_.mark_peer_bad(peer);
        return false;
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
        bool ingested_any = false;
        // Pipeline every missing block on one persistent connection instead of
        // one Tor round trip (and formerly one circuit) per block.
        std::vector<std::uint64_t> wanted;
        std::vector<std::string> reqs;
        for (const auto& [h, hash] : inventory) {
            if (h <= chain_.height() || chain_.has_block_hash(hash)) {
                continue;
            }
            wanted.push_back(h);
            reqs.push_back(id + " REQBLK|" + std::to_string(h));
        }
        if (reqs.empty()) {
            return true;
        }
        std::vector<std::string> resps;
        const auto got = send_p2p_pipeline(peer, reqs, resps);
        for (std::size_t i = 0; i < got; ++i) {
            const auto h = wanted[i];
            const auto& block_resp = resps[i];
            if (h <= chain_.height()) {
                continue;  // ingested by gossip while the state lock was released
            }
            if (block_resp.rfind("ok:BLKDATA|", 0) != 0) {
                error = "REQBLK rejected at height " + std::to_string(h) + ": " + block_resp;
                peers_.mark_peer_bad(peer);
                return ingested_any;
            }
            std::string ingest_error;
            if (!ingest_block_payload(block_resp.substr(11), ingest_error)) {
                if (chain_.height() >= h) {
                    continue;  // raced with gossip; already have it
                }
                peers_.mark_peer_bad(peer);
                error = ingest_error;
                return ingested_any;
            }
            ingested_any = true;
            peers_.mark_peer_good(peer);
        }
        if (got < reqs.size()) {
            error = "REQBLK transport failed at height " + std::to_string(wanted[got]);
            peers_.mark_peer_bad(peer);
            forget_handshake(peer);
            return ingested_any;
        }
        return true;
    }

    std::string remote_height_resp;
    if (!send_p2p_request(peer, id + " REQH|", remote_height_resp) ||
        remote_height_resp.rfind("ok:HAVEH|", 0) != 0) {
        error = "REQH failed during reorg";
        peers_.mark_peer_bad(peer);
        return false;
    }
    std::uint64_t remote_h = 0;
    try {
        remote_h = std::stoull(remote_height_resp.substr(9));
    } catch (const std::exception&) {
        error = "invalid remote height during reorg";
        peers_.mark_peer_bad(peer);
        return false;
    }

    std::vector<Block> candidate;
    candidate.reserve(static_cast<std::size_t>(remote_h + 1));

    for (std::uint64_t h = 0; h <= remote_h; ++h) {
        std::ostringstream req;
        req << id << " REQBLK|" << h;

        std::string block_resp;
        if (!send_p2p_request(peer, req.str(), block_resp)) {
            error = "REQBLK transport failed at height " + std::to_string(h);
            peers_.mark_peer_bad(peer);
            return false;
        }

        if (block_resp.rfind("ok:BLKDATA|", 0) != 0) {
            error = "REQBLK rejected at height " + std::to_string(h) + ": " + block_resp;
            peers_.mark_peer_bad(peer);
            return false;
        }

        Block b{};
        std::string decode_err;
        if (!decode_block_payload(block_resp.substr(11), b, decode_err)) {
            error = decode_err;
            peers_.mark_peer_bad(peer);
            return false;
        }
        candidate.push_back(std::move(b));
    }

    if (candidate.empty()) {
        error = "empty candidate chain";
        peers_.mark_peer_bad(peer);
        return false;
    }

    std::string reorg_error;
    if (!chain_.replace_with_chain(candidate, reorg_error)) {
        peers_.mark_peer_bad(peer);
        error = reorg_error;
        return false;
    }

    for (const auto& b : candidate) {
        const auto bh = hash_block_header(b.header);
        seen_block_hashes_.insert(bh);
        for (const auto& tx : b.transactions) {
            seen_txids_.insert(hash_transaction(tx));
        }
    }

    peers_.mark_peer_good(peer);
    return true;
}

bool DecentralizedNode::handshake_with_peer(const std::string& peer, std::string& wire_id, std::string& error) {
    error.clear();
    // Reuse a recent signed handshake instead of a ~15 KiB ML-DSA HELLO per
    // message. Invalidated on "handshake required" or any transport failure.
    {
        const auto it = hello_cache_.find(peer);
        if (it != hello_cache_.end()) {
            const auto hs = outbound_hs_ok_.find(peer);
            if (hs != outbound_hs_ok_.end() && hs->second &&
                now_seconds() < it->second.second + kHelloCacheSeconds) {
                wire_id = it->second.first;
                return true;
            }
            hello_cache_.erase(it);
        }
    }
    const bool ok = handshake_with_peer_uncached(peer, wire_id, error);
    if (ok) {
        hello_cache_[peer] = {wire_id.empty() ? self_id_ : wire_id, now_seconds()};
    }
    return ok;
}

void DecentralizedNode::forget_handshake(const std::string& peer) {
    hello_cache_.erase(peer);
}

bool DecentralizedNode::handshake_with_peer_uncached(const std::string& peer, std::string& wire_id, std::string& error) {
    error.clear();
    outbound_hs_ok_[peer] = false;
    wire_id.clear();

    int transport_fails = 0;
    std::string hello_version = kProtoVersion;
    bool downgraded = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto hello_ts = now_seconds();
        const std::string hello_nonce = "s" + std::to_string(attempt) + "-" + std::to_string(hello_ts % 1000000ULL);
        const std::string hello_body = hello_version + "|" + active_network_id(chain_) + "|" +
                                       std::to_string(hello_ts) + "|" + hello_nonce + "|" +
                                       node_public_key_;
        std::string hello_sig;
        try {
            hello_sig = sign_message_hybrid(node_private_key_, hello_body);
        } catch (const std::exception&) {
            error = "hello signing failed";
            return false;
        }

        const std::string attempt_id = (attempt == 0) ? self_id_ : (self_id_ + "x" + std::to_string(attempt));
        std::ostringstream hello_req;
        hello_req << attempt_id << " HELLO|" << hello_version << '|' << active_network_id(chain_)
                  << '|' << hello_ts << '|' << hello_nonce << '|' << node_public_key_ << '|' << hello_sig;

        std::string hello_resp;
        if (!send_p2p_request(peer, hello_req.str(), hello_resp)) {
            error = "HELLO transport failed to " + peer +
                    " (paced WAN write/read of ML-DSA HELLO failed)";
            ++transport_fails;
            {
                std::string hip;
                std::uint16_t hport = 0;
                const bool onion = parse_endpoint(peer, hip, hport) && is_onion_v3_host(hip);
                const int max_fails = onion ? kOnionHelloTransportRetries : 2;
                if (transport_fails >= max_fails) {
                    return false;
                }
                if (onion) {
                    // Exponential backoff (2,4,8,16 s): lets Tor build a new circuit.
                    StateUnlockDuringIo unlocked;
                    std::this_thread::sleep_for(std::chrono::seconds(1LL << transport_fails));
                }
            }
            continue;
        }

        if (hello_resp.rfind("ok:HELLO_ACK|", 0) == 0) {
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
                error = "invalid HELLO_ACK fields";
                return false;
            }
            // Server answers min(ours, theirs); never accept a version above what we offered.
            if (!proto_supported(v) || (std::stoi(v) > std::stoi(hello_version)) ||
                n != active_network_id(chain_) || nonce != hello_nonce ||
                peer_pk.empty() || sig.empty()) {
                error = "HELLO_ACK mismatch";
                return false;
            }
            if (!is_reasonable_field_size(nonce, kMaxNonceLen) ||
                !is_reasonable_field_size(peer_pk, kMaxPubKeyHexLen) ||
                !is_reasonable_field_size(sig, kMaxSigHexLen)) {
                error = "HELLO_ACK field too large";
                return false;
            }
            const auto pit = pinned_peer_pubkeys_.find(peer);
            if (pit != pinned_peer_pubkeys_.end() && pit->second != peer_pk) {
                error = "peer pubkey pin mismatch";
                return false;
            }
            const std::string ack_body = v + "|" + active_network_id(chain_) + "|" + ts +
                                         "|" + nonce + "|" + peer_pk;
            if (!verify_message_signature_hybrid(peer_pk, ack_body, sig)) {
                error = "invalid HELLO_ACK signature";
                return false;
            }
            peer_pubkeys_[peer] = peer_pk;
            pinned_peer_pubkeys_.emplace(peer, peer_pk);
            outbound_hs_ok_[peer] = true;
            peer_proto_[peer] = std::stoi(v);
            wire_id = attempt_id;
            return true;
        }

        if (persist_leftover_reply(hello_resp)) {
            outbound_hs_ok_[peer] = true;
            std::string work;
            if (send_p2p_request(peer, attempt_id + " REQWORK|", work) && work.rfind("ok:HAVEWORK|", 0) == 0) {
                wire_id = attempt_id;
                return true;
            }
            error = "persist leftover reply without HAVEWORK: " + hello_resp;
            outbound_hs_ok_[peer] = false;
            continue;
        }

        if (persist_truncated_hello(hello_resp)) {
            error = "HELLO rejected as invalid or bad signature";
            continue;
        }

        if (hello_resp == "error: unknown peer") {
            error = "seed rejected peer id (persist banned self)";
            continue;
        }

        error = "expected HELLO_ACK, got: " + hello_resp;
        if (hello_resp.rfind("error: handshake mismatch", 0) == 0 && !downgraded) {
            // Older peer: step down 4 -> 3 -> 2 (min-version negotiation).
            hello_version = (hello_version == "4") ? "3" : kProtoVersionLegacy;
            downgraded = (hello_version == kProtoVersionLegacy);
            continue;
        }
        if (hello_resp.rfind("error: hello timestamp", 0) == 0 ||
            hello_resp.rfind("error: handshake mismatch", 0) == 0 ||
            hello_resp.rfind("error: peer pubkey pin", 0) == 0 ||
            hello_resp.rfind("error: missing hello", 0) == 0) {
            return false;
        }
    }

    if (error.empty()) {
        error = "handshake failed";
    }
    return false;
}

bool DecentralizedNode::ingest_from_public_rpc(const std::string& p2p_peer, std::string& error) {
    error.clear();
    const auto endpoints = public_rpc_endpoints(p2p_peer);
    if (endpoints.empty()) {
        error = "no public-rpc endpoint for " + p2p_peer;
        return false;
    }

    std::string last;
    for (const auto& ep : endpoints) {
        std::string info;
        if (!send_public_rpc(ep, "getinfo", info)) {
            last = "public-rpc getinfo transport failed at " + ep;
            continue;
        }
        if (info.rfind("error:", 0) == 0) {
            last = "public-rpc getinfo: " + info;
            continue;
        }
        std::string remote_id;
        if (!parse_info_token(info, "network_id", remote_id) || remote_id != active_network_id(chain_)) {
            last = "public-rpc network_id mismatch at " + ep + " (want " + active_network_id(chain_) +
                   ", got " + (remote_id.empty() ? std::string("<missing>") : remote_id) + ")";
            continue;
        }
        std::uint64_t remote_h = 0;
        if (!parse_info_u64(info, "height", remote_h)) {
            last = "public-rpc getinfo missing height at " + ep;
            continue;
        }
        if (remote_h <= chain_.height()) {
            return true;
        }

        for (std::uint64_t h = chain_.height() + 1; h <= remote_h; ++h) {
            std::string raw;
            const std::string cmd = "getblockraw " + std::to_string(h);
            if (!send_public_rpc(ep, cmd, raw)) {
                error = "public-rpc getblockraw transport failed at height " + std::to_string(h);
                return false;
            }
            std::string payload = raw;
            if (payload.rfind("ok:BLKDATA|", 0) == 0) {
                payload = payload.substr(11);
            } else if (payload.rfind("error:", 0) == 0) {
                error = "public-rpc getblockraw " + std::to_string(h) + ": " + raw;
                return false;
            }
            std::string ingest_err;
            if (!ingest_block_payload(payload, ingest_err)) {
                error = "public-rpc ingest height " + std::to_string(h) + ": " + ingest_err;
                return false;
            }
        }
        if (chain_.height() < remote_h) {
            error = "public-rpc ingest stalled at height " + std::to_string(chain_.height()) +
                    " peer_height=" + std::to_string(remote_h);
            return false;
        }
        return true;
    }

    error = last.empty() ? ("public-rpc ingest failed for " + p2p_peer) : last;
    return false;
}

bool DecentralizedNode::sync_once(std::string& error) {
    // The state lock is released during network I/O, so a second caller (RPC
    // "sync" vs the background loop) could interleave; let only one run.
    while (sync_busy_.exchange(true)) {
        StateUnlockDuringIo unlocked;  // wait for the other sync to finish, then run ours
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    struct Clear { std::atomic<bool>& f; ~Clear() { f.store(false); } } clear{sync_busy_};
    const bool ok = sync_once_impl(error);
    if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) {
        std::cerr << "sync_once tid=" << std::this_thread::get_id() << " ok=" << ok << " height=" << chain_.height() << " err=" << error << std::endl;
    }
    refresh_local_votes(); // vote on whatever tip we ended up on
    return ok;
}

bool DecentralizedNode::sync_once_impl(std::string& error) {
    error.clear();
    // Try a few unverified candidates (from ADDR / ADVERTISE); a successful
    // signed handshake promotes them into the peer table.
    {
        std::size_t tried = 0;
        for (const auto& cand : peers_.candidates()) {
            if (tried++ >= 2) {
                break;
            }
            std::string wid;
            std::string herr;
            if (handshake_with_peer(cand, wid, herr)) {
                peers_.promote_candidate(cand);
            } else {
                peers_.remove_candidate(cand);
            }
        }
    }
    auto peers = peers_.select_peers(0);
    if (peers.empty()) {
        error = "no peer";
        return false;
    }

    const auto start_height = chain_.height();
    bool saw_longer_peer = false;
    bool any_handshake = false;
    std::uint64_t max_remote_h = start_height;
    std::string last_error;

    for (const auto& peer : peers) {
        if (peers_.is_banned(peer) || (!advertised_p2p_.empty() && peer == advertised_p2p_)) {
            continue;  // never sync from / gossip to our own advertised endpoint
        }

        std::string rpc_err;
        if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) std::cerr << "sync try tid=" << std::this_thread::get_id() << " peer=" << peer << std::endl;
        if (ingest_from_public_rpc(peer, rpc_err)) {
            if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) std::cerr << "sync via public rpc" << std::endl;
            any_handshake = true;
            if (chain_.height() > max_remote_h) {
                max_remote_h = chain_.height();
            }
            continue;
        }

        std::string hs_err;
        std::string wire_id;
        if (!handshake_with_peer(peer, wire_id, hs_err)) {
            if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) std::cerr << "sync hs fail " << hs_err << " rpc=" << rpc_err << std::endl;
            outbound_hs_ok_[peer] = false;
            last_error = (hs_err.empty() ? std::string("handshake failed") : hs_err) + "; " +
                         (rpc_err.empty() ? std::string("public-rpc ingest failed") : rpc_err);
            continue;
        }
        if (wire_id.empty()) {
            wire_id = self_id_;
        }
        any_handshake = true;

        {
            std::string learn_err;
            learn_peers_from(peer, wire_id, learn_err);
            if (!advertised_p2p_.empty() && is_peer_endpoint(peer)) {
                std::string adv_resp;
                send_p2p_request(peer, wire_id + " ADVERTISE|" + advertised_p2p_, adv_resp);
            }
        }

        {
            std::string relay_err;
            if (!relay_rotation_messages_to_peer(peer, relay_err)) {
                if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) std::cerr << "sync relay fail " << relay_err << std::endl;
                last_error = relay_err.empty() ? "rotation relay failed" : relay_err;
                peers_.mark_peer_bad(peer);
                continue;
            }
        }

        std::string remote_work_resp;
        if (!send_p2p_request(peer, wire_id + " REQWORK|", remote_work_resp)) {
            last_error = "REQWORK transport failed";
            if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) std::cerr << "sync reqwork transport fail" << std::endl;
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (remote_work_resp.find("handshake required") != std::string::npos) {
            // Cached HELLO went stale on the remote side: re-handshake once and retry.
            forget_handshake(peer);
            std::string hs2;
            if (handshake_with_peer(peer, wire_id, hs2)) {
                if (wire_id.empty()) {
                    wire_id = self_id_;
                }
                remote_work_resp.clear();
                send_p2p_request(peer, wire_id + " REQWORK|", remote_work_resp);
            }
        }
        if (remote_work_resp.rfind("ok:HAVEWORK|", 0) != 0) {
            last_error = "REQWORK rejected: " + remote_work_resp;
            if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) std::cerr << "sync reqwork rejected " << remote_work_resp.substr(0,200) << std::endl;
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) std::cerr << "sync reqwork resp=" << remote_work_resp.substr(0, 120) << std::endl;
        std::uint64_t remote_work = 0;
        try {
            remote_work = std::stoull(remote_work_resp.substr(12));
        } catch (const std::exception&) {
            last_error = "invalid HAVEWORK";
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (std::getenv("ADDITION_SYNC_DEBUG") != nullptr) {
            std::cerr << "sync peer=" << peer << " remote_work=" << remote_work << " local_work=" << chain_.cumulative_work() << std::endl;
        }
        if (remote_work <= chain_.cumulative_work()) {
            peers_.mark_peer_good(peer);
            continue;
        }

        std::string remote_height_resp;
        if (!send_p2p_request(peer, wire_id + " REQH|", remote_height_resp)) {
            last_error = "REQH transport failed";
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (remote_height_resp.rfind("ok:HAVEH|", 0) != 0) {
            last_error = "REQH rejected: " + remote_height_resp;
            peers_.mark_peer_bad(peer);
            continue;
        }

        std::uint64_t remote_h = 0;
        try {
            remote_h = std::stoull(remote_height_resp.substr(9));
        } catch (const std::exception&) {
            last_error = "invalid HAVEH";
            peers_.mark_peer_bad(peer);
            continue;
        }

        if (remote_h <= chain_.height()) {
            peers_.mark_peer_good(peer);
            continue;
        }

        saw_longer_peer = true;
        if (remote_h > max_remote_h) {
            max_remote_h = remote_h;
        }

        int idle_rounds = 0;
        while (chain_.height() < remote_h) {
            const auto before = chain_.height();
            std::string fetch_err;
            if (!fetch_blocks_from_peer(peer, wire_id, fetch_err)) {
                last_error = fetch_err.empty() ? "block fetch failed" : fetch_err;
                break;
            }

            std::string refresh;
            if (send_p2p_request(peer, wire_id + " REQH|", refresh) && refresh.rfind("ok:HAVEH|", 0) == 0) {
                try {
                    remote_h = std::stoull(refresh.substr(9));
                    if (remote_h > max_remote_h) {
                        max_remote_h = remote_h;
                    }
                } catch (const std::exception&) {
                }
            }

            if (chain_.height() == before) {
                ++idle_rounds;
                if (idle_rounds >= 2) {
                    last_error = "sync stalled at height " + std::to_string(before) +
                                 " peer_height=" + std::to_string(remote_h);
                    break;
                }
            } else {
                idle_rounds = 0;
            }
        }
    }

    if (!any_handshake) {
        error = last_error.empty() ? "handshake failed" : last_error;
        return false;
    }
    if (saw_longer_peer && chain_.height() < max_remote_h) {
        error = last_error.empty()
                    ? ("peer has a longer chain; local_height=" + std::to_string(chain_.height()) +
                       " peer_height=" + std::to_string(max_remote_h))
                    : last_error;
        return false;
    }
    return true;
}

bool DecentralizedNode::announce_tip(std::string& error) {
    error.clear();
    if (chain_.height() == 0) {
        return true;
    }
    const auto& tip = chain_.tip();
    const auto bh = hash_block_header(tip.header);
    seen_block_hashes_.insert(bh);
    for (const auto& tx : tip.transactions) {
        seen_txids_.insert(hash_transaction(tx));
    }
    outbound_.push_back(encode_block_announce(tip));
    return true;
}

bool DecentralizedNode::is_push_gossip_message(const std::string& message) const {
    return message.rfind("TX|", 0) == 0 || message.rfind("BLK|", 0) == 0 ||
           message.rfind("IDROTATE|", 0) == 0 || message.rfind("IDVOTE|", 0) == 0 ||
           message.rfind("SVOTE|", 0) == 0;
}

bool DecentralizedNode::flush_outbound_gossip(std::size_t& sent, std::string& error) {
    error.clear();
    sent = 0;
    {
        std::string stem_last;
        sent += flush_stem(stem_last);
    }
    auto msgs = pull_outbound_messages();
    // Best-effort rebroadcast of still-pending (already fluffed) mempool txs: one-shot
    // gossip can be lost on a transient failure. Stem-phase txs are never rebroadcast.
    {
        const auto pending = mempool_.snapshot();
        std::size_t added = 0;
        for (const auto& tx : pending) {
            if (added >= 32) break;
            if (stempool_.count(hash_transaction(tx)) != 0) continue;
            auto m = encode_tx_gossip(tx);
            if (std::find(msgs.begin(), msgs.end(), m) == msgs.end()) { msgs.push_back(std::move(m)); ++added; }
        }
    }
    if (msgs.empty()) {
        return true;
    }
    std::vector<std::string> pushable;
    pushable.reserve(msgs.size());
    for (const auto& msg : msgs) {
        if (is_push_gossip_message(msg)) {
            pushable.push_back(msg);
        }
    }
    if (pushable.empty()) {
        return true;
    }

    bool any_ok = false;
    std::string last;
    for (const auto& peer : peers_.peers()) {
        if (peers_.is_banned(peer) || (!advertised_p2p_.empty() && peer == advertised_p2p_)) {
            continue;  // self (e.g. our own .onion learned via peer exchange): wasted Tor round trip
        }
        std::string wire_id;
        std::string hs_err;
        if (!handshake_with_peer(peer, wire_id, hs_err)) {
            last = hs_err.empty() ? ("gossip handshake failed to " + peer) : hs_err;
            continue;
        }
        if (wire_id.empty()) {
            wire_id = self_id_;
        }
        for (const auto& msg : pushable) {
            std::string resp;
            const auto adapted = adapt_for_peer(msg, peer_proto(peer));
            if (adapted.empty()) {
                continue;  // not representable on this peer's protocol version
            }
            if (!send_p2p_request(peer, wire_id + " " + adapted, resp)) {
                last = "gossip transport failed to " + peer;
                peers_.mark_peer_bad(peer);
                forget_handshake(peer);
                break;
            }
            if (resp.find("handshake required") != std::string::npos) {
                forget_handshake(peer);
            }
            if (resp.rfind("error:", 0) == 0) {
                last = "gossip rejected by " + peer + ": " + resp;
                // Duplicate / already-seen is fine; keep peer.
                if (resp.find("duplicate") == std::string::npos &&
                    resp.find("handshake") == std::string::npos) {
                    peers_.mark_peer_good(peer);
                }
                continue;
            }
            ++sent;
            any_ok = true;
            peers_.mark_peer_good(peer);
        }
    }

    if (!any_ok && !peers_.peers().empty() && sent == 0) {
        error = last.empty() ? "gossip flush: no peer accepted push" : last;
    }
    return true;
}

bool DecentralizedNode::learn_peers_from(const std::string& peer, const std::string& wire_id, std::string& error) {
    error.clear();
    const std::string id = wire_id.empty() ? self_id_ : wire_id;
    std::string resp;
    if (!send_p2p_request(peer, id + " REQADDR|", resp)) {
        error = "REQADDR transport failed";
        return false;
    }
    if (resp.rfind("ok:ADDR|", 0) != 0) {
        // Older peers may not speak REQADDR; not fatal for sync.
        return true;
    }
    const auto body = resp.substr(8);
    std::istringstream iss(body);
    std::string item;
    while (std::getline(iss, item, ',')) {
        if (item.empty()) {
            continue;
        }
        if (!is_external_advertised_peer(item) || item == advertised_p2p_) {
            continue;
        }
        if (!peers_.has_peer(item)) {
            peers_.add_candidate(item);
        }
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
    std::string reply;
    const bool ok = handle_inbound_message(peer, message, reply, error);
    if (ok && !reply.empty()) {
        outbound_.push_back(std::move(reply));
    }
    return ok;
}

std::string DecentralizedNode::handle_p2p_line(const std::string& line) {
    std::istringstream iss(line);
    std::string peer;
    iss >> peer;
    std::string payload;
    std::getline(iss, payload);
    if (!payload.empty() && payload.front() == ' ') {
        payload.erase(payload.begin());
    }
    if (peer.empty() || payload.empty()) {
        return "error: usage <peer> <payload>";
    }
    std::string err;
    std::string reply;
    if (!handle_inbound_message(peer, payload, reply, err)) {
        return std::string("error: ") + err;
    }
    if (reply.empty()) {
        return "ok";
    }
    return "ok:" + reply;
}

bool DecentralizedNode::handle_inbound_message(const std::string& peer,
                                               const std::string& message,
                                               std::string& reply,
                                               std::string& error) {
    reply.clear();
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
        (message.rfind("REQADDR|", 0) == 0) ||
        (message.rfind("ADVERTISE|", 0) == 0) ||
        (message.rfind("IDROTATE|", 0) == 0) ||
        (message.rfind("IDVOTE|", 0) == 0) ||
        (message.rfind("SVOTE|", 0) == 0);
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

        if (!proto_supported(version) || network != active_network_id(chain_)) {
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

        const std::string negotiated =
            (version == "4" || version == "3") ? version : std::string(kProtoVersionLegacy);
        const std::string ack_body = negotiated + "|" + active_network_id(chain_) + "|" +
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
        ack << "HELLO_ACK|" << negotiated << '|' << active_network_id(chain_) << '|' << now << '|' << nonce
            << '|' << node_public_key_ << '|' << ack_sig;
        reply = ack.str();
        peers_.mark_peer_good(peer);
        handshake_ok_[peer] = true;
        peer_proto_[peer] = std::stoi(negotiated);
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
        const auto txid = hash_transaction(tx);
        stempool_.erase(txid);
        dandelion_router_.mark_fluffed(txid);
        const auto ok = fluff_transaction(tx, error);
        if (ok) {
            peers_.mark_peer_good(peer);
        } else {
            peers_.mark_peer_bad(peer);
        }
        return ok;
    }

    if (message.rfind("SVOTE|", 0) == 0) {
        StakeVote v{};
        if (!decode_stake_vote(message.substr(6), v, error)) {
            peers_.mark_peer_bad(peer);
            return false;
        }
        const auto ok = submit_stake_vote(v, error);
        // A vote for a tip we have not seen yet (or already pruned) is not misbehaviour.
        if (ok || error.find("duplicate") != std::string::npos || error.find("stale") != std::string::npos) {
            // Benign for re-announced votes: ack so the sender's batch (txs too) continues.
            peers_.mark_peer_good(peer);
            error.clear();
            return true;
        }
        peers_.mark_peer_bad(peer);
        return false;
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
        reply = out.str();
        peers_.mark_peer_good(peer);
        return true;
    }

    if (message.rfind("REQWORK|", 0) == 0) {
        std::ostringstream out;
        out << "HAVEWORK|" << chain_.cumulative_work();
        reply = out.str();
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

        reply = adapt_for_peer("BLKDATA|" + payload, peer_proto(peer));
        if (reply.empty()) {
            error = "block requires a newer protocol version (4)";
            return false;
        }
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

        reply = out.str();
        peers_.mark_peer_good(peer);
        return true;
    }

    if (message.rfind("STEM|", 0) == 0) {
        Transaction tx{};
        if (!decode_tx_gossip(message.substr(5), tx, error)) {
            peers_.mark_peer_bad(peer);
            return false;
        }
        const bool ok = dandelion_ ? stem_transaction(tx, peer, error) : fluff_transaction(tx, error);
        if (ok) {
            peers_.mark_peer_good(peer);
        } else {
            peers_.mark_peer_bad(peer);
        }
        return ok;
    }

    if (message.rfind("ADVERTISE|", 0) == 0) {
        // Unverified: stored as a candidate, promoted only after we complete
        // an outbound signed handshake with it.
        const auto addr = message.substr(10);
        if (!is_external_advertised_peer(addr)) {
            error = "bad advertise address";
            return false;
        }
        if (addr == advertised_p2p_) {
            reply = "ADVERTISE_OK";
            return true;  // ignore our own endpoint echoed back
        }
        peers_.add_candidate(addr);
        reply = "ADVERTISE_OK";
        return true;
    }

    if (message.rfind("REQADDR|", 0) == 0) {
        // IPv4 ip:port only. Never advertise self / node-id labels.
        std::ostringstream out;
        out << "ADDR|";
        bool first = true;
        std::size_t emitted = 0;
        for (const auto& p : peers_.select_peers(0)) {
            // Only routable addresses (IPv4 / v3 onion), never loopback or labels.
            if (!is_external_advertised_peer(p)) {
                continue;
            }
            if (++emitted > 64) {
                break;
            }
            if (!first) {
                out << ',';
            }
            first = false;
            out << p;
        }
        reply = out.str();
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
    return std::string(wire::kB64Prefix) + wire::base64_encode(wire::encode_tx_v3(tx));
}

std::string DecentralizedNode::encode_block_payload(const Block& block) const {
    return std::string(wire::kB64Prefix) + wire::base64_encode(wire::encode_block_v3(block));
}

int DecentralizedNode::peer_proto(const std::string& peer) const {
    const auto it = peer_proto_.find(peer);
    return it == peer_proto_.end() ? 2 : it->second;
}

namespace {
bool tx_needs_proto4(const Transaction& t) {
    for (const auto& in : t.inputs) {
        if (!in.witness.empty()) return true;
    }
    return false;
}
} // namespace

// Returns "" when the message cannot be represented for that peer's protocol
// (witness txs need proto 4; shielded txs need wire v3). Callers skip it.
std::string DecentralizedNode::adapt_for_peer(const std::string& msg, int proto) const {
    if (proto >= wire::kProtoV4) {
        return msg;
    }
    static const char* kPrefixes[] = {"TX|", "STEM|", "BLK|", "BLKDATA|"};
    for (const char* pre : kPrefixes) {
        const std::string p(pre);
        if (msg.rfind(p, 0) != 0 || msg.compare(p.size(), 4, wire::kB64Prefix) != 0) {
            continue;
        }
        std::string err;
        const auto body = msg.substr(p.size());
        if (p == "BLK|" || p == "BLKDATA|") {
            Block b{};
            if (decode_block_payload(body, b, err)) {
                for (const auto& t : b.transactions) {
                    if (tx_needs_proto4(t) || (proto < wire::kProtoV3 && !t.shielded.empty())) return {};
                }
                return proto >= wire::kProtoV3 ? msg : p + encode_block_payload_v2(b);
            }
        } else {
            Transaction t{};
            if (decode_tx_payload(body, t, err)) {
                if (tx_needs_proto4(t) || (proto < wire::kProtoV3 && !t.shielded.empty())) return {};
                return proto >= wire::kProtoV3 ? msg : p + encode_tx_payload_v2(t);
            }
        }
        return msg;
    }
    return msg;
}

std::string DecentralizedNode::encode_tx_payload_v2(const Transaction& tx) const {
    std::string bin;
    bin.reserve(256);
    // Legacy v2 hex. Witness txs are never sent here (adapt_for_peer filters
    // them); script witnesses live only in TXV3 (proto 4). TXB3 is retired.
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

    if (payload.rfind(wire::kB64Prefix, 0) == 0) {
        std::string raw;
        if (!wire::base64_decode(payload.substr(4), raw)) {
            error = "invalid tx payload base64";
            return false;
        }
        return wire::decode_tx_v3(raw, tx, error);
    }

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
    tx.inputs.reserve(std::min<std::uint32_t>(in_count, 1024));
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
    tx.outputs.reserve(std::min<std::uint32_t>(out_count, 1024));
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

std::string DecentralizedNode::encode_block_payload_v2(const Block& block) const {
    std::string bin;
    bin.reserve(1024);
    const bool staked = !block.votes.empty() || !block.slashes.empty();
    bin.append(staked ? "BLB3" : "BLB2", 4);

    write_u64(bin, block.header.height);
    write_string(bin, block.header.previous_hash);
    write_u64(bin, block.header.timestamp);
    write_u64(bin, block.header.nonce);
    write_u64(bin, block.header.difficulty_target);
    write_string(bin, block.header.merkle_root);

    write_u32(bin, static_cast<std::uint32_t>(block.transactions.size()));
    for (const auto& tx : block.transactions) {
        const auto tx_payload_hex = encode_tx_payload_v2(tx);
        std::string tx_payload_bin;
        if (!from_hex(tx_payload_hex, tx_payload_bin)) {
            return {};
        }
        write_u32(bin, static_cast<std::uint32_t>(tx_payload_bin.size()));
        bin.append(tx_payload_bin);
    }
    if (staked) {
        auto write_vote = [&](const StakeVote& v) {
            write_string(bin, v.ticket_id);
            write_u64(bin, v.voted_height);
            write_string(bin, v.voted_hash);
            write_u32(bin, v.approve ? 1U : 0U);
            write_string(bin, v.signature);
        };
        write_u32(bin, static_cast<std::uint32_t>(block.votes.size()));
        for (const auto& v : block.votes) {
            write_vote(v);
        }
        write_u32(bin, static_cast<std::uint32_t>(block.slashes.size()));
        for (const auto& sl : block.slashes) {
            write_vote(sl.a);
            write_vote(sl.b);
        }
    }

    write_u32(bin, payload_checksum32(bin));

    return to_hex(bin);
}

bool DecentralizedNode::decode_block_payload(const std::string& payload,
                                             Block& block,
                                             std::string& error) const {
    block = Block{};

    if (payload.rfind(wire::kB64Prefix, 0) == 0) {
        std::string raw;
        if (!wire::base64_decode(payload.substr(4), raw)) {
            error = "invalid block payload base64";
            return false;
        }
        return wire::decode_block_v3(raw, block, error);
    }

    std::string bin;
    if (!from_hex(payload, bin)) {
        error = "invalid block payload hex";
        return false;
    }
    if (bin.size() < 4 || (bin.compare(0, 4, "BLB1") != 0 && bin.compare(0, 4, "BLB2") != 0 &&
                           bin.compare(0, 4, "BLB3") != 0)) {
        error = "invalid block payload magic";
        return false;
    }
    const bool staked = (bin.compare(0, 4, "BLB3") == 0);
    const bool has_checksum = staked || (bin.compare(0, 4, "BLB2") == 0);

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

    block.transactions.reserve(std::min<std::uint32_t>(tx_count, 4096));
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
    if (staked) {
        auto read_vote = [&](StakeVote& v) {
            std::uint32_t a = 0;
            if (!read_string(bin, off, v.ticket_id) || !read_u64(bin, off, v.voted_height) ||
                !read_string(bin, off, v.voted_hash) || !read_u32(bin, off, a) ||
                !read_string(bin, off, v.signature)) {
                return false;
            }
            v.approve = (a != 0);
            return true;
        };
        std::uint32_t nv = 0;
        if (!read_u32(bin, off, nv) || nv > 4096) {
            error = "invalid block payload vote count";
            return false;
        }
        for (std::uint32_t i = 0; i < nv; ++i) {
            StakeVote v{};
            if (!read_vote(v)) {
                error = "invalid block payload vote";
                return false;
            }
            block.votes.push_back(std::move(v));
        }
        std::uint32_t ns = 0;
        if (!read_u32(bin, off, ns) || ns > 4096) {
            error = "invalid block payload slash count";
            return false;
        }
        for (std::uint32_t i = 0; i < ns; ++i) {
            SlashEvidence sl{};
            if (!read_vote(sl.a) || !read_vote(sl.b)) {
                error = "invalid block payload slash";
                return false;
            }
            block.slashes.push_back(std::move(sl));
        }
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

    if (block.transactions.empty() && block.header.height != 0) {
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

// ---- stake vote relay ----

std::string DecentralizedNode::encode_stake_vote(const StakeVote& v) {
    const std::vector<std::uint8_t> sig(v.signature.begin(), v.signature.end());
    std::ostringstream out;
    out << v.ticket_id << '|' << v.voted_height << '|' << v.voted_hash << '|' << (v.approve ? 1 : 0) << '|'
        << bytes_to_hex(sig);
    return out.str();
}

bool DecentralizedNode::decode_stake_vote(const std::string& payload, StakeVote& v, std::string& error) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto pos = payload.find('|', start);
        parts.push_back(payload.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    if (parts.size() != 5 || parts[0].empty() || parts[2].empty() || (parts[3] != "0" && parts[3] != "1") ||
        parts[4].empty() || payload.size() > 32768) {
        error = "invalid stake vote payload";
        return false;
    }
    try {
        v.voted_height = std::stoull(parts[1]);
    } catch (const std::exception&) {
        error = "invalid stake vote height";
        return false;
    }
    std::vector<std::uint8_t> sig;
    if (!hex_to_bytes(parts[4], sig, error)) {
        error = "invalid stake vote signature hex";
        return false;
    }
    v.ticket_id = parts[0];
    v.voted_hash = parts[2];
    v.approve = parts[3] == "1";
    v.signature.assign(sig.begin(), sig.end());
    return true;
}

void DecentralizedNode::prune_vote_pool() {
    const auto tip = chain_.height();
    std::vector<StakeVote> kept;
    for (auto& v : vote_pool_) {
        if (v.voted_height >= tip) kept.push_back(std::move(v));
    }
    vote_pool_.swap(kept);
    if (seen_stake_votes_.size() > 100000) seen_stake_votes_.clear();
    std::vector<SlashEvidence> live;
    for (auto& ev : pending_slashes_) {
        if (chain_.ticket(ev.a.ticket_id)) live.push_back(std::move(ev));
    }
    pending_slashes_.swap(live);
}

bool DecentralizedNode::observe_vote_for_equivocation(const StakeVote& v) {
    std::string err;
    if (!chain_.verify_vote_signature_only(v, err)) {
        return false;
    }
    const auto key = v.ticket_id + "|" + std::to_string(v.voted_height);
    const auto it = vote_watch_.find(key);
    if (it == vote_watch_.end()) {
        if (vote_watch_.size() > 200000) vote_watch_.clear();
        vote_watch_.emplace(key, v);
        return false;
    }
    const auto& first = it->second;
    if (first.voted_hash == v.voted_hash && first.approve == v.approve) {
        return false;
    }
    if (slash_reported_.count(v.ticket_id) != 0) {
        return true;
    }
    SlashEvidence ev{first, v};
    if (!chain_.slash_evidence_valid(ev, err)) {
        return false;
    }
    slash_reported_.insert(v.ticket_id);
    pending_slashes_.push_back(ev);
    // Relay the conflicting vote so every peer can build the same evidence.
    outbound_.push_back("SVOTE|" + encode_stake_vote(v));
    outbound_.push_back("SVOTE|" + encode_stake_vote(first));
    std::cerr << "[staking] equivocation detected: ticket " << v.ticket_id.substr(0, 16)
              << "... height " << v.voted_height << " (slash evidence queued)\n";
    return true;
}

std::vector<SlashEvidence> DecentralizedNode::usable_slash_evidence() const {
    std::vector<SlashEvidence> out;
    for (const auto& ev : pending_slashes_) {
        std::string err;
        if (chain_.slash_evidence_valid(ev, err)) out.push_back(ev);
    }
    return out;
}

bool DecentralizedNode::submit_stake_vote(const StakeVote& v, std::string& error) {
    prune_vote_pool();
    if (observe_vote_for_equivocation(v)) {
        error = "duplicate stake vote (equivocation recorded)";
        return false;
    }
    const auto tip = chain_.height();
    if (v.voted_height != tip) {
        error = "stale stake vote";
        return false;
    }
    const auto key = v.ticket_id + "|" + std::to_string(v.voted_height) + "|" + v.voted_hash + "|" +
                     (v.approve ? "1" : "0");
    if (seen_stake_votes_.count(key) != 0) {
        error = "duplicate stake vote";
        return false;
    }
    if (!chain_.verify_stake_vote(v, error)) {
        return false;
    }
    const auto sel = chain_.selected_tickets_for_next_block();
    if (std::find(sel.begin(), sel.end(), v.ticket_id) == sel.end()) {
        error = "ticket not selected for this block";
        return false;
    }
    seen_stake_votes_.insert(key);
    vote_pool_.push_back(v);
    outbound_.push_back("SVOTE|" + encode_stake_vote(v));
    return true;
}

std::vector<StakeVote> DecentralizedNode::usable_stake_votes() const {
    return chain_.usable_votes_for_next_block(vote_pool_);
}

void DecentralizedNode::set_auto_vote_key(const std::string& address, const std::string& public_key,
                                          const std::string& private_key) {
    auto_vote_keys_[address] = AutoVoteKey{public_key, private_key};
}

void DecentralizedNode::clear_auto_vote_key(const std::string& address) {
    auto_vote_keys_.erase(address);
}

std::vector<std::string> DecentralizedNode::auto_vote_addresses() const {
    std::vector<std::string> out;
    for (const auto& kv : auto_vote_keys_) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

std::size_t DecentralizedNode::refresh_local_votes() {
    if (auto_vote_keys_.empty() || !chain_.staking_active_at(chain_.height() + 1)) {
        return 0;
    }
    const auto tip = chain_.height();
    const auto tip_block = chain_.block_at(tip);
    if (!tip_block) return 0;
    const auto tip_hash = hash_block_header(tip_block->header);
    std::size_t cast = 0;
    for (const auto& id : chain_.selected_tickets_for_next_block()) {
        const auto t = chain_.ticket(id);
        if (!t) continue;
        const auto k = auto_vote_keys_.find(t->owner);
        if (k == auto_vote_keys_.end() || k->second.public_key != t->owner_pubkey) continue;
        StakeVote v{};
        v.ticket_id = id;
        v.voted_height = tip;
        v.voted_hash = tip_hash;
        v.approve = true;
        // Never sign a second, conflicting vote at this height (equivocation burns the ticket).
        // Re-announce our own existing vote instead (gossip is best-effort; dedup is cheap).
        bool already = false;
        for (const auto& p : vote_pool_) {
            if (p.ticket_id == id && p.voted_height == tip) {
                already = true;
                if (p.voted_hash == tip_hash) {
                    const std::string msg = "SVOTE|" + encode_stake_vote(p);
                    if (std::find(outbound_.begin(), outbound_.end(), msg) == outbound_.end()) {
                        outbound_.push_back(msg);
                    }
                }
                break;
            }
        }
        if (already) continue;
        try {
            v.signature = sign_message_hybrid(k->second.private_key,
                                              chain_.stake_vote_message_for(tip, tip_hash, id, true));
        } catch (const std::exception&) {
            continue;
        }
        std::string err;
        if (submit_stake_vote(v, err)) ++cast;
    }
    return cast;
}

} // namespace addition
