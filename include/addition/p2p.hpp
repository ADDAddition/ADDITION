#pragma once

#include <cstdint>
#include <unordered_map>
#include <string>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace addition {

class PeerNetwork {
public:
    bool add_peer(const std::string& endpoint);
    bool remove_peer(const std::string& endpoint);
    bool has_peer(const std::string& endpoint) const;
    void mark_peer_good(const std::string& endpoint);
    void mark_peer_bad(const std::string& endpoint);
    bool is_banned(const std::string& endpoint) const;
    std::vector<std::string> peers() const;
    std::size_t peer_count() const;

private:
    mutable std::mutex mu_;
    std::unordered_set<std::string> peers_;
    std::unordered_map<std::string, int> score_;
    std::unordered_set<std::string> banned_;
};

} // namespace addition
