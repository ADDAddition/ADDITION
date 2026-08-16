#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
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

private:
    void run_loop();
    void serve_client(std::uintptr_t client_raw);
    void join_client_threads();

    std::string bind_ip_;
    std::uint16_t port_;
    Handler handler_;

    std::atomic<bool> running_{false};
    std::thread worker_;
    std::mutex clients_mu_;
    std::vector<std::thread> client_threads_;
#ifdef _WIN32
    uintptr_t listen_socket_{static_cast<uintptr_t>(-1)};
#else
    int listen_socket_{-1};
#endif
};

} // namespace addition
