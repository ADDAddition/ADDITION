#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace addition {

class PrivateMessagingEngine {
public:
    struct Envelope {
        std::string msg_id;
        std::string sender;
        std::string recipient;
        std::string ciphertext_ref;
        std::uint64_t anchor_height{0};
        std::string anchor_block_hash;
        std::string anchor_hash;
        std::uint64_t created_at{0};
        std::uint64_t ttl_sec{0};
        std::uint64_t expire_at{0};
        std::string policy{"default"};
    };

    struct DestroyedRecord {
        std::uint64_t destroyed_at{0};
        std::string reason;
        std::string anchor_hash;
    };

    bool send_ttl(const std::string& sender,
                  const std::string& recipient,
                  const std::string& ciphertext_ref,
                  std::uint64_t ttl_sec,
                  std::uint64_t anchor_height,
                  const std::string& anchor_block_hash,
                  const std::string& policy,
                  std::string& msg_id,
                  std::string& error);

    bool inbox(const std::string& recipient, std::string& out, std::string& error);
    bool status(const std::string& msg_id, std::string& out, std::string& error);
    bool fetch(const std::string& msg_id, const std::string& requester, std::string& out, std::string& error);
    bool destroy(const std::string& msg_id, const std::string& requester, std::string& error);
    std::uint64_t purge_expired();

    std::string dump_state() const;
    bool load_state(const std::string& state, std::string& error);

private:
    std::unordered_map<std::string, Envelope> envelopes_;
    std::unordered_map<std::string, DestroyedRecord> destroyed_;
};

} // namespace addition
