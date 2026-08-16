#include "addition/rpc_network_server.hpp"

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

constexpr std::size_t kMaxRpcLineBytes = 32768;
constexpr int kRecvTimeoutMs = 4000;
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
        close_socket(static_cast<SocketT>(listen_socket_));
        listen_socket_ = static_cast<uintptr_t>(-1);
    }
#else
    if (listen_socket_ != -1) {
        close_socket(listen_socket_);
        listen_socket_ = -1;
    }
#endif

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

        {
            std::lock_guard<std::mutex> lk(clients_mu_);
            client_threads_.emplace_back([this, client]() {
                this->serve_client(static_cast<std::uintptr_t>(client));
            });
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
    std::vector<std::thread> local;
    {
        std::lock_guard<std::mutex> lk(clients_mu_);
        local.swap(client_threads_);
    }
    for (auto& th : local) {
        if (th.joinable()) {
            th.join();
        }
    }
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

    char buffer[kMaxRpcLineBytes + 1]{};
#ifdef _WIN32
    const int received = recv(client, buffer, sizeof(buffer) - 1, 0);
#else
    const int received = static_cast<int>(recv(client, buffer, sizeof(buffer) - 1, 0));
#endif
    if (received > 0) {
        if (static_cast<std::size_t>(received) > kMaxRpcLineBytes) {
            const std::string resp = "error: request too large\n";
#ifdef _WIN32
            send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
#else
            send(client, resp.c_str(), resp.size(), 0);
#endif
            close_socket(client);
            return;
        }

        std::string req(buffer, buffer + received);
        req = trim_eol(req);
        if (req.size() > kMaxRpcLineBytes) {
            const std::string resp = "error: request too large\n";
#ifdef _WIN32
            send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
#else
            send(client, resp.c_str(), resp.size(), 0);
#endif
            close_socket(client);
            return;
        }

        std::string resp = handler_(req);
        if (resp.rfind("HTTP/1.", 0) != 0 && (resp.empty() || resp.back() != '\n')) {
            resp.push_back('\n');
        }
#ifdef _WIN32
        if (resp.size() > kMaxRpcLineBytes) {
            const std::string too_big = "error: response too large\n";
            send(client, too_big.c_str(), static_cast<int>(too_big.size()), 0);
        } else {
            send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
        }
#else
        if (resp.size() > kMaxRpcLineBytes) {
            const std::string too_big = "error: response too large\n";
            send(client, too_big.c_str(), too_big.size(), 0);
        } else {
            send(client, resp.c_str(), resp.size(), 0);
        }
#endif
    }

    close_socket(client);
}

} // namespace addition
