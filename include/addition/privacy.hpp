#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace addition {

struct PrivateNote {
    std::string owner_tag;
    std::string amount_sealed;
    std::string blinding;
    std::string commitment;
    std::string nullifier;
    bool spent{false};
};

struct OpeningNote {
    std::string trapdoor;
    std::string commitment;
    std::string nullifier;
};

// Pluggable note-metadata cipher. Replaces the old node-wide
// ADDITION_PRIVACY_MASTER_KEY (a single server-held secret that could link every
// owner and unseal every amount). Nothing here is a node secret.
//
// A real shielded-transaction backend (teammate work) should implement this
// with client-held keys: owner_tag = recipient-derived diversified tag,
// seal/unseal = AEAD to the recipient's viewing key, done CLIENT side; the node
// then only stores opaque bytes. All methods must be deterministic given inputs.
class NoteCipher {
public:
    virtual ~NoteCipher() = default;
    virtual std::string name() const = 0;
    // Per-note owner tag. Must be salted by note_id so tags are not linkable
    // across notes by anyone without the owner identity.
    virtual bool owner_tag(const std::string& owner, const std::string& note_id,
                           std::string& out, std::string& error) const = 0;
    virtual bool seal_amount(std::uint64_t amount, const std::string& note_id, const std::string& blinding,
                             std::string& sealed_hex, std::string& error) const = 0;
    virtual bool unseal_amount(const std::string& sealed_hex, const std::string& note_id,
                               const std::string& blinding, std::uint64_t& out, std::string& error) const = 0;
};

// Default: keyless, per-note salted SHA3 derivation. Honest scope: this is
// obfuscation of node state, NOT confidentiality from a node that sees the
// RPC calls (owner/amount arrive in clear). There is no master key to leak.
std::shared_ptr<const NoteCipher> make_keyless_note_cipher();

// DEPRECATED: transparent "opening" notes. Amounts/owners are visible to the node;
// this is NOT a privacy feature. Use shielded (addz) transactions (shielded.hpp).
class PrivacyPool {
public:
    PrivacyPool();
    // Swap the cipher (e.g. a client-key shielded backend). Must be set before
    // any notes are minted/loaded; notes are bound to the cipher that made them.
    void set_note_cipher(std::shared_ptr<const NoteCipher> cipher);
    std::string note_cipher_name() const;

    bool set_native_verifier_mode(const std::string& mode, std::string& error);
    std::string native_verifier_mode() const;
    bool strict_zk_mode() const;

    static void compute_opening_relation(std::uint64_t amount,
                                         const std::string& trapdoor,
                                         std::string& commitment,
                                         std::string& nullifier);
    static bool verify_opening(std::uint64_t amount,
                               const std::string& trapdoor,
                               const std::string& commitment,
                               const std::string& nullifier,
                               std::string& error);
    static bool prepare_opening(std::uint64_t amount, OpeningNote& out, std::string& error);

    std::string mint_open(const std::string& owner,
                          std::uint64_t amount,
                          const std::string& commitment,
                          const std::string& nullifier,
                          const std::string& trapdoor,
                          std::string& error);
    bool spend_open(const std::string& owner,
                    const std::string& note_id,
                    const std::string& recipient,
                    std::uint64_t amount,
                    const std::string& trapdoor,
                    std::string& new_note_id,
                    OpeningNote& recipient_opening,
                    std::string& change_note_id,
                    OpeningNote& change_opening,
                    std::string& error);

    std::string mint_zk(const std::string& owner,
                        std::uint64_t amount,
                        const std::string& commitment,
                        const std::string& nullifier,
                        const std::string& proof_hex,
                        const std::string& verification_key_hex,
                        std::string& error);
    bool spend_zk(const std::string& owner,
                  const std::string& note_id,
                  const std::string& recipient,
                  std::uint64_t amount,
                  const std::string& nullifier,
                  const std::string& proof_hex,
                  const std::string& verification_key_hex,
                  std::string& new_note_id,
                  std::string& error);

    bool verifier_configured() const;
    // Always false: no node-held privacy master key exists anymore. Kept so
    // callers/RPC output stay source compatible.
    static bool master_key_configured();
    std::size_t note_count() const;
    std::size_t used_nullifier_count() const;
    std::size_t spent_commitment_count() const;

    std::string dump_state() const;
    bool load_state(const std::string& state, std::string& error);

private:
    enum class NativeVerifierMode {
        pq_mldsa87,
    };

    bool verify_native_proof(const std::string& public_input,
                             const std::string& proof_hex,
                             const std::string& verification_key_hex,
                             std::string& error) const;
    bool verify_proof(const std::string& public_input,
                      const std::string& proof_hex,
                      const std::string& verification_key_hex,
                      std::string& error) const;
    std::string mint_note_internal(const std::string& owner,
                                   std::uint64_t amount,
                                   std::string& error);

    std::shared_ptr<const NoteCipher> cipher_;
    NativeVerifierMode native_verifier_mode_{NativeVerifierMode::pq_mldsa87};
    bool strict_zk_mode_{true};
    std::unordered_map<std::string, PrivateNote> notes_;
    std::unordered_set<std::string> used_nullifiers_;
    std::unordered_set<std::string> spent_commitments_;
};

} // namespace addition
