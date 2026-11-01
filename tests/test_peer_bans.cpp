// Peer ban policy: transport failures never ban, misbehaviour bans are
// time-limited, and authenticated peers are re-admitted after the ban.
#include "addition/p2p.hpp"
#include <cstdlib>
#include <iostream>

using addition::PeerNetwork;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::cerr << "FAIL line " << __LINE__ << ": " #c "\n"; ++fails; } } while (0)

int main() {
    const std::string p = "edndmdzj5cwvr5hbzpn6mwmwd54zwcohn6exncyzo2woziqzxtur5vad.onion:29002";
    {   // flaky Tor: many transport failures, still accepted inbound
        PeerNetwork n; n.set_limits(10, 10, 600); n.set_clock_for_test(1000);
        CHECK(n.add_peer(p));
        for (int i = 0; i < 50; ++i) n.mark_peer_unreachable(p);
        CHECK(!n.is_banned(p));
        CHECK(n.has_peer(p));
        CHECK(n.in_backoff(p));
        n.readmit_authenticated(p);
        CHECK(!n.in_backoff(p));
        CHECK(n.score(p) >= 0);
    }
    {   // misbehaviour: time-limited ban, error-visible expiry, re-admission after expiry
        PeerNetwork n; n.set_limits(10, 10, 600); n.set_clock_for_test(1000);
        CHECK(n.add_peer(p));
        for (int i = 0; i < 10; ++i) n.mark_peer_bad(p);
        CHECK(n.is_banned(p));
        CHECK(n.ban_expiry(p) == 1600);
        CHECK(!n.add_peer(p));
        n.readmit_authenticated(p);          // HELLO cannot lift a misbehaviour ban
        CHECK(n.is_banned(p));
        n.set_clock_for_test(1601);
        CHECK(!n.is_banned(p));
        CHECK(n.ban_expiry(p) == 0);
        n.readmit_authenticated(p);
        CHECK(n.has_peer(p));
        CHECK(n.score(p) == 0);
    }
    if (fails) { std::cerr << fails << " failures\n"; return 1; }
    std::cout << "test_peer_bans OK\n";
    return 0;
}
