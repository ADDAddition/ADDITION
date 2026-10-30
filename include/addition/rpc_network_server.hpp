#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <thread>
#include <vector>

namespace addition {

class RpcNetworkServer {
public:
    using Handler = std::function<std::string(const std::string&)>;

    RpcNetworkServer(std::string bind_ip, std::uint16_t port, Handler handler);
    ~RpcNetworkServer();

    bool start(std::string& error);
    void stop();
    // Persistent P2P connections: after a plain-line reply, keep reading further
    // lines on the same socket until the peer closes or idles for idle_ms.
    void set_keepalive(int idle_ms) { keepalive_idle_ms_ = idle_ms; }

    struct Stats {
        std::size_t active{0};
        std::size_t rejected_total_cap{0};
        std::size_t rejected_ip_cap{0};
        std::size_t large_slots_in_use{0};
    };
    Stats stats() const;

private:
    void run_loop();
    void serve_client(std::uintptr_t client_raw);
    void join_client_threads();

    std::string bind_ip_;
    std::uint16_t port_;
    Handler handler_;

    std::atomic<bool> running_{false};
    int keepalive_idle_ms_{0};
    std::thread worker_;
    struct ClientThread {
        std::thread th;
        std::shared_ptr<std::atomic<bool>> done;
    };
    void reap_finished_locked();
    bool admit(const std::string& ip);
    void release(const std::string& ip);

    mutable std::mutex clients_mu_;
    std::vector<ClientThread> client_threads_;
    std::unordered_map<std::string, std::size_t> per_ip_;
    std::size_t active_{0};
    std::size_t rejected_total_cap_{0};
    std::size_t rejected_ip_cap_{0};
    std::atomic<std::size_t> large_slots_{0};
#ifdef _WIN32
    uintptr_t listen_socket_{static_cast<uintptr_t>(-1)};
#else
    int listen_socket_{-1};
#endif
};

} // namespace addition
