#pragma once

#include <cstdint>
#include <unordered_map>
#include <string>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace addition {

// Address manager + peer scoring.
//  * scores clamp to [kMinScore, kMaxScore]; good +1, bad -1
//  * reaching kBanScore bans a normal peer for ban_seconds (not forever),
//    while a seed (bootstrap) peer is only put on backoff, never dropped
//  * the table is capped (max_peers); on overflow the lowest-scored
//    non-seed peer is evicted
//  * unverified candidates (ADDR/ADVERTISE) live in a separate capped set
//    and only become peers after promote_candidate() (successful handshake)
class PeerNetwork {
public:
    static constexpr int kMaxScore = 100;
    static constexpr int kMinScore = -10;
    static constexpr int kBanScore = -5;

    void set_limits(std::size_t max_peers, std::size_t max_candidates, std::int64_t ban_seconds);
    void set_clock_for_test(std::int64_t now_sec);  // 0 = use real clock
    bool add_seed(const std::string& endpoint);
    bool is_seed(const std::string& endpoint) const;
    bool add_candidate(const std::string& endpoint);
    bool promote_candidate(const std::string& endpoint);
    bool remove_candidate(const std::string& endpoint);
    std::vector<std::string> candidates() const;
    int score(const std::string& endpoint) const;
    bool in_backoff(const std::string& endpoint) const;
    // Peers ordered by score (desc), skipping those in backoff; n=0 => all.
    std::vector<std::string> select_peers(std::size_t n) const;

    bool add_peer(const std::string& endpoint);
    bool remove_peer(const std::string& endpoint);
    bool has_peer(const std::string& endpoint) const;
    void mark_peer_good(const std::string& endpoint);
    void mark_peer_bad(const std::string& endpoint);
    bool is_banned(const std::string& endpoint) const;
    std::vector<std::string> peers() const;
    std::size_t peer_count() const;
    std::vector<std::string> advertised_peers() const;
    std::vector<std::string> loopback_peers() const;
    std::size_t advertised_peer_count() const;
    std::size_t loopback_peer_count() const;

private:
    mutable std::mutex mu_;
    std::unordered_set<std::string> peers_;
    std::unordered_map<std::string, int> score_;
    std::unordered_map<std::string, std::int64_t> banned_;   // endpoint -> ban expiry (unix sec)
    std::unordered_map<std::string, std::int64_t> backoff_;  // seed -> retry-after
    std::unordered_set<std::string> seeds_;
    std::unordered_set<std::string> candidates_;
    std::size_t max_peers_ = 125;
    std::size_t max_candidates_ = 1000;
    std::int64_t ban_seconds_ = 3600;
    std::int64_t clock_override_ = 0;

    std::int64_t now_locked() const;
    bool banned_locked(const std::string& endpoint) const;
    void evict_if_full_locked();
};

} // namespace addition
