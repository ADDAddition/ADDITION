#include "addition/p2p.hpp"

#include "addition/config.hpp"

#include <algorithm>
#include <chrono>

namespace addition {

std::int64_t PeerNetwork::now_locked() const {
    if (clock_override_ != 0) {
        return clock_override_;
    }
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count());
}

bool PeerNetwork::banned_locked(const std::string& endpoint) const {
    const auto it = banned_.find(endpoint);
    return it != banned_.end() && it->second > now_locked();
}

void PeerNetwork::set_limits(std::size_t max_peers, std::size_t max_candidates, std::int64_t ban_seconds) {
    std::lock_guard<std::mutex> lk(mu_);
    max_peers_ = max_peers > 0 ? max_peers : 1;
    max_candidates_ = max_candidates;
    ban_seconds_ = ban_seconds > 0 ? ban_seconds : 1;
}

void PeerNetwork::set_clock_for_test(std::int64_t now_sec) {
    std::lock_guard<std::mutex> lk(mu_);
    clock_override_ = now_sec;
}

void PeerNetwork::evict_if_full_locked() {
    while (peers_.size() >= max_peers_) {
        std::string victim;
        int worst = kMaxScore + 1;
        for (const auto& p : peers_) {
            if (seeds_.count(p) != 0) {
                continue;
            }
            const auto it = score_.find(p);
            const int sc = it == score_.end() ? 0 : it->second;
            if (sc < worst || (sc == worst && p < victim)) {
                worst = sc;
                victim = p;
            }
        }
        if (victim.empty()) {
            return;  // only seeds left
        }
        peers_.erase(victim);
        score_.erase(victim);
    }
}

bool PeerNetwork::add_peer(const std::string& endpoint) {
    std::lock_guard<std::mutex> lk(mu_);
    if (endpoint.empty()) {
        return false;
    }
    if (banned_locked(endpoint)) {
        return false;
    }
    banned_.erase(endpoint);
    if (peers_.count(endpoint) != 0) {
        return false;
    }
    evict_if_full_locked();
    if (peers_.size() >= max_peers_) {
        return false;
    }
    score_.try_emplace(endpoint, 0);
    candidates_.erase(endpoint);
    return peers_.insert(endpoint).second;
}

bool PeerNetwork::add_seed(const std::string& endpoint) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (endpoint.empty()) {
            return false;
        }
        seeds_.insert(endpoint);
        banned_.erase(endpoint);
        if (peers_.count(endpoint) != 0) {
            return false;
        }
        score_.try_emplace(endpoint, 0);
        return peers_.insert(endpoint).second;
    }
}

bool PeerNetwork::is_seed(const std::string& endpoint) const {
    std::lock_guard<std::mutex> lk(mu_);
    return seeds_.count(endpoint) != 0;
}

bool PeerNetwork::add_candidate(const std::string& endpoint) {
    std::lock_guard<std::mutex> lk(mu_);
    if (endpoint.empty() || peers_.count(endpoint) != 0 || banned_locked(endpoint)) {
        return false;
    }
    if (candidates_.size() >= max_candidates_) {
        return false;
    }
    return candidates_.insert(endpoint).second;
}

bool PeerNetwork::promote_candidate(const std::string& endpoint) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (candidates_.erase(endpoint) == 0) {
            return false;
        }
    }
    return add_peer(endpoint);
}

bool PeerNetwork::remove_candidate(const std::string& endpoint) {
    std::lock_guard<std::mutex> lk(mu_);
    return candidates_.erase(endpoint) > 0;
}

std::vector<std::string> PeerNetwork::candidates() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out(candidates_.begin(), candidates_.end());
    std::sort(out.begin(), out.end());
    return out;
}

int PeerNetwork::score(const std::string& endpoint) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = score_.find(endpoint);
    return it == score_.end() ? 0 : it->second;
}

bool PeerNetwork::in_backoff(const std::string& endpoint) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = backoff_.find(endpoint);
    return it != backoff_.end() && it->second > now_locked();
}

std::vector<std::string> PeerNetwork::select_peers(std::size_t n) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto now = now_locked();
    std::vector<std::pair<int, std::string>> v;
    for (const auto& p : peers_) {
        const auto b = backoff_.find(p);
        if (b != backoff_.end() && b->second > now) {
            continue;
        }
        const auto it = score_.find(p);
        v.emplace_back(it == score_.end() ? 0 : it->second, p);
    }
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::vector<std::string> out;
    for (const auto& e : v) {
        if (n != 0 && out.size() >= n) {
            break;
        }
        out.push_back(e.second);
    }
    return out;
}

bool PeerNetwork::remove_peer(const std::string& endpoint) {
    std::lock_guard<std::mutex> lk(mu_);
    return peers_.erase(endpoint) > 0;
}

bool PeerNetwork::has_peer(const std::string& endpoint) const {
    std::lock_guard<std::mutex> lk(mu_);
    return peers_.find(endpoint) != peers_.end();
}

void PeerNetwork::mark_peer_good(const std::string& endpoint) {
    std::lock_guard<std::mutex> lk(mu_);
    if (peers_.find(endpoint) == peers_.end()) {
        return;
    }
    auto& sc = score_[endpoint];
    sc = std::min(kMaxScore, sc + 1);
    backoff_.erase(endpoint);
}

void PeerNetwork::mark_peer_bad(const std::string& endpoint) {
    std::lock_guard<std::mutex> lk(mu_);
    if (peers_.find(endpoint) == peers_.end()) {
        return;
    }
    auto& sc = score_[endpoint];
    sc = std::max(kMinScore, sc - 1);
    if (sc > kBanScore) {
        return;
    }
    if (seeds_.count(endpoint) != 0) {
        // Seeds are often just temporarily unreachable: back off, never drop.
        backoff_[endpoint] = now_locked() + 60;
        sc = kBanScore + 2;
        return;
    }
    banned_[endpoint] = now_locked() + ban_seconds_;
    peers_.erase(endpoint);
    score_.erase(endpoint);
}

bool PeerNetwork::is_banned(const std::string& endpoint) const {
    std::lock_guard<std::mutex> lk(mu_);
    return banned_locked(endpoint);
}

std::vector<std::string> PeerNetwork::peers() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    out.reserve(peers_.size());
    for (const auto& p : peers_) {
        out.push_back(p);
    }
    return out;
}

std::size_t PeerNetwork::peer_count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return peers_.size();
}

std::vector<std::string> PeerNetwork::advertised_peers() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    for (const auto& p : peers_) {
        if (is_external_advertised_peer(p)) {
            out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> PeerNetwork::loopback_peers() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    for (const auto& p : peers_) {
        if (is_loopback_endpoint(p)) {
            out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::size_t PeerNetwork::advertised_peer_count() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::size_t n = 0;
    for (const auto& p : peers_) {
        if (is_external_advertised_peer(p)) {
            ++n;
        }
    }
    return n;
}

std::size_t PeerNetwork::loopback_peer_count() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::size_t n = 0;
    for (const auto& p : peers_) {
        if (is_loopback_endpoint(p)) {
            ++n;
        }
    }
    return n;
}

} // namespace addition
