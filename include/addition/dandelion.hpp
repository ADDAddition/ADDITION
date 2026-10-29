#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace addition {

// Dandelion++ (Fanti et al. 2018) routing state, transport-agnostic.
//  * Time is split into epochs. At each epoch start the node flips a coin:
//    with probability fluff_probability it is a "diffuser" for the epoch
//    (every relayed stem tx is fluffed), otherwise a "relayer".
//  * Per epoch it picks up to 2 outbound stem relays; each inbound source is
//    mapped to one relay (pseudo-random per-source mapping), and local txs
//    use a fixed relay for the whole epoch. This defeats intersection attacks
//    that a per-tx random relay would allow.
//  * Local transactions are ALWAYS stemmed (never fluffed by the originator).
//  * Every stemmed tx gets an embargo timer; if the node has not seen the tx
//    fluffed (TX| gossip) before it expires, it fluffs it itself (black-hole
//    protection).
class DandelionRouter {
public:
    struct Params {
        std::int64_t epoch_seconds = 600;
        double fluff_probability = 0.10;
        std::int64_t embargo_base_seconds = 30;
        std::int64_t embargo_jitter_seconds = 15;
    };

    DandelionRouter();
    explicit DandelionRouter(Params p, std::uint64_t seed);

    void set_now_for_test(std::int64_t now_sec);
    std::int64_t now() const;

    struct Decision {
        bool fluff = false;
        std::string relay;  // set when !fluff
    };
    // source == "" for a locally created tx. outbound = candidate relay peers.
    Decision route(const std::string& source, const std::vector<std::string>& outbound);

    void embargo(const std::string& txid);
    void mark_fluffed(const std::string& txid);  // seen as TX| or fluffed by us
    bool is_embargoed(const std::string& txid) const;
    std::vector<std::string> expired_embargoes();  // removes them

    bool diffuser_this_epoch() const { return diffuser_; }
    std::uint64_t epoch() const { return epoch_; }
    const std::vector<std::string>& relays() const { return relays_; }

private:
    Params p_;
    std::mt19937_64 rng_;
    std::int64_t now_override_ = 0;
    std::uint64_t epoch_ = UINT64_MAX;
    bool diffuser_ = false;
    std::vector<std::string> relays_;
    std::unordered_map<std::string, std::string> source_to_relay_;
    std::unordered_map<std::string, std::int64_t> embargo_until_;

    void maybe_new_epoch(const std::vector<std::string>& outbound);
};

} // namespace addition
