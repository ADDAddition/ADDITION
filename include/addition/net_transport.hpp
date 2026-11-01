#pragma once

#include <cstdint>
#include <string>

namespace addition {

// Tor v3 onion host: 56 base32 chars [a-z2-7] + ".onion".
bool is_onion_v3_host(const std::string& host);
// "<v3>.onion:port"
bool is_onion_v3_endpoint(const std::string& endpoint);
// IPv4 host:port or v3 onion host:port (the only peer address kinds we accept).
bool is_peer_endpoint(const std::string& endpoint);

// Process-wide outbound transport policy for P2P/RPC connections.
struct TransportPolicy {
    std::string socks_proxy;  // "127.0.0.1:9050" or empty (no proxy)
    bool onion_only = false;  // refuse clearnet destinations entirely
    bool proxy_all = false;   // route clearnet IPv4 through the proxy as well
};

void set_transport_policy(const TransportPolicy& p);
TransportPolicy transport_policy();

// Decision for one destination.
enum class RouteKind { Direct, Socks, Refused };
RouteKind route_for(const std::string& host, const TransportPolicy& p);

// Perform a SOCKS5 CONNECT (RFC 1928, no-auth, ATYP=domain) on an already
// connected socket to the proxy. Domain names are sent unresolved, so .onion
// names are resolved inside Tor (no DNS leak). Returns false on any failure.
bool socks5_handshake(std::uintptr_t sock, const std::string& host, std::uint16_t port, std::string& error);

// Open a connected TCP socket to host:port honoring the transport policy.
// Returns true and sets out_sock on success; caller closes the socket.
bool transport_connect(const std::string& host,
                       std::uint16_t port,
                       int connect_timeout_ms,
                       std::uintptr_t& out_sock,
                       std::string& error);

// Shutdown support: abort_all_io() marks the process as stopping and
// shutdown()s every outbound socket opened by transport_connect, so a blocked
// connect/SOCKS/recv returns at once. New connects are refused afterwards.
void abort_all_io();
bool io_aborted();

} // namespace addition
