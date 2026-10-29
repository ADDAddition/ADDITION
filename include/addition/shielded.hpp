#pragma once
// ADDITION shielded pool ("addz") — EXPERIMENTAL, NOT AUDITED. Do not enable on
// mainnet before an external cryptographic audit.
//
// Design (Zcash-Sapling-like note model, post-quantum primitives only):
//   * Notes:        cm = SHA256("ADD/cm/v1" || value || owner || rho || r)
//   * Nullifiers:   nf = SHA256("ADD/nf/v1" || nk || rho); owner = SHA256("ADD/owner/v1" || nk)
//   * Anonymity:    spends prove membership in a depth-32 SHA-256 Merkle tree of
//                   ALL commitments (anonymity set = whole pool), revealing only nf.
//   * Proofs:       RISC Zero zkVM STARK (FRI, hash-based, transparent setup),
//                   succinct receipts; statement in zk/shielded_stark/core.
//   * Amounts:      hidden inside cm; conservation + 64-bit range enforced in-proof.
//   * Receiver:     each output encrypted to recipient's ML-KEM-1024 key (fresh
//                   encapsulation per output => one-time, unlinkable ciphertexts),
//                   AES-256-GCM, fixed-size 512-byte encrypted memo.
#include "addition/block.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace addition::shielded {

using H32 = std::array<std::uint8_t, 32>;
using Bytes = std::vector<std::uint8_t>;

constexpr std::size_t kTreeDepth = 32;
constexpr std::size_t kMaxIn = 4;
constexpr std::size_t kMaxOut = 4;
constexpr std::size_t kMemoSize = 512;
constexpr std::size_t kRecentAnchors = 256;
constexpr std::size_t kMaxProofBytes = 4 * 1024 * 1024;

bool proof_system_available();          // false if built without the Rust STARK library
H32 image_id();                          // pinned guest image (consensus constant)

H32 sha256_dom(const std::string& dom, const std::vector<const Bytes*>& parts);
H32 owner_tag(const H32& nk);
H32 note_commitment(std::uint64_t value, const H32& owner, const H32& rho, const H32& r);
H32 nullifier(const H32& nk, const H32& rho);
H32 merkle_node(const H32& l, const H32& r);
H32 output_rho(const H32& bind, const std::vector<H32>& nfs, std::uint32_t j);

std::string hex32(const H32& h);

// ---- keys / addresses ------------------------------------------------------
struct Address {
    H32 owner{};
    Bytes kem_pk;  // ML-KEM-1024 encapsulation key
    std::string encode() const;                       // "addz1" + hex
    static bool decode(const std::string& s, Address& out, std::string& error);
};
struct SpendingKey {
    H32 nk{};      // nullifier/spending secret (never leaves the wallet)
    Bytes kem_sk;  // ML-KEM-1024 decapsulation key (incoming viewing key)
    Address address() const;
    Bytes kem_pk;
};
bool generate_spending_key(SpendingKey& out, std::string& error);

// ---- bundle ----------------------------------------------------------------
struct EncryptedNote {
    Bytes kem_ct;  // 1568 bytes
    Bytes aead;    // nonce(12) || ciphertext || tag(16)
};
struct Bundle {
    std::uint64_t vpub_in{0};   // transparent -> pool
    std::uint64_t vpub_out{0};  // pool -> transparent outputs + fee
    H32 anchor{};
    std::vector<H32> nullifiers;
    std::vector<H32> commitments;
    std::vector<EncryptedNote> enc;  // one per commitment
    Bytes proof;                     // bincode(risc0 succinct Receipt)
};
Bytes serialize(const Bundle& b);
bool deserialize(const Bytes& in, Bundle& out, std::string& error);
std::string to_tx_field(const Bundle& b);  // hex
bool from_tx_field(const std::string& hex, Bundle& out, std::string& error);

// Binds the proof to every other byte of the transaction except the proof itself
// and the journal fields (anchor/nf/cm) which the proof outputs.
H32 bind_hash(const Transaction& tx, const Bundle& b);
Bytes expected_journal(const H32& bind, const Bundle& b);

// ---- notes / wallet --------------------------------------------------------
struct Note {
    std::uint64_t value{0};
    H32 owner{};
    H32 rho{};
    H32 r{};
    std::uint64_t position{0};
    std::string memo;
};
bool encrypt_note(const Address& to, std::uint64_t value, const H32& r, const std::string& memo,
                  EncryptedNote& out, std::string& error);
// Trial-decrypt output j of a mined bundle; verifies the commitment.
std::optional<Note> try_decrypt(const SpendingKey& sk, const Transaction& tx, const Bundle& b, std::uint32_t j);

// ---- commitment tree -------------------------------------------------------
class CommitmentTree {
public:
    CommitmentTree();
    void append(const H32& cm);
    H32 root() const;
    std::size_t size() const { return leaves_.size(); }
    bool path(std::uint64_t pos, std::array<H32, kTreeDepth>& sib) const;  // O(n), wallet-side
private:
    std::vector<H32> leaves_;
    std::array<H32, kTreeDepth> frontier_{};
    std::array<H32, kTreeDepth + 1> empty_{};
    H32 root_{};
};

// ---- consensus state -------------------------------------------------------
class State {
public:
    State();
    // Cheap stateful checks (anchor known, nullifiers fresh, counts, no dup cm).
    bool check(const Bundle& b, const std::unordered_set<std::string>& pending_nfs, std::string& error) const;
    void apply(const Bundle& b);
    const CommitmentTree& tree() const { return tree_; }
    bool nullifier_spent(const H32& nf) const;
    std::size_t nullifier_count() const { return nfs_.size(); }
private:
    CommitmentTree tree_;
    std::deque<H32> recent_;
    std::unordered_set<std::string> recent_set_;
    std::unordered_set<std::string> nfs_;
    std::unordered_set<std::string> cms_;
    void remember_root();
};

// Stateless checks + STARK verification (expensive).
bool verify_bundle_proof(const Transaction& tx, const Bundle& b, std::string& error);

// Build a fully proven bundle. Inputs must carry valid positions in `tree`.
struct OutputSpec { Address to; std::uint64_t value{0}; std::string memo; };
bool build_bundle(const Transaction& tx_without_bundle,
                  const SpendingKey* spender, const std::vector<Note>& inputs, const CommitmentTree& tree,
                  const std::vector<OutputSpec>& outputs, std::uint64_t vpub_in, std::uint64_t vpub_out,
                  Bundle& out, std::string& error, double* prove_seconds = nullptr);

} // namespace addition::shielded
