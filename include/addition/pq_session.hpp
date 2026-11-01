#pragma once

// AMLS1: post-quantum 1:1 secure sessions on top of the PMSG relay.
//
//  * Identity      : ML-DSA-87 key pair per wallet (separate from the wallet
//                    spending key; never linked on-chain unless the user does it).
//  * Key package   : signed by the identity key (ML-DSA-87). Carries an ML-KEM-1024
//                    init key (handshake), the ML-KEM-1024 relay key (PMSG
//                    envelope), an optional discovery index, created/expires.
//  * Discovery     : contact string "addc1:<hex package>" (out of band) or an
//                    opt-in directory index = SHA3-512("ADDITION-KP-dir|v1|"+handle)
//                    replicated to every node (KPKG gossip), so lookups are local
//                    and never leave the querying node.
//  * Handshake     : PQXDH-like, KEM only. Initiator encapsulates to the init key,
//                    signs the transcript with ML-DSA-87 and sends its own key
//                    package inside the encrypted frame.
//  * Ratchet       : KEM double ratchet. Symmetric HMAC-SHA3-512 chains give
//                    per-message forward secrecy; each turn the sender
//                    encapsulates to the peer's latest ML-KEM-1024 ratchet key and
//                    rotates its own (post-compromise security). AES-256-GCM.
//  * Transport     : every frame goes inside the existing ML-KEM-1024 + AES-256-GCM
//                    PMSG envelope, so relays only see the envelope.
// See docs/PQ_MESSAGING.md for the threat model, tradeoffs and the group plan.

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace addition {
class PmRelay;

namespace pqmsg {

using Bytes = std::vector<std::uint8_t>;

inline constexpr std::uint64_t kMaxPackageLifetime = 30ULL * 24 * 3600;

std::string handle_index(const std::string& handle);  // 32 hex
std::string fingerprint(const Bytes& identity_pub);    // 40 hex

struct KeyPackage {
    std::string index = "-";  // discovery index or "-" (unlisted)
    Bytes identity_pub;       // ML-DSA-87
    Bytes init_pub;           // ML-KEM-1024
    Bytes relay_pub;          // ML-KEM-1024 (PMSG envelope key)
    std::uint64_t created = 0;
    std::uint64_t expires = 0;
    Bytes sig;                // ML-DSA-87 over tbs()

    std::string tbs() const;
    Bytes encode() const;
    std::string hex() const;
    std::string hash() const;  // 32 hex
    static bool decode(const Bytes& in, KeyPackage& out, std::string& error);
    static bool from_hex(const std::string& hex, KeyPackage& out, std::string& error);
    // Signature, sizes, lifetime and expiry. now = unix seconds.
    bool verify(std::uint64_t now, std::string& error) const;
};

// ML-DSA-87 helpers (raw liboqs).
bool sig_keypair(Bytes& pk, Bytes& sk);
bool sig_sign(const Bytes& sk, const std::string& msg, Bytes& sig);
bool sig_verify(const Bytes& pk, const std::string& msg, const Bytes& sig);

class Messenger {
public:
    Messenger(std::string dir, PmRelay* relay);

    // Create identity (if needed), a fresh init key and a signed key package.
    // handle "-" = unlisted (contact string only). Listed packages are put in
    // the local directory and gossiped.
    bool publish(const std::string& wallet, const std::string& handle, std::uint64_t days,
                 std::string& contact, std::string& fp, std::string& error);
    bool lookup(const std::string& handle_or_contact, KeyPackage& kp, std::string& error);
    // Start a session (INIT frame + first message). Returns session id.
    bool start(const std::string& wallet, const std::string& peer, const std::string& text,
               std::uint64_t ttl, std::string& sid, std::string& error);
    bool send(const std::string& wallet, const std::string& sid, const std::string& text,
              std::uint64_t ttl, std::string& error);
    // Decrypt all session frames for wallet in the relay inbox. Processed
    // envelopes are deleted (tombstoned); frames for unknown sessions stay.
    bool receive(const std::string& wallet, std::string& out, std::string& error);
    bool sessions(const std::string& wallet, std::string& out, std::string& error);

    // In-memory frame API (tests / no relay): build and consume frames.
    bool start_frame(const std::string& wallet, const KeyPackage& peer, const std::string& text,
                     std::string& sid, Bytes& frame, std::string& error);
    bool send_frame(const std::string& wallet, const std::string& sid, const std::string& text,
                    Bytes& frame, std::string& error);
    bool open_frame(const std::string& wallet, const Bytes& frame, std::string& sid,
                    std::string& from_fp, std::string& text, std::string& error);

private:
    struct Session;
    bool load_identity(const std::string& wallet, Bytes& pk, Bytes& sk, bool create, std::string& error);
    bool load_package(const std::string& wallet, KeyPackage& kp, std::string& error);
    bool find_init_sk(const std::string& wallet, const std::string& kp_hash, Bytes& sk, std::string& error);
    bool load_session(const std::string& wallet, const std::string& sid, Session& s, std::string& error);
    bool save_session(const std::string& wallet, const Session& s, std::string& error);
    bool encrypt_locked(Session& s, const std::string& text, Bytes& body, std::string& error);
    bool decrypt_locked(Session& s, const Bytes& hdr_rk_pub, const Bytes& step_ct, const std::string& to,
                        std::uint32_t pn, std::uint32_t n, const Bytes& iv, const Bytes& ct,
                        const std::string& aad, std::string& text, std::string& error);

    std::mutex mu_;
    std::string dir_;
    PmRelay* relay_;
};

} // namespace pqmsg
} // namespace addition
