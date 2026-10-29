// Tests: v3 onion parsing, transport routing policy, SOCKS5 client against a
// fake local proxy, PeerNetwork scoring/bans/seeds/cap/candidates, Dandelion++
// router behaviour, Tor CLI flags, and miner relay byte budget helpers.
#include "addition/block.hpp"
#include "addition/config.hpp"
#include "addition/dandelion.hpp"
#include "addition/net_transport.hpp"
#include "addition/p2p.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::cerr << "FAIL " << __LINE__ << ": " #c "\n";             \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

const std::string kOnion = std::string(55, 'a') + "d.onion";  // syntactically v3

void test_onion() {
    using namespace addition;
    CHECK(is_onion_v3_host(kOnion));
    CHECK(!is_onion_v3_host(std::string(16, 'a') + ".onion"));      // v2
    CHECK(!is_onion_v3_host(std::string(55, 'a') + "1.onion"));      // bad base32
    CHECK(!is_onion_v3_host(std::string(55, 'a') + "a.onion"));      // version nibble
    CHECK(is_onion_v3_endpoint(kOnion + ":19444"));
    CHECK(!is_onion_v3_endpoint(kOnion));
    CHECK(!is_onion_v3_endpoint(kOnion + ":0"));
    CHECK(is_peer_endpoint("1.2.3.4:19444"));
    CHECK(is_peer_endpoint(kOnion + ":19444"));
    CHECK(!is_peer_endpoint("example.com:19444"));
}

void test_route() {
    using namespace addition;
    TransportPolicy none{};
    CHECK(route_for("1.2.3.4", none) == RouteKind::Direct);
    CHECK(route_for(kOnion, none) == RouteKind::Refused);
    TransportPolicy tor{"127.0.0.1:9050", false, false};
    CHECK(route_for(kOnion, tor) == RouteKind::Socks);
    CHECK(route_for("1.2.3.4", tor) == RouteKind::Direct);
    TransportPolicy only{"127.0.0.1:9050", true, true};
    CHECK(route_for("1.2.3.4", only) == RouteKind::Refused);
    CHECK(route_for(kOnion, only) == RouteKind::Socks);
}

// Fake SOCKS5 proxy: validates greeting + CONNECT(domain), replies success,
// then echoes one line back prefixed "ok:".
void test_socks5() {
    using namespace addition;
    int ls = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    CHECK(::bind(ls, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0);
    socklen_t al = sizeof(a);
    ::getsockname(ls, reinterpret_cast<sockaddr*>(&a), &al);
    ::listen(ls, 1);
    const int port = ntohs(a.sin_port);
    std::atomic<bool> proto_ok{false};
    std::string seen_host;
    std::thread srv([&] {
        int c = ::accept(ls, nullptr, nullptr);
        unsigned char b[600];
        if (::recv(c, b, 3, MSG_WAITALL) != 3 || b[0] != 5 || b[1] != 1 || b[2] != 0) { ::close(c); return; }
        const unsigned char rep[2] = {5, 0};
        ::send(c, rep, 2, 0);
        if (::recv(c, b, 5, MSG_WAITALL) != 5 || b[0] != 5 || b[1] != 1 || b[3] != 3) { ::close(c); return; }
        const int n = b[4];
        if (::recv(c, b, n + 2, MSG_WAITALL) != n + 2) { ::close(c); return; }
        seen_host.assign(reinterpret_cast<char*>(b), n);
        const int dport = (b[n] << 8) | b[n + 1];
        proto_ok = (dport == 19444);
        const unsigned char ok[10] = {5, 0, 0, 1, 0, 0, 0, 0, 0, 0};
        ::send(c, ok, 10, 0);
        char line[64];
        ssize_t r = ::recv(c, line, sizeof(line), 0);
        std::string resp = "ok:" + std::string(line, r > 0 ? r : 0);
        ::send(c, resp.data(), resp.size(), 0);
        ::close(c);
    });
    set_transport_policy(TransportPolicy{"127.0.0.1:" + std::to_string(port), false, false});
    std::uintptr_t s = 0;
    std::string err;
    const bool ok = transport_connect(kOnion, 19444, 3000, s, err);
    CHECK(ok);
    if (ok) {
        const std::string msg = "PING\n";
        ::send(static_cast<int>(s), msg.data(), msg.size(), 0);
        char buf[64];
        ssize_t r = ::recv(static_cast<int>(s), buf, sizeof(buf), MSG_WAITALL);
        CHECK(std::string(buf, r > 0 ? r : 0) == "ok:PING\n");
        ::close(static_cast<int>(s));
    } else {
        std::cerr << "socks err: " << err << "\n";
    }
    srv.join();
    ::close(ls);
    CHECK(proto_ok.load());
    CHECK(seen_host == kOnion);  // name passed unresolved (no DNS leak)
    // onion with no proxy must be refused, never resolved locally
    set_transport_policy(TransportPolicy{});
    CHECK(!transport_connect(kOnion, 19444, 500, s, err));
    set_transport_policy(TransportPolicy{"127.0.0.1:9", true, true});
    CHECK(!transport_connect("127.0.0.1", 19444, 500, s, err));  // clearnet refused
    set_transport_policy(TransportPolicy{});
}

void test_peers() {
    using namespace addition;
    PeerNetwork pn;
    pn.set_clock_for_test(1000);
    pn.set_limits(3, 2, 100);
    CHECK(pn.add_seed("10.0.0.1:1"));
    CHECK(pn.add_peer("10.0.0.2:1"));
    CHECK(pn.add_peer("10.0.0.3:1"));
    pn.mark_peer_good("10.0.0.3:1");
    // cap: adding a 4th evicts the lowest non-seed (10.0.0.2)
    CHECK(pn.add_peer("10.0.0.4:1"));
    CHECK(pn.peer_count() == 3);
    CHECK(pn.has_peer("10.0.0.1:1"));
    CHECK(!pn.has_peer("10.0.0.2:1"));
    // ban with expiry
    for (int i = 0; i < 6; ++i) pn.mark_peer_bad("10.0.0.4:1");
    CHECK(pn.is_banned("10.0.0.4:1"));
    CHECK(!pn.add_peer("10.0.0.4:1"));
    pn.set_clock_for_test(1101);
    CHECK(!pn.is_banned("10.0.0.4:1"));
    // seed never dropped, only backoff
    for (int i = 0; i < 10; ++i) pn.mark_peer_bad("10.0.0.1:1");
    CHECK(pn.has_peer("10.0.0.1:1"));
    CHECK(pn.in_backoff("10.0.0.1:1"));
    for (const auto& p : pn.select_peers(0)) CHECK(p != "10.0.0.1:1");
    pn.set_clock_for_test(1200);
    CHECK(!pn.in_backoff("10.0.0.1:1"));
    // candidates capped and promoted only explicitly
    CHECK(pn.add_candidate("10.0.1.1:1"));
    CHECK(pn.add_candidate("10.0.1.2:1"));
    CHECK(!pn.add_candidate("10.0.1.3:1"));
    CHECK(!pn.has_peer("10.0.1.1:1"));
    CHECK(pn.promote_candidate("10.0.1.1:1"));
    CHECK(pn.has_peer("10.0.1.1:1"));
    CHECK(pn.candidates().size() == 1);
    // ordering by score
    pn.mark_peer_good("10.0.1.1:1");
    pn.mark_peer_good("10.0.1.1:1");
    pn.mark_peer_good("10.0.1.1:1");
    auto top = pn.select_peers(1);
    CHECK(top.size() == 1 && top[0] == "10.0.1.1:1");
}

void test_dandelion() {
    using namespace addition;
    const std::vector<std::string> out = {"a", "b", "c", "d"};
    // local txs always stem, and use one relay for the whole epoch
    int diffusers = 0;
    for (std::uint64_t seed = 1; seed <= 400; ++seed) {
        DandelionRouter r({}, seed);
        r.set_now_for_test(10'000);
        auto d1 = r.route("", out);
        auto d2 = r.route("", out);
        CHECK(!d1.fluff && !d2.fluff);
        CHECK(d1.relay == d2.relay && !d1.relay.empty());
        CHECK(r.relays().size() <= 2);
        auto s1 = r.route("peerX", out);
        auto s2 = r.route("peerX", out);
        CHECK(s1.fluff == s2.fluff);
        if (!s1.fluff) CHECK(s1.relay == s2.relay);
        if (r.diffuser_this_epoch()) ++diffusers;
    }
    CHECK(diffusers > 10 && diffusers < 90);  // ~10% of 400
    // epoch rotation re-picks relays eventually
    DandelionRouter r({}, 7);
    r.set_now_for_test(0);
    std::set<std::string> seen;
    for (int e = 0; e < 30; ++e) {
        r.set_now_for_test(e * 600 + 1);
        seen.insert(r.route("", out).relay);
    }
    CHECK(seen.size() >= 2);
    // embargo
    r.set_now_for_test(100'000);
    r.embargo("tx1");
    r.embargo("tx2");
    CHECK(r.is_embargoed("tx1"));
    r.mark_fluffed("tx2");
    CHECK(!r.is_embargoed("tx2"));
    r.set_now_for_test(100'029);
    CHECK(r.expired_embargoes().empty());
    r.set_now_for_test(100'046);
    auto ex = r.expired_embargoes();
    CHECK(ex.size() == 1 && ex[0] == "tx1");
    CHECK(!r.is_embargoed("tx1"));
    // no outbound peers => must fluff (cannot stem)
    DandelionRouter r2({}, 3);
    CHECK(r2.route("", {}).fluff);
}

void test_cli() {
    using namespace addition;
    {
        NodeConfig cfg = default_node_config();
        const char* argv[] = {"additiond", "--onion-only", "--dandelion"};
        bool help = false;
        std::string err;
        CHECK(apply_cli_args(3, const_cast<char**>(argv), cfg, help, err));
        CHECK(cfg.onion_only);
        CHECK(cfg.dandelion);
        CHECK(cfg.tor_socks_proxy == "127.0.0.1:9050");
        CHECK(cfg.p2p_bind == "127.0.0.1");
    }
    {
        NodeConfig cfg = default_node_config();
        const std::string peer = "--bootstrap=" + kOnion + ":19444";
        const char* argv[] = {"additiond", "--tor-proxy=127.0.0.1:9150", peer.c_str()};
        bool help = false;
        std::string err;
        const bool ok = apply_cli_args(3, const_cast<char**>(argv), cfg, help, err);
        if (!ok) std::cerr << "cli err: " << err << "\n";
        CHECK(ok);
        CHECK(cfg.tor_socks_proxy == "127.0.0.1:9150");
        CHECK(!cfg.onion_only);
    }
}

void test_wire_budget() {
    using namespace addition;
    Transaction tx{};
    tx.signer = std::string(64, 'x');
    tx.signer_pubkey = std::string(5184, 'a');          // ML-DSA-87 pk hex
    tx.signature = "pq=" + std::string(9254, 'b');      // ML-DSA-87 sig hex
    tx.inputs.push_back({std::string(128, 'c'), 0});
    tx.outputs.push_back({std::string(64, 'd'), 1});
    tx.outputs.push_back({std::string(64, 'e'), 1});
    const auto w = tx_wire_bytes(tx);
    CHECK(w > 29000 && w < 31000);
    const auto per_block = (kMaxRelayBlockWireBytes - kBlockWireOverheadBytes - 2048) / w;
    std::cout << "ml-dsa-87 tx wire bytes=" << w << " max relayable txs/block=" << per_block << "\n";
    CHECK(per_block >= 7 && per_block <= 9);
}
} // namespace

int main() {
    test_onion();
    test_route();
    test_socks5();
    test_peers();
    test_dandelion();
    test_cli();
    test_wire_budget();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "test_net_tor: all passed\n";
    return 0;
}
