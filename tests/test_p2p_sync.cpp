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

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
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
        if (!node_a.miner.mine_next_block("miner1", 8, 2, mined, err)) {
            return fail("mine A block: " + err);
        }
    }
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
    if (!node_b.node.sync_once(err)) {
        p2p_a->stop();
        return fail("sync_once: " + err);
    }
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

} // namespace

int main() {
    if (int rc = test_genesis_zero_tx_decode()) {
        return rc;
    }
    if (int rc = test_hello_reply_not_shadowed()) {
        return rc;
    }
    if (int rc = test_two_node_socket_sync()) {
        return rc;
    }
    std::cout << "test_p2p_sync ok\n";
    return 0;
}
