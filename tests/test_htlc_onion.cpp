// Onion route privacy: each hop peels only its own layer; packets are fixed
// size; tampering, wrong keys and wrong payment hash are rejected.
#include "addition/htlc_onion.hpp"

#include <cstdio>
#include <cstdlib>

using namespace addition::onion;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

int main() {
    const std::string ph(128, 'a');
    for (std::size_t n = 1; n <= kMaxHops; ++n) {
        std::vector<Bytes> pk(n), sk(n);
        std::vector<Hop> hops(n);
        for (std::size_t i = 0; i < n; ++i) {
            CHECK(keygen(pk[i], sk[i]));
            hops[i] = Hop{pk[i], i + 1 < n ? "c" + std::to_string(i + 2) : "", 1000 + 10 * (n - 1 - i), 500 - 6 * i};
        }
        Bytes pkt;
        std::string err;
        CHECK(build(hops, ph, pkt, err));
        CHECK(pkt.size() == kPacket);
        for (std::size_t i = 0; i < n; ++i) {
            Peeled p;
            if (i + 1 < n || n > 1) {  // a hop never opens another hop's layer
                Peeled q;
                CHECK(!peel(sk[(i + 1) % n], pkt, ph, q, err) || n == 1);
            }
            Bytes bad = pkt; bad[kKemCt + 5] ^= 1;
            CHECK(!peel(sk[i], bad, ph, p, err));
            CHECK(!peel(sk[i], pkt, std::string(128, 'b'), p, err));
            CHECK(peel(sk[i], pkt, ph, p, err));
            CHECK(p.final == (i + 1 == n));
            CHECK(p.amount == hops[i].amount && p.expiry == hops[i].expiry && p.next_chan == hops[i].next_chan);
            if (!p.final) { CHECK(p.next_packet.size() == kPacket); pkt = p.next_packet; }
        }
    }
    std::vector<Hop> five(5);
    Bytes pkt; std::string err;
    CHECK(!build(five, ph, pkt, err));
    std::puts("test_htlc_onion OK");
    return 0;
}
