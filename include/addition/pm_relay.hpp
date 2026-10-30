#pragma once

// End-to-end encrypted private message relay (store-and-forward over P2P).
//
// Envelope (wire "PMSG|v1|<msg_id>|<mailbox>|<expire_at>|<del_hash>|<kem_ct>|<iv>|<ct>"):
//   * ML-KEM-1024 encapsulation to the recipient's message key; AES-256-GCM
//     with a key derived from the shared secret (SHA3-512, domain separated).
//   * mailbox = first 128 bits of SHA3-512(recipient KEM public key). No sender
//     identity, wallet address or plaintext length beyond padding is on the wire.
//   * del_hash = SHA3-512(delete token); the token is derived from the shared
//     secret, so only sender/recipient can issue "PMDEL|<msg_id>|<token>".
//   * expire_at bounds storage: relays drop the envelope after it (max 7 days).
// Relays keep envelopes in memory only, bounded by count/bytes, with a
// per-peer accept rate limit. Nothing here logs plaintext or metadata.

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace addition {

class PmRelay {
public:
    static constexpr std::size_t kMaxPlaintext = 4096;
    static constexpr std::uint64_t kMinTtl = 60;
    static constexpr std::uint64_t kMaxTtl = 7ULL * 24 * 3600;
    static constexpr std::size_t kMaxMessages = 4096;
    static constexpr std::size_t kMaxStoreBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t kPeerRatePerMin = 60;
    static constexpr std::size_t kPushPerPeerRound = 32;

    explicit PmRelay(std::string key_dir = {});
    void set_key_dir(std::string dir);

    // Message key for a local wallet name (created on first use, 0600 file).
    bool ensure_key(const std::string& wallet, std::string& pubkey_hex, std::string& mailbox,
                    std::string& error);

    // Encrypt to recipient_pk_hex and queue for relay. Returns msg_id.
    bool send(const std::string& recipient_pk_hex, const std::string& plaintext,
              std::uint64_t ttl_sec, std::string& msg_id, std::string& error);

    // Inbound wire body (after "PMSG|"). duplicate=true when already known.
    bool accept_wire(const std::string& peer, const std::string& body, bool& duplicate,
                     std::string& error);
    // Inbound "PMDEL|" body: "<msg_id>|<token_hex>".
    bool accept_delete(const std::string& peer, const std::string& body, bool& known,
                       std::string& error);

    // Local wallet: list / read / delete (delete is propagated network-wide).
    bool inbox(const std::string& wallet, std::string& out, std::string& error);
    bool read(const std::string& wallet, const std::string& msg_id, std::string& out,
              std::string& error);
    bool remove(const std::string& wallet, const std::string& msg_id, std::string& error);

    // Store-and-forward: wire lines this peer has not been given yet.
    std::vector<std::string> undelivered_for(const std::string& peer, std::size_t max);
    void mark_delivered(const std::string& peer, const std::string& line);
    bool has_undelivered(const std::string& peer);

    std::size_t purge_expired();
    std::string status();

private:
    struct Env {
        std::string id, mailbox, del_hash, kem_ct, iv, ct;
        std::uint64_t expire_at = 0;
        std::string wire() const;
    };
    struct Keys { std::vector<std::uint8_t> pk, sk; std::string mailbox; };

    bool load_key_locked(const std::string& wallet, Keys& k, bool create, std::string& error);
    bool decrypt_locked(const Env& e, const Keys& k, std::string& plain, std::string& token_hex,
                        std::string& error) const;
    bool store_locked(Env e, bool& duplicate, std::string& error);
    bool rate_ok_locked(const std::string& peer);
    void purge_locked(std::uint64_t now);

    std::mutex mu_;
    std::string key_dir_;
    std::map<std::string, Env> envs_;                         // msg_id -> envelope
    std::unordered_map<std::string, std::uint64_t> tombstones_;  // msg_id -> expire_at
    std::unordered_map<std::string, std::string> del_lines_;    // msg_id -> "PMDEL|..." to relay
    std::unordered_map<std::string, std::unordered_set<std::string>> delivered_;  // peer -> ids
    std::unordered_map<std::string, std::deque<std::uint64_t>> rate_;
    std::size_t bytes_ = 0;
};

} // namespace addition
