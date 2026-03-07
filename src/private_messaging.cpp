#include "addition/private_messaging.hpp"

#include "addition/crypto.hpp"

#include <chrono>
#include <sstream>

namespace addition {
namespace {

std::uint64_t now_seconds_pm() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

} // namespace

bool PrivateMessagingEngine::send_ttl(const std::string& sender,
                                      const std::string& recipient,
                                      const std::string& ciphertext_ref,
                                      std::uint64_t ttl_sec,
                                      std::uint64_t anchor_height,
                                      const std::string& anchor_block_hash,
                                      const std::string& policy,
                                      std::string& msg_id,
                                      std::string& error) {
    msg_id.clear();
    if (sender.empty() || recipient.empty() || ciphertext_ref.empty()) {
        error = "invalid message params";
        return false;
    }
    if (ttl_sec == 0 || ttl_sec > 31536000ULL) {
        error = "ttl out of range";
        return false;
    }
    if (anchor_height == 0 || anchor_block_hash.empty()) {
        error = "missing chain anchor";
        return false;
    }

    const auto created = now_seconds_pm();
    const auto seed = sender + "|" + recipient + "|" + ciphertext_ref + "|" +
                      std::to_string(anchor_height) + "|" + anchor_block_hash + "|" +
                      std::to_string(created) + "|" + std::to_string(envelopes_.size());
    msg_id = to_hex(sha3_512_bytes("msg|" + seed));

    Envelope e{};
    e.msg_id = msg_id;
    e.sender = sender;
    e.recipient = recipient;
    e.ciphertext_ref = ciphertext_ref;
    e.anchor_height = anchor_height;
    e.anchor_block_hash = anchor_block_hash;
    e.anchor_hash = to_hex(sha3_512_bytes("msg_anchor|" + msg_id + "|" + std::to_string(anchor_height) + "|" + anchor_block_hash));
    e.created_at = created;
    e.ttl_sec = ttl_sec;
    e.expire_at = created + ttl_sec;
    e.policy = policy.empty() ? "default" : policy;

    envelopes_[msg_id] = std::move(e);
    return true;
}

bool PrivateMessagingEngine::inbox(const std::string& recipient, std::string& out, std::string& error) {
    out.clear();
    if (recipient.empty()) {
        error = "recipient empty";
        return false;
    }
    purge_expired();

    std::ostringstream oss;
    bool first = true;
    for (const auto& [id, e] : envelopes_) {
        if (e.recipient != recipient) continue;
        if (!first) oss << ',';
        first = false;
        oss << id << ':' << e.sender << ':' << e.expire_at << ':' << e.policy
            << ":anchor_h=" << e.anchor_height;
    }
    out = oss.str();
    return true;
}

bool PrivateMessagingEngine::status(const std::string& msg_id, std::string& out, std::string& error) {
    out.clear();
    purge_expired();

    auto it = envelopes_.find(msg_id);
    if (it != envelopes_.end()) {
        const auto& e = it->second;
        std::ostringstream oss;
        oss << "msg_id=" << e.msg_id
            << " sender=" << e.sender
            << " recipient=" << e.recipient
            << " created_at=" << e.created_at
            << " expire_at=" << e.expire_at
            << " policy=" << e.policy
            << " status=active"
            << " anchor_height=" << e.anchor_height
            << " anchor_block_hash=" << e.anchor_block_hash
            << " anchor_hash=" << e.anchor_hash;
        out = oss.str();
        return true;
    }

    auto dit = destroyed_.find(msg_id);
    if (dit != destroyed_.end()) {
        std::ostringstream oss;
        oss << "msg_id=" << msg_id
            << " status=destroyed"
            << " destroyed_at=" << dit->second.destroyed_at
            << " reason=" << dit->second.reason
            << " anchor_hash=" << dit->second.anchor_hash;
        out = oss.str();
        return true;
    }

    error = "message not found";
    return false;
}

bool PrivateMessagingEngine::fetch(const std::string& msg_id,
                                   const std::string& requester,
                                   std::string& out,
                                   std::string& error) {
    out.clear();
    purge_expired();

    auto it = envelopes_.find(msg_id);
    if (it == envelopes_.end()) {
        error = "message not found";
        return false;
    }
    if (requester.empty() || (requester != it->second.sender && requester != it->second.recipient)) {
        error = "not allowed";
        return false;
    }

    const auto& e = it->second;
    std::ostringstream oss;
    oss << "msg_id=" << e.msg_id
        << " sender=" << e.sender
        << " recipient=" << e.recipient
        << " ciphertext_ref=" << e.ciphertext_ref
        << " created_at=" << e.created_at
        << " expire_at=" << e.expire_at
        << " policy=" << e.policy
        << " anchor_height=" << e.anchor_height
        << " anchor_block_hash=" << e.anchor_block_hash
        << " anchor_hash=" << e.anchor_hash;
    out = oss.str();
    return true;
}

bool PrivateMessagingEngine::destroy(const std::string& msg_id, const std::string& requester, std::string& error) {
    auto it = envelopes_.find(msg_id);
    if (it == envelopes_.end()) {
        error = "message not found";
        return false;
    }
    if (requester.empty() || (requester != it->second.sender && requester != it->second.recipient)) {
        error = "not allowed";
        return false;
    }
    DestroyedRecord rec{};
    rec.destroyed_at = now_seconds_pm();
    rec.reason = "manual";
    rec.anchor_hash = to_hex(sha3_512_bytes("destroy|" + msg_id + "|" + requester + "|" + std::to_string(rec.destroyed_at)));
    destroyed_[msg_id] = std::move(rec);
    envelopes_.erase(it);
    return true;
}

std::uint64_t PrivateMessagingEngine::purge_expired() {
    const auto now = now_seconds_pm();
    std::uint64_t changed = 0;
    std::vector<std::string> to_erase;
    to_erase.reserve(envelopes_.size());
    for (const auto& [id, e] : envelopes_) {
        if (e.expire_at <= now) {
            to_erase.push_back(id);
        }
    }

    for (const auto& id : to_erase) {
        DestroyedRecord rec{};
        rec.destroyed_at = now;
        rec.reason = "ttl_expired";
        rec.anchor_hash = to_hex(sha3_512_bytes("destroy|" + id + "|ttl|" + std::to_string(now)));
        destroyed_[id] = std::move(rec);
        envelopes_.erase(id);
        ++changed;
    }
    return changed;
}

std::string PrivateMessagingEngine::dump_state() const {
    std::ostringstream oss;
    for (const auto& [id, e] : envelopes_) {
        (void)id;
        oss << "M|" << e.msg_id << '|' << e.sender << '|' << e.recipient << '|' << e.ciphertext_ref << '|'
            << e.anchor_height << '|' << e.anchor_block_hash << '|' << e.anchor_hash << '|'
            << e.created_at << '|' << e.ttl_sec << '|' << e.expire_at << '|' << e.policy << '\n';
    }
    for (const auto& [id, d] : destroyed_) {
        oss << "D|" << id << '|' << d.destroyed_at << '|' << d.reason << '|' << d.anchor_hash << '\n';
    }
    return oss.str();
}

bool PrivateMessagingEngine::load_state(const std::string& state, std::string& error) {
    envelopes_.clear();
    destroyed_.clear();

    std::istringstream iss(state);
    for (std::string line; std::getline(iss, line);) {
        if (line.empty() || line.size() < 2 || line[1] != '|') continue;
        if (line[0] == 'M') {
            std::istringstream ls(line.substr(2));
            Envelope e{};
            std::string anchor_height;
            std::string created_at, ttl_sec, expire_at;
            std::getline(ls, e.msg_id, '|');
            std::getline(ls, e.sender, '|');
            std::getline(ls, e.recipient, '|');
            std::getline(ls, e.ciphertext_ref, '|');
            std::getline(ls, anchor_height, '|');
            std::getline(ls, e.anchor_block_hash, '|');
            std::getline(ls, e.anchor_hash, '|');
            std::getline(ls, created_at, '|');
            std::getline(ls, ttl_sec, '|');
            std::getline(ls, expire_at, '|');
            std::getline(ls, e.policy);

            if (e.msg_id.empty()) {
                error = "invalid message state line";
                return false;
            }
            e.anchor_height = static_cast<std::uint64_t>(std::stoull(anchor_height));
            e.created_at = static_cast<std::uint64_t>(std::stoull(created_at));
            e.ttl_sec = static_cast<std::uint64_t>(std::stoull(ttl_sec));
            e.expire_at = static_cast<std::uint64_t>(std::stoull(expire_at));
            envelopes_[e.msg_id] = std::move(e);
        } else if (line[0] == 'D') {
            std::istringstream ls(line.substr(2));
            std::string msg_id;
            DestroyedRecord d{};
            std::string destroyed_at;
            std::getline(ls, msg_id, '|');
            std::getline(ls, destroyed_at, '|');
            std::getline(ls, d.reason, '|');
            std::getline(ls, d.anchor_hash);
            if (msg_id.empty()) {
                error = "invalid destroy state line";
                return false;
            }
            d.destroyed_at = static_cast<std::uint64_t>(std::stoull(destroyed_at));
            destroyed_[msg_id] = std::move(d);
        }
    }

    purge_expired();

    return true;
}

} // namespace addition
