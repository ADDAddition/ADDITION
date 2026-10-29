#include "addition/dandelion.hpp"

#include <algorithm>
#include <chrono>

namespace addition {

DandelionRouter::DandelionRouter() : DandelionRouter(Params{}, std::random_device{}()) {}

DandelionRouter::DandelionRouter(Params p, std::uint64_t seed) : p_(p), rng_(seed) {
    if (p_.epoch_seconds <= 0) p_.epoch_seconds = 600;
}

void DandelionRouter::set_now_for_test(std::int64_t now_sec) { now_override_ = now_sec; }

std::int64_t DandelionRouter::now() const {
    if (now_override_ != 0) return now_override_;
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void DandelionRouter::maybe_new_epoch(const std::vector<std::string>& outbound) {
    const auto e = static_cast<std::uint64_t>(now() / p_.epoch_seconds);
    // Also re-pick if a chosen relay disappeared from the outbound set.
    bool stale = false;
    for (const auto& r : relays_) {
        if (std::find(outbound.begin(), outbound.end(), r) == outbound.end()) stale = true;
    }
    if (e == epoch_ && !stale && !(relays_.empty() && !outbound.empty())) return;
    if (e != epoch_) {
        std::bernoulli_distribution coin(p_.fluff_probability);
        diffuser_ = coin(rng_);
        epoch_ = e;
    }
    relays_.clear();
    source_to_relay_.clear();
    std::vector<std::string> pool = outbound;
    std::sort(pool.begin(), pool.end());
    std::shuffle(pool.begin(), pool.end(), rng_);
    for (std::size_t i = 0; i < pool.size() && i < 2; ++i) relays_.push_back(pool[i]);
}

DandelionRouter::Decision DandelionRouter::route(const std::string& source,
                                                 const std::vector<std::string>& outbound) {
    maybe_new_epoch(outbound);
    Decision d;
    if (!source.empty() && diffuser_) {
        d.fluff = true;
        return d;
    }
    // candidate relays excluding the source (never stem back)
    std::vector<std::string> c;
    for (const auto& r : relays_) if (r != source) c.push_back(r);
    if (c.empty()) {
        d.fluff = true;  // no stem path available
        return d;
    }
    const std::string key = source.empty() ? std::string("\x01local") : source;
    auto it = source_to_relay_.find(key);
    if (it == source_to_relay_.end() ||
        std::find(c.begin(), c.end(), it->second) == c.end()) {
        std::uniform_int_distribution<std::size_t> pick(0, c.size() - 1);
        it = source_to_relay_.insert_or_assign(key, c[pick(rng_)]).first;
    }
    d.relay = it->second;
    return d;
}

void DandelionRouter::embargo(const std::string& txid) {
    if (embargo_until_.count(txid)) return;
    std::uniform_int_distribution<std::int64_t> j(0, std::max<std::int64_t>(0, p_.embargo_jitter_seconds));
    embargo_until_[txid] = now() + p_.embargo_base_seconds + j(rng_);
}

void DandelionRouter::mark_fluffed(const std::string& txid) { embargo_until_.erase(txid); }

bool DandelionRouter::is_embargoed(const std::string& txid) const { return embargo_until_.count(txid) != 0; }

std::vector<std::string> DandelionRouter::expired_embargoes() {
    std::vector<std::string> out;
    const auto t = now();
    for (auto it = embargo_until_.begin(); it != embargo_until_.end();) {
        if (it->second <= t) {
            out.push_back(it->first);
            it = embargo_until_.erase(it);
        } else {
            ++it;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace addition
