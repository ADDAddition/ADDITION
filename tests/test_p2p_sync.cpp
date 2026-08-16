#include "addition/chain.hpp"
#include "addition/config.hpp"
#include "addition/consensus_engine.hpp"
#include "addition/crypto.hpp"
#include "addition/decentralized_node.hpp"
#include "addition/mempool.hpp"
#include "addition/miner.hpp"
#include "addition/p2p.hpp"
#include "addition/rpc_network_server.hpp"
#include "addition/wallet_keys.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

int fail(const std::string& msg) {
    std::cerr << "test failed: " << msg << '\n';
    return 1;
}

bool port_open(const char* ip, std::uint16_t port) {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
#endif
    const int sock = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (sock < 0) {
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }
#ifdef _WIN32
    DWORD ms = 400;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 400000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    const bool ok = ::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return ok;
}

bool wait_port(const char* ip, std::uint16_t port, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (port_open(ip, port)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

std::uint64_t now_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

struct NodeKit {
    addition::Chain chain;
    addition::Mempool mempool;
    addition::PeerNetwork peers;
    addition::ConsensusEngine consensus;
    addition::WalletKeys keys;
    addition::DecentralizedNode node;
    addition::Miner miner;

    explicit NodeKit(addition::ChainConfig cfg)
        : chain(std::move(cfg)),
          keys(addition::generate_wallet_keys()),
          node("self", keys.public_key, keys.private_key, chain, mempool, peers, consensus),
          miner(chain, mempool) {}
};

int test_genesis_zero_tx_decode() {
    std::cerr << "test_p2p_sync: genesis decode\n";
    NodeKit kit(addition::testnet_chain_config());
    if (kit.chain.height() != 0) {
        return fail("fresh chain height");
    }
    if (!kit.chain.genesis_block().transactions.empty()) {
        return fail("live genesis must have tx_count=0");
    }
    if (kit.chain.genesis_block().header.timestamp != 1763000000ULL) {
        return fail("genesis timestamp");
    }

    std::string payload;
    std::string err;
    if (!kit.node.get_block_payload(0, payload, err)) {
        return fail("get_block_payload 0: " + err);
    }
    addition::Block decoded{};
    if (!kit.node.decode_block_payload(payload, decoded, err)) {
        return fail("decode genesis: " + err);
    }
    if (decoded.header.height != 0 || !decoded.transactions.empty()) {
        return fail("decoded genesis shape");
    }
    if (decoded.header.timestamp != 1763000000ULL) {
        return fail("decoded genesis timestamp");
    }
    return 0;
}

int test_hello_reply_not_shadowed() {
    std::cerr << "test_p2p_sync: hello reply\n";
    NodeKit miner(addition::testnet_chain_config());
    std::string mined;
    std::string err;
    if (!miner.miner.mine_next_block("miner1", 8, 1, mined, err)) {
        return fail("mine for gossip: " + err);
    }

    std::string payload;
    if (!miner.node.get_block_payload(1, payload, err)) {
        return fail("get mined payload: " + err);
    }

    NodeKit server(addition::testnet_chain_config());
    if (!server.node.ingest_block_payload(payload, err)) {
        return fail("server ingest: " + err);
    }
    const auto leftover = server.node.pull_outbound_messages();
    if (leftover.empty() || leftover.front().rfind("BLK|", 0) != 0) {
        return fail("expected leftover BLK gossip after ingest");
    }
    if (!server.node.ingest_block_payload(payload, err)) {
        // second ingest is a duplicate hash; keep a BLK announce by submitting again via encode path
    }
    // Re-ingest is a no-op once seen; push is already consumed. Ingest a fresh copy on a clean server:
    NodeKit live(addition::testnet_chain_config());
    if (!live.node.ingest_block_payload(payload, err)) {
        return fail("live ingest: " + err);
    }

    const auto ts = now_seconds();
    const std::string nonce = "unit-hello";
    const std::string hello_body = std::string("2|ADDITION_TESTNET_V1|") + std::to_string(ts) + "|" + nonce + "|" +
                                   miner.keys.public_key;
    const auto sig = addition::sign_message_hybrid(miner.keys.private_key, hello_body);
    const std::string hello = "HELLO|2|ADDITION_TESTNET_V1|" + std::to_string(ts) + "|" + nonce + "|" +
                              miner.keys.public_key + "|" + sig;

    std::string reply;
    if (!live.node.handle_inbound_message("n-client", hello, reply, err)) {
        return fail("HELLO: " + err);
    }
    if (reply.rfind("HELLO_ACK|", 0) != 0) {
        return fail("HELLO reply was not HELLO_ACK: " + reply);
    }
    if (reply.rfind("BLK|", 0) == 0) {
        return fail("HELLO reply shadowed by gossip");
    }
    const auto gossip = live.node.pull_outbound_messages();
    bool saw_blk = false;
    for (const auto& msg : gossip) {
        if (msg.rfind("HELLO_ACK|", 0) == 0) {
            return fail("HELLO_ACK leaked into gossip outbound");
        }
        if (msg.rfind("BLK|", 0) == 0) {
            saw_blk = true;
        }
    }
    if (!saw_blk) {
        return fail("BLK gossip was discarded by HELLO");
    }
    return 0;
}

int test_two_node_socket_sync() {
    std::cerr << "test_p2p_sync: two-node socket sync\n";
    NodeKit node_a(addition::testnet_chain_config());
    NodeKit node_b(addition::testnet_chain_config());

    if (node_a.chain.genesis_block().transactions.size() != 0 ||
        node_b.chain.genesis_block().transactions.size() != 0) {
        return fail("both nodes must start from zero-tx genesis");
    }
    if (addition::hash_block_header(node_a.chain.genesis_block().header) !=
        addition::hash_block_header(node_b.chain.genesis_block().header)) {
        return fail("genesis hash mismatch between A and B");
    }

    std::string err;
    for (int i = 0; i < 3; ++i) {
        std::string mined;
        std::cerr << "test_p2p_sync: mining A block " << (i + 1) << "\n";
        if (!node_a.miner.mine_next_block("miner1", 8, 2, mined, err)) {
            return fail("mine A block: " + err);
        }
    }
    std::cerr << "test_p2p_sync: A height=" << node_a.chain.height() << "\n";
    if (node_a.chain.height() < 3) {
        return fail("node A did not reach height 3");
    }
    if (node_b.chain.height() != 0) {
        return fail("node B must start at height 0");
    }

    std::uint16_t port = 0;
    std::unique_ptr<addition::RpcNetworkServer> p2p_a;
    for (std::uint16_t candidate : {std::uint16_t{29147}, std::uint16_t{29148}, std::uint16_t{29149}}) {
        auto server = std::make_unique<addition::RpcNetworkServer>(
            "127.0.0.1", candidate, [&](const std::string& line) { return node_a.node.handle_p2p_line(line); });
        std::string start_err;
        if (!server->start(start_err)) {
            continue;
        }
        if (wait_port("127.0.0.1", candidate, 4000)) {
            port = candidate;
            p2p_a = std::move(server);
            break;
        }
        server->stop();
    }
    if (!p2p_a) {
        return fail("p2p port did not open");
    }

    if (!node_b.peers.add_peer("127.0.0.1:" + std::to_string(port))) {
        p2p_a->stop();
        return fail("addpeer");
    }
    std::cerr << "test_p2p_sync: sync_once on 127.0.0.1:" << port << "\n";
    if (!node_b.node.sync_once(err)) {
        p2p_a->stop();
        return fail("sync_once: " + err);
    }
    std::cerr << "test_p2p_sync: stopping p2p A\n";
    p2p_a->stop();

    if (node_b.chain.height() != node_a.chain.height()) {
        return fail("B height " + std::to_string(node_b.chain.height()) + " != A height " +
                    std::to_string(node_a.chain.height()));
    }
    if (addition::hash_block_header(node_b.chain.tip().header) !=
        addition::hash_block_header(node_a.chain.tip().header)) {
        return fail("tip hash mismatch after sync");
    }
    return 0;
}

int test_persist_wan_truncation();

std::string persist_handle_line(addition::DecentralizedNode& node, const std::string& line) {
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
    if (!node.handle_inbound_message(peer, payload, err)) {
        return std::string("error: ") + err;
    }
    const auto outbound = node.pull_outbound_messages();
    if (outbound.empty()) {
        return "ok";
    }
    return "ok:" + outbound.front();
}

int test_persist_seed_dialect() {
    std::cerr << "test_p2p_sync: persist-era seed dialect\n";
    NodeKit node_a(addition::testnet_chain_config());
    NodeKit persist(addition::testnet_chain_config());
    NodeKit node_b(addition::testnet_chain_config());

    std::string err;
    for (int i = 0; i < 3; ++i) {
        std::string mined;
        if (!node_a.miner.mine_next_block("miner1", 8, 2, mined, err)) {
            return fail("persist mine: " + err);
        }
        std::string payload;
        if (!node_a.node.get_block_payload(static_cast<std::uint64_t>(i + 1), payload, err)) {
            return fail("persist payload: " + err);
        }
        if (!persist.node.ingest_block_payload(payload, err)) {
            return fail("persist ingest: " + err);
        }
    }
    if (persist.peers.add_peer("self")) {
        for (int i = 0; i < 5; ++i) {
            persist.peers.mark_peer_bad("self");
        }
    }
    if (!persist.peers.is_banned("self")) {
        return fail("persist fixture must ban self");
    }

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return fail("persist listen socket");
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    std::uint16_t port = 0;
    for (std::uint16_t candidate : {std::uint16_t{29247}, std::uint16_t{29248}, std::uint16_t{29249}}) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(candidate);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
            ::listen(listen_fd, 16) == 0) {
            port = candidate;
            break;
        }
    }
    if (port == 0) {
        close(listen_fd);
        return fail("persist bind");
    }

    std::atomic<bool> running{true};
    std::thread worker([&]() {
        while (running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client < 0) {
                continue;
            }
            std::string req;
            char buffer[4096];
            while (req.find('\n') == std::string::npos && req.size() < 262144) {
                const int n = static_cast<int>(::recv(client, buffer, sizeof(buffer), 0));
                if (n <= 0) {
                    break;
                }
                req.append(buffer, buffer + n);
            }
            if (!req.empty()) {
                while (!req.empty() && (req.back() == '\n' || req.back() == '\r')) {
                    req.pop_back();
                }
                std::string resp = persist_handle_line(persist.node, req);
                if (resp.empty() || resp.back() != '\n') {
                    resp.push_back('\n');
                }
                ::send(client, resp.data(), resp.size(), 0);
            }
            close(client);
        }
    });

    if (!node_b.peers.add_peer("127.0.0.1:" + std::to_string(port))) {
        running = false;
        close(listen_fd);
        worker.join();
        return fail("persist addpeer");
    }
    if (!node_b.node.sync_once(err)) {
        running = false;
        close(listen_fd);
        worker.join();
        return fail("persist sync_once: " + err);
    }
    running = false;
    // wake accept
    {
        const int poke = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
        if (poke >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            ::connect(poke, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            close(poke);
        }
    }
    close(listen_fd);
    worker.join();

    if (node_b.chain.height() != persist.chain.height()) {
        return fail("persist dialect: B height " + std::to_string(node_b.chain.height()) +
                    " != persist height " + std::to_string(persist.chain.height()));
    }
    return 0;
}

int test_persist_wan_truncation() {
    std::cerr << "test_p2p_sync: persist WAN one-recv truncation\n";
    NodeKit persist(addition::testnet_chain_config());
    NodeKit node_b(addition::testnet_chain_config());

    std::string err;
    std::string mined;
    if (!persist.miner.mine_next_block("miner1", 8, 2, mined, err)) {
        return fail("wan persist mine: " + err);
    }

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return fail("wan persist listen socket");
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    std::uint16_t port = 0;
    for (std::uint16_t candidate : {std::uint16_t{29347}, std::uint16_t{29348}, std::uint16_t{29349}}) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(candidate);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
            ::listen(listen_fd, 16) == 0) {
            port = candidate;
            break;
        }
    }
    if (port == 0) {
        close(listen_fd);
        return fail("wan persist bind");
    }

    std::atomic<bool> running{true};
    std::thread worker([&]() {
        while (running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client < 0) {
                continue;
            }
            // First IPv4 segment is typically ~1460 bytes. Persist recv returns then.
            char buffer[1460];
            const int n = static_cast<int>(::recv(client, buffer, sizeof(buffer), 0));
            if (n > 0) {
                std::string req(buffer, buffer + n);
                while (!req.empty() && (req.back() == '\n' || req.back() == '\r')) {
                    req.pop_back();
                }
                std::string resp = persist_handle_line(persist.node, req);
                if (resp.empty() || resp.back() != '\n') {
                    resp.push_back('\n');
                }
                ::send(client, resp.data(), resp.size(), 0);
            }
            close(client);
        }
    });

    if (!node_b.peers.add_peer("127.0.0.1:" + std::to_string(port))) {
        running = false;
        close(listen_fd);
        worker.join();
        return fail("wan persist addpeer");
    }
    const bool synced = node_b.node.sync_once(err);
    running = false;
    {
        const int poke = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
        if (poke >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            ::connect(poke, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
            close(poke);
        }
    }
    close(listen_fd);
    worker.join();

    if (synced) {
        return fail("WAN persist one-recv must not report sync success at height 0");
    }
    if (err.find("HELLO") == std::string::npos &&
        err.find("handshake") == std::string::npos &&
        err.find("public-rpc") == std::string::npos) {
        return fail("WAN persist error was not a handshake failure: " + err);
    }
    if (node_b.chain.height() != 0) {
        return fail("WAN persist must leave the home node at height 0");
    }
    return 0;
}

void poke_port(std::uint16_t port) {
    const int poke = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (poke >= 0) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        ::connect(poke, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        close(poke);
    }
}

int bind_listen(int listen_fd, std::uint16_t& port, const std::uint16_t* candidates, int n) {
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    for (int i = 0; i < n; ++i) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(candidates[i]);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 &&
            ::listen(listen_fd, 16) == 0) {
            port = candidates[i];
            return 0;
        }
    }
    return -1;
}

int test_home_like_wan_hello() {
    std::cerr << "test_p2p_sync: home-like WAN paced HELLO\n";
    NodeKit node_a(addition::testnet_chain_config());
    NodeKit node_b(addition::testnet_chain_config());
    std::string err;
    for (int i = 0; i < 3; ++i) {
        std::string mined;
        if (!node_a.miner.mine_next_block("miner1", 8, 2, mined, err)) {
            return fail("home-like mine: " + err);
        }
    }

    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return fail("home-like listen");
    }
    std::uint16_t port = 0;
    const std::uint16_t candidates[] = {29447, 29448, 29449};
    if (bind_listen(listen_fd, port, candidates, 3) != 0) {
        close(listen_fd);
        return fail("home-like bind");
    }

    std::atomic<bool> running{true};
    std::thread worker([&]() {
        while (running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client < 0) {
                continue;
            }
            std::string req;
            char buffer[1400];
            bool burst = false;
            while (req.find('\n') == std::string::npos && req.size() < 262144) {
                const int n = static_cast<int>(::recv(client, buffer, sizeof(buffer), 0));
                if (n <= 0) {
                    break;
                }
                if (req.empty() && n == static_cast<int>(sizeof(buffer))) {
                    int avail = 0;
                    ioctl(client, FIONREAD, &avail);
                    if (avail > 4000) {
                        burst = true;
                        break;
                    }
                }
                req.append(buffer, buffer + n);
            }
            if (burst) {
                close(client);
                continue;
            }
            while (!req.empty() && (req.back() == '\n' || req.back() == '\r')) {
                req.pop_back();
            }
            std::string resp = node_a.node.handle_p2p_line(req);
            if (resp.empty() || resp.back() != '\n') {
                resp.push_back('\n');
            }
            ::send(client, resp.data(), resp.size(), 0);
            close(client);
        }
    });

    if (!node_b.peers.add_peer("127.0.0.1:" + std::to_string(port))) {
        running = false;
        poke_port(port);
        close(listen_fd);
        worker.join();
        return fail("home-like addpeer");
    }
    if (!node_b.node.sync_once(err)) {
        running = false;
        poke_port(port);
        close(listen_fd);
        worker.join();
        return fail("home-like sync_once: " + err);
    }
    running = false;
    poke_port(port);
    close(listen_fd);
    worker.join();

    if (node_b.chain.height() != node_a.chain.height()) {
        return fail("home-like height " + std::to_string(node_b.chain.height()) +
                    " != " + std::to_string(node_a.chain.height()));
    }
    return 0;
}

int test_public_rpc_ingest() {
    std::cerr << "test_p2p_sync: public-rpc getblockraw ingest\n";
    NodeKit node_a(addition::testnet_chain_config());
    NodeKit node_b(addition::testnet_chain_config());
    std::string err;
    for (int i = 0; i < 3; ++i) {
        std::string mined;
        if (!node_a.miner.mine_next_block("miner1", 8, 2, mined, err)) {
            return fail("public-rpc mine: " + err);
        }
    }

    int p2p_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int pub_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (p2p_fd < 0 || pub_fd < 0) {
        if (p2p_fd >= 0) {
            close(p2p_fd);
        }
        if (pub_fd >= 0) {
            close(pub_fd);
        }
        return fail("public-rpc sockets");
    }
    std::uint16_t p2p_port = 0;
    const std::uint16_t p2p_candidates[] = {29547, 29548, 29549};
    if (bind_listen(p2p_fd, p2p_port, p2p_candidates, 3) != 0) {
        close(p2p_fd);
        close(pub_fd);
        return fail("public-rpc p2p bind");
    }
    const std::uint16_t pub_port = static_cast<std::uint16_t>(p2p_port + 10000);
    sockaddr_in pub_addr{};
    pub_addr.sin_family = AF_INET;
    pub_addr.sin_port = htons(pub_port);
    inet_pton(AF_INET, "127.0.0.1", &pub_addr.sin_addr);
    int opt = 1;
    setsockopt(pub_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (::bind(pub_fd, reinterpret_cast<sockaddr*>(&pub_addr), sizeof(pub_addr)) != 0 ||
        ::listen(pub_fd, 16) != 0) {
        close(p2p_fd);
        close(pub_fd);
        return fail("public-rpc pub bind");
    }

    std::atomic<bool> running{true};
    std::thread p2p_worker([&]() {
        while (running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client = ::accept(p2p_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client >= 0) {
                close(client);
            }
        }
    });
    std::thread pub_worker([&]() {
        while (running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client = ::accept(pub_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client < 0) {
                continue;
            }
            std::string req;
            char buffer[4096];
            while (req.find('\n') == std::string::npos && req.size() < 65536) {
                const int n = static_cast<int>(::recv(client, buffer, sizeof(buffer), 0));
                if (n <= 0) {
                    break;
                }
                req.append(buffer, buffer + n);
            }
            while (!req.empty() && (req.back() == '\n' || req.back() == '\r')) {
                req.pop_back();
            }
            std::string cmd = req;
            if (cmd.rfind("GET ", 0) == 0) {
                const auto q = cmd.find("cmd=");
                if (q != std::string::npos) {
                    auto rest = cmd.substr(q + 4);
                    const auto sp = rest.find(' ');
                    if (sp != std::string::npos) {
                        rest.resize(sp);
                    }
                    for (char& c : rest) {
                        if (c == '+') {
                            c = ' ';
                        }
                    }
                    const auto pct = rest.find("%20");
                    if (pct != std::string::npos) {
                        rest.replace(pct, 3, " ");
                    }
                    cmd = rest;
                }
            }
            std::string resp = "error: command disabled on public RPC";
            if (cmd.rfind("getinfo", 0) == 0) {
                resp = "network=testnet height=" + std::to_string(node_a.chain.height());
            } else if (cmd.rfind("getblockraw ", 0) == 0) {
                try {
                    const auto h = std::stoull(cmd.substr(12));
                    std::string payload;
                    std::string perr;
                    if (node_a.node.get_block_payload(h, payload, perr)) {
                        resp = "ok:BLKDATA|" + payload;
                    } else {
                        resp = "error: " + perr;
                    }
                } catch (const std::exception&) {
                    resp = "error: invalid block height";
                }
            }
            if (req.rfind("GET ", 0) == 0) {
                resp = "HTTP/1.0 200 OK\r\nContent-Length: " + std::to_string(resp.size()) +
                       "\r\nConnection: close\r\n\r\n" + resp;
            } else if (resp.empty() || resp.back() != '\n') {
                resp.push_back('\n');
            }
            ::send(client, resp.data(), resp.size(), 0);
            close(client);
        }
    });

    if (!node_b.peers.add_peer("127.0.0.1:" + std::to_string(p2p_port))) {
        running = false;
        poke_port(p2p_port);
        poke_port(pub_port);
        close(p2p_fd);
        close(pub_fd);
        p2p_worker.join();
        pub_worker.join();
        return fail("public-rpc addpeer");
    }
    if (!node_b.node.sync_once(err)) {
        running = false;
        poke_port(p2p_port);
        poke_port(pub_port);
        close(p2p_fd);
        close(pub_fd);
        p2p_worker.join();
        pub_worker.join();
        return fail("public-rpc sync_once: " + err);
    }
    running = false;
    poke_port(p2p_port);
    poke_port(pub_port);
    close(p2p_fd);
    close(pub_fd);
    p2p_worker.join();
    pub_worker.join();

    if (node_b.chain.height() != node_a.chain.height()) {
        return fail("public-rpc height " + std::to_string(node_b.chain.height()) +
                    " != " + std::to_string(node_a.chain.height()));
    }
    return 0;
}

int test_http_port80_ingest() {
    std::cerr << "test_p2p_sync: HTTP :80 ingest when :38545 is closed\n";
    NodeKit node_a(addition::testnet_chain_config());
    NodeKit node_b(addition::testnet_chain_config());
    std::string err;
    for (int i = 0; i < 3; ++i) {
        std::string mined;
        if (!node_a.miner.mine_next_block("miner1", 8, 2, mined, err)) {
            return fail("http80 mine: " + err);
        }
    }

    int p2p_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int http_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (p2p_fd < 0 || http_fd < 0) {
        if (p2p_fd >= 0) {
            close(p2p_fd);
        }
        if (http_fd >= 0) {
            close(http_fd);
        }
        return fail("http80 sockets");
    }
    std::uint16_t p2p_port = 0;
    const std::uint16_t p2p_candidates[] = {29647, 29648, 29649};
    if (bind_listen(p2p_fd, p2p_port, p2p_candidates, 3) != 0) {
        close(p2p_fd);
        close(http_fd);
        return fail("http80 p2p bind");
    }
    std::uint16_t http_port = 0;
    const std::uint16_t http_candidates[] = {18080, 18081, 18082};
    if (bind_listen(http_fd, http_port, http_candidates, 3) != 0) {
        close(p2p_fd);
        close(http_fd);
        return fail("http80 bind (unprivileged stand-in for :80)");
    }
    if (setenv("ADDITION_PUBLIC_HTTP_PORT", std::to_string(http_port).c_str(), 1) != 0) {
        close(p2p_fd);
        close(http_fd);
        return fail("http80 setenv");
    }

    std::atomic<bool> running{true};
    std::thread p2p_worker([&]() {
        while (running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client = ::accept(p2p_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client >= 0) {
                close(client);
            }
        }
    });
    std::thread http_worker([&]() {
        while (running.load()) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            const int client = ::accept(http_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client < 0) {
                continue;
            }
            std::string req;
            char buffer[4096];
            while (req.find("\r\n\r\n") == std::string::npos && req.find("\n\n") == std::string::npos &&
                   req.size() < 65536) {
                const int n = static_cast<int>(::recv(client, buffer, sizeof(buffer), 0));
                if (n <= 0) {
                    break;
                }
                req.append(buffer, buffer + n);
            }
            std::string cmd;
            const auto q = req.find("cmd=");
            if (q != std::string::npos) {
                auto rest = req.substr(q + 4);
                const auto end = rest.find_first_of(" \r\n&");
                if (end != std::string::npos) {
                    rest.resize(end);
                }
                const auto pct = rest.find("%20");
                if (pct != std::string::npos) {
                    rest.replace(pct, 3, " ");
                }
                cmd = rest;
            }
            std::string body = "error: command disabled on public RPC";
            if (cmd.rfind("getinfo", 0) == 0) {
                body = "network=testnet height=" + std::to_string(node_a.chain.height());
            } else if (cmd.rfind("getblockraw ", 0) == 0) {
                try {
                    const auto h = std::stoull(cmd.substr(12));
                    std::string payload;
                    std::string perr;
                    if (node_a.node.get_block_payload(h, payload, perr)) {
                        body = "ok:BLKDATA|" + payload;
                    } else {
                        body = "error: " + perr;
                    }
                } catch (const std::exception&) {
                    body = "error: invalid block height";
                }
            }
            std::string resp = "HTTP/1.0 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
                               "\r\nConnection: close\r\n\r\n" + body;
            ::send(client, resp.data(), resp.size(), 0);
            close(client);
        }
    });

    const bool added = node_b.peers.add_peer("127.0.0.1:" + std::to_string(p2p_port));
    bool synced = false;
    if (added) {
        synced = node_b.node.sync_once(err);
    }
    running = false;
    poke_port(p2p_port);
    poke_port(http_port);
    close(p2p_fd);
    close(http_fd);
    p2p_worker.join();
    http_worker.join();
    unsetenv("ADDITION_PUBLIC_HTTP_PORT");

    if (!added) {
        return fail("http80 addpeer");
    }
    if (!synced) {
        return fail("http80 sync_once: " + err);
    }
    if (node_b.chain.height() != node_a.chain.height() || node_b.chain.height() == 0) {
        return fail("http80 height " + std::to_string(node_b.chain.height()) +
                    " != " + std::to_string(node_a.chain.height()));
    }
    return 0;
}

} // namespace

int test_sync_requires_peer() {
    std::cerr << "test_p2p_sync: sync_once with no peer\n";
    NodeKit kit(addition::testnet_chain_config());
    std::string err;
    if (kit.node.sync_once(err)) {
        return fail("sync_once must fail without a peer");
    }
    if (err.find("no peer") == std::string::npos) {
        return fail("expected no peer, got: " + err);
    }
    return 0;
}

int main() {
    if (int rc = test_sync_requires_peer()) {
        return rc;
    }
    if (int rc = test_genesis_zero_tx_decode()) {
        return rc;
    }
    if (int rc = test_hello_reply_not_shadowed()) {
        return rc;
    }
    if (int rc = test_two_node_socket_sync()) {
        return rc;
    }
    if (int rc = test_persist_seed_dialect()) {
        return rc;
    }
    if (int rc = test_persist_wan_truncation()) {
        return rc;
    }
    if (int rc = test_home_like_wan_hello()) {
        return rc;
    }
    if (int rc = test_public_rpc_ingest()) {
        return rc;
    }
    if (int rc = test_http_port80_ingest()) {
        return rc;
    }
    std::cout << "test_p2p_sync ok\n";
    return 0;
}
