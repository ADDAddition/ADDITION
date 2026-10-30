#include "addition/net_transport.hpp"

#include "addition/net_io.hpp"

#include <atomic>
#include <cctype>
#include <deque>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace addition {

namespace {

std::mutex g_mu;
TransportPolicy g_policy;
std::atomic<bool> g_io_abort{false};
std::mutex g_io_mu;
std::deque<std::uintptr_t> g_io_socks;  // recent outbound sockets (bounded)

#ifdef _WIN32
using SockT = SOCKET;
void close_sock(SockT s) { closesocket(s); }
constexpr SockT kBad = INVALID_SOCKET;
#else
using SockT = int;
void close_sock(SockT s) { if (s >= 0) close(s); }
constexpr SockT kBad = -1;
#endif

bool split_host_port(const std::string& ep, std::string& host, std::uint16_t& port) {
    const auto c = ep.rfind(':');
    if (c == std::string::npos || c == 0 || c + 1 >= ep.size() || ep.size() - c - 1 > 5) {
        return false;
    }
    host = ep.substr(0, c);
    unsigned long p = 0;
    for (std::size_t i = c + 1; i < ep.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(ep[i]))) return false;
        p = p * 10 + static_cast<unsigned long>(ep[i] - '0');
    }
    if (p == 0 || p > 65535) return false;
    port = static_cast<std::uint16_t>(p);
    return true;
}

bool is_ipv4_host(const std::string& h) {
    in_addr a{};
    return inet_pton(AF_INET, h.c_str(), &a) == 1;
}

bool recv_exact(SockT s, unsigned char* buf, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const auto r = ::recv(s, reinterpret_cast<char*>(buf + got), static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += static_cast<std::size_t>(r);
    }
    return true;
}

bool connect_ipv4(const std::string& ip, std::uint16_t port, int timeout_ms, SockT& out) {
    SockT s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kBad) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1 ||
        !socket_connect(static_cast<std::uintptr_t>(s), &addr, sizeof(addr), timeout_ms)) {
        close_sock(s);
        return false;
    }
    out = s;
    return true;
}

} // namespace

bool is_onion_v3_host(const std::string& host) {
    static const std::string kSuffix = ".onion";
    if (host.size() != 56 + kSuffix.size()) return false;
    if (host.compare(56, kSuffix.size(), kSuffix) != 0) return false;
    for (std::size_t i = 0; i < 56; ++i) {
        const char c = host[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '2' && c <= '7'))) return false;
    }
    // v3 addresses always end with version byte 3 -> last base32 char is 'd'.
    return host[55] == 'd';
}

bool is_onion_v3_endpoint(const std::string& endpoint) {
    std::string host;
    std::uint16_t port = 0;
    return split_host_port(endpoint, host, port) && is_onion_v3_host(host);
}

bool is_peer_endpoint(const std::string& endpoint) {
    std::string host;
    std::uint16_t port = 0;
    if (!split_host_port(endpoint, host, port)) return false;
    if (is_onion_v3_host(host)) return true;
    // strict dotted quad (inet_pton accepts only canonical a.b.c.d on POSIX)
    return is_ipv4_host(host);
}

void set_transport_policy(const TransportPolicy& p) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_policy = p;
}

TransportPolicy transport_policy() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_policy;
}

namespace {
void track_io_socket(std::uintptr_t s) {
    std::lock_guard<std::mutex> lk(g_io_mu);
    g_io_socks.push_back(s);
    while (g_io_socks.size() > 512) g_io_socks.pop_front();
}
} // namespace

void abort_all_io() {
    g_io_abort.store(true);
    std::lock_guard<std::mutex> lk(g_io_mu);
    for (auto s : g_io_socks) {
#ifdef _WIN32
        shutdown(static_cast<SOCKET>(s), SD_BOTH);
#else
        ::shutdown(static_cast<int>(s), SHUT_RDWR);
#endif
    }
    g_io_socks.clear();
}

bool io_aborted() { return g_io_abort.load(); }

RouteKind route_for(const std::string& host, const TransportPolicy& p) {
    const bool onion = is_onion_v3_host(host);
    if (onion) {
        return p.socks_proxy.empty() ? RouteKind::Refused : RouteKind::Socks;
    }
    if (p.onion_only) {
        return RouteKind::Refused;
    }
    if (p.proxy_all && !p.socks_proxy.empty()) {
        return RouteKind::Socks;
    }
    return RouteKind::Direct;
}

bool socks5_handshake(std::uintptr_t sock_raw, const std::string& host, std::uint16_t port, std::string& error) {
    const SockT s = static_cast<SockT>(sock_raw);
    if (host.empty() || host.size() > 255) {
        error = "socks5: bad host";
        return false;
    }
    const unsigned char greet[3] = {0x05, 0x01, 0x00};
    if (!socket_send_all(sock_raw, reinterpret_cast<const char*>(greet), sizeof(greet))) {
        error = "socks5: greet send failed";
        return false;
    }
    unsigned char rep[2]{};
    if (!recv_exact(s, rep, 2) || rep[0] != 0x05 || rep[1] != 0x00) {
        error = "socks5: proxy refused no-auth";
        return false;
    }
    std::vector<unsigned char> req;
    req.reserve(7 + host.size());
    req.push_back(0x05);
    req.push_back(0x01);  // CONNECT
    req.push_back(0x00);
    req.push_back(0x03);  // ATYP domain: resolved by the proxy (no local DNS)
    req.push_back(static_cast<unsigned char>(host.size()));
    req.insert(req.end(), host.begin(), host.end());
    req.push_back(static_cast<unsigned char>(port >> 8));
    req.push_back(static_cast<unsigned char>(port & 0xFF));
    if (!socket_send_all(sock_raw, reinterpret_cast<const char*>(req.data()), req.size())) {
        error = "socks5: connect send failed";
        return false;
    }
    unsigned char hdr[4]{};
    if (!recv_exact(s, hdr, 4) || hdr[0] != 0x05) {
        error = "socks5: bad reply";
        return false;
    }
    if (hdr[1] != 0x00) {
        error = "socks5: connect failed code " + std::to_string(static_cast<int>(hdr[1]));
        return false;
    }
    std::size_t skip = 0;
    if (hdr[3] == 0x01) {
        skip = 4;
    } else if (hdr[3] == 0x04) {
        skip = 16;
    } else if (hdr[3] == 0x03) {
        unsigned char l = 0;
        if (!recv_exact(s, &l, 1)) {
            error = "socks5: bad bind addr";
            return false;
        }
        skip = l;
    } else {
        error = "socks5: bad atyp";
        return false;
    }
    std::vector<unsigned char> sink(skip + 2);
    if (!recv_exact(s, sink.data(), sink.size())) {
        error = "socks5: short bind addr";
        return false;
    }
    return true;
}

bool transport_connect(const std::string& host,
                       std::uint16_t port,
                       int connect_timeout_ms,
                       std::uintptr_t& out_sock,
                       std::string& error) {
    const auto pol = transport_policy();
    const auto kind = route_for(host, pol);
    SockT s = kBad;
    if (g_io_abort.load()) {
        error = "shutting down";
        return false;
    }
    if (kind == RouteKind::Refused) {
        error = is_onion_v3_host(host) ? "onion peer needs a SOCKS proxy" : "clearnet refused (onion_only)";
        return false;
    }
    if (kind == RouteKind::Direct) {
        if (!connect_ipv4(host, port, connect_timeout_ms, s)) {
            error = "connect failed";
            return false;
        }
        track_io_socket(static_cast<std::uintptr_t>(s));
        out_sock = static_cast<std::uintptr_t>(s);
        return true;
    }
    std::string ph;
    std::uint16_t pp = 0;
    if (!split_host_port(pol.socks_proxy, ph, pp) || !is_ipv4_host(ph)) {
        error = "bad socks proxy";
        return false;
    }
    if (!connect_ipv4(ph, pp, connect_timeout_ms, s)) {
        error = "socks proxy unreachable";
        return false;
    }
    track_io_socket(static_cast<std::uintptr_t>(s));
    // Onion circuits can take a while; caller sets the overall IO timeouts.
#ifdef _WIN32
    DWORD tmo = 60000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof(tmo));
#else
    timeval tv{};
    tv.tv_sec = 60;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    if (!socks5_handshake(static_cast<std::uintptr_t>(s), host, port, error)) {
        close_sock(s);
        return false;
    }
    out_sock = static_cast<std::uintptr_t>(s);
    return true;
}

} // namespace addition
