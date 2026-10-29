#include "addition/rpc_network_server.hpp"

#include "addition/net_io.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>

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

constexpr int kRecvTimeoutMs = 15000;
constexpr int kSendTimeoutMs = 60000;

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

std::string trim_eol(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

} // namespace

RpcNetworkServer::RpcNetworkServer(std::string bind_ip, std::uint16_t port, Handler handler)
    : bind_ip_(std::move(bind_ip)), port_(port), handler_(std::move(handler)) {}

RpcNetworkServer::~RpcNetworkServer() {
    stop();
}

bool RpcNetworkServer::start(std::string& error) {
    if (running_) {
        return true;
    }

#ifdef _WIN32
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        error = "WSAStartup failed";
        return false;
    }
#endif

    running_ = true;
    worker_ = std::thread(&RpcNetworkServer::run_loop, this);
    return true;
}

void RpcNetworkServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

#ifdef _WIN32
    if (listen_socket_ != static_cast<uintptr_t>(-1)) {
        shutdown(static_cast<SocketT>(listen_socket_), SD_BOTH);
        close_socket(static_cast<SocketT>(listen_socket_));
        listen_socket_ = static_cast<uintptr_t>(-1);
    }
#else
    if (listen_socket_ != -1) {
        shutdown(listen_socket_, SHUT_RDWR);
        close_socket(listen_socket_);
        listen_socket_ = -1;
    }
#endif

    // Unblock accept() if close() did not wake it.
    {
        SocketT poke = ::socket(AF_INET, SOCK_STREAM, 0);
        if (poke != kInvalidSocket) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port_);
            inet_pton(AF_INET, bind_ip_.c_str(), &addr.sin_addr);
#ifdef _WIN32
            DWORD ms = 200;
            setsockopt(poke, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200000;
            setsockopt(poke, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
            ::connect(poke, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            close_socket(poke);
        }
    }

    if (worker_.joinable()) {
        worker_.join();
    }
    join_client_threads();

#ifdef _WIN32
    WSACleanup();
#endif
}

void RpcNetworkServer::run_loop() {
    SocketT server = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server == kInvalidSocket) {
        running_ = false;
        return;
    }

#ifdef _WIN32
    listen_socket_ = static_cast<uintptr_t>(server);
#else
    listen_socket_ = server;
#endif

    int opt = 1;
#ifdef _WIN32
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (inet_pton(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) {
        close_socket(server);
        running_ = false;
        return;
    }

    if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(server);
        running_ = false;
        return;
    }

    if (::listen(server, 32) != 0) {
        close_socket(server);
        running_ = false;
        return;
    }

    while (running_) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif

        SocketT client = ::accept(server, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client == kInvalidSocket) {
            if (!running_) {
                break;
            }
            continue;
        }

        char ipbuf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
        const std::string ip(ipbuf);
        if (!admit(ip)) {
            // Over cap: drop immediately, no thread, no buffer.
            close_socket(client);
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(clients_mu_);
            reap_finished_locked();
            auto done = std::make_shared<std::atomic<bool>>(false);
            ClientThread ct;
            ct.done = done;
            ct.th = std::thread([this, client, ip, done]() {
                this->serve_client(static_cast<std::uintptr_t>(client));
                this->release(ip);
                done->store(true);
            });
            client_threads_.push_back(std::move(ct));
        }
    }

    close_socket(server);
#ifdef _WIN32
    listen_socket_ = static_cast<uintptr_t>(-1);
#else
    listen_socket_ = -1;
#endif
}

void RpcNetworkServer::join_client_threads() {
    std::vector<ClientThread> local;
    {
        std::lock_guard<std::mutex> lk(clients_mu_);
        local.swap(client_threads_);
    }
    for (auto& ct : local) {
        if (ct.th.joinable()) {
            ct.th.join();
        }
    }
}

void RpcNetworkServer::reap_finished_locked() {
    for (auto it = client_threads_.begin(); it != client_threads_.end();) {
        if (it->done->load()) {
            if (it->th.joinable()) {
                it->th.join();
            }
            it = client_threads_.erase(it);
        } else {
            ++it;
        }
    }
}

bool RpcNetworkServer::admit(const std::string& ip) {
    std::lock_guard<std::mutex> lk(clients_mu_);
    if (active_ >= kMaxInboundConnections) {
        ++rejected_total_cap_;
        return false;
    }
    const bool loopback = ip.rfind("127.", 0) == 0;
    const auto cap = loopback ? kMaxInboundLoopback : kMaxInboundPerIp;
    auto& n = per_ip_[ip];
    if (n >= cap) {
        ++rejected_ip_cap_;
        return false;
    }
    ++n;
    ++active_;
    return true;
}

void RpcNetworkServer::release(const std::string& ip) {
    std::lock_guard<std::mutex> lk(clients_mu_);
    auto it = per_ip_.find(ip);
    if (it != per_ip_.end()) {
        if (--it->second == 0) {
            per_ip_.erase(it);
        }
    }
    if (active_ > 0) {
        --active_;
    }
}

RpcNetworkServer::Stats RpcNetworkServer::stats() const {
    std::lock_guard<std::mutex> lk(clients_mu_);
    Stats s;
    s.active = active_;
    s.rejected_total_cap = rejected_total_cap_;
    s.rejected_ip_cap = rejected_ip_cap_;
    s.large_slots_in_use = large_slots_.load();
    return s;
}

void RpcNetworkServer::serve_client(std::uintptr_t client_raw) {
    const SocketT client = static_cast<SocketT>(client_raw);

#ifdef _WIN32
    DWORD recv_ms = static_cast<DWORD>(kRecvTimeoutMs);
    DWORD send_ms = static_cast<DWORD>(kSendTimeoutMs);
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recv_ms), sizeof(recv_ms));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_ms), sizeof(send_ms));
#else
    timeval recv_tv{};
    recv_tv.tv_sec = kRecvTimeoutMs / 1000;
    recv_tv.tv_usec = (kRecvTimeoutMs % 1000) * 1000;
    timeval send_tv{};
    send_tv.tv_sec = kSendTimeoutMs / 1000;
    send_tv.tv_usec = (kSendTimeoutMs % 1000) * 1000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &recv_tv, sizeof(recv_tv));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &send_tv, sizeof(send_tv));
#endif

    // Per-connection memory: only kMaxLargeSlots connections at once may buffer
    // a full 4 MiB line; the others are capped at kSmallLineBytes.
    std::size_t limit = kSmallLineBytes;
    bool large = false;
    {
        auto cur = large_slots_.load();
        while (cur < kMaxLargeSlots) {
            if (large_slots_.compare_exchange_weak(cur, cur + 1)) {
                large = true;
                limit = kMaxLineBytes;
                break;
            }
        }
    }
    struct SlotGuard {
        std::atomic<std::size_t>& c;
        bool on;
        ~SlotGuard() {
            if (on) {
                c.fetch_sub(1);
            }
        }
    } slot_guard{large_slots_, large};

    std::string req;
    if (!socket_recv_request(static_cast<std::uintptr_t>(client), req, limit)) {
        close_socket(client);
        return;
    }
    if (req.rfind("GET ", 0) != 0 && req.rfind("POST ", 0) != 0 &&
        req.rfind("OPTIONS ", 0) != 0 && req.rfind("HEAD ", 0) != 0) {
        req = trim_eol(req);
    }
    if (req.size() > limit) {
        const std::string resp = "error: request too large\n";
        socket_send_all(static_cast<std::uintptr_t>(client), resp.c_str(), resp.size());
        close_socket(client);
        return;
    }

    std::string resp = handler_(req);
    if (resp.rfind("HTTP/1.", 0) != 0 && (resp.empty() || resp.back() != '\n')) {
        resp.push_back('\n');
    }
    if (resp.size() > kMaxLineBytes) {
        const std::string too_big = "error: response too large\n";
        socket_send_all(static_cast<std::uintptr_t>(client), too_big.c_str(), too_big.size());
    } else {
        socket_send_all(static_cast<std::uintptr_t>(client), resp.c_str(), resp.size());
    }

    close_socket(client);
}

} // namespace addition
