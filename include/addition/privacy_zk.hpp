#pragma once

#include <cstdint>
#include <string>

namespace addition {

// Honest privacy claim labels for mint/spend paths.
// Live getinfo must keep reporting opening_not_zk until a real ZK backend verifies.
enum class PrivacyClaimLabel {
    OpeningNotZk,     // SHA3-512 opening (node learns trapdoor)
    MldsaWrapNotZk,   // legacy privacy_mint_zk signature wrap
    ZkPending,        // zk_v1 interface present; verifier fail-closed / not wired
    ZkV1,             // ONLY when backend_wired() and verify can succeed
};

std::string privacy_claim_label_string(PrivacyClaimLabel label);

// Live product claim for getinfo / privacy_status. Never zk_v1 while stubbed.
std::string live_privacy_claim();

// Parallel roadmap field (not a substitute for privacy_claim).
std::string privacy_zk_roadmap_label();

struct ZkMintPublicInputs {
    std::string owner;
    std::uint64_t amount{0};
    std::string commitment;
    std::string nullifier;
};

struct ZkSpendPublicInputs {
    std::string owner;
    std::string note_id;
    std::string recipient;
    std::uint64_t amount{0};
    std::string nullifier;
};

struct ZkProofMaterial {
    std::string proof_hex;
    std::string verification_key_hex;
};

// Interface for real ZK mint/spend verification (C++20).
// Implementations MUST be fail-closed until a concrete proof system is wired.
class PrivacyZkVerifier {
public:
    virtual ~PrivacyZkVerifier() = default;

    virtual std::string backend_id() const = 0;
    virtual bool backend_wired() const = 0;
    // May return ZkPending while unwired. Must not return ZkV1 unless verify can succeed.
    virtual PrivacyClaimLabel claim_label() const = 0;

    virtual bool verify_mint(const ZkMintPublicInputs& inputs,
                             const ZkProofMaterial& proof,
                             std::string& error) const = 0;
    virtual bool verify_spend(const ZkSpendPublicInputs& inputs,
                              const ZkProofMaterial& proof,
                              std::string& error) const = 0;
};

// Default production stub: always rejects. claim_label() == ZkPending.
class FailClosedPrivacyZkVerifier final : public PrivacyZkVerifier {
public:
    std::string backend_id() const override;
    bool backend_wired() const override;
    PrivacyClaimLabel claim_label() const override;
    bool verify_mint(const ZkMintPublicInputs& inputs,
                     const ZkProofMaterial& proof,
                     std::string& error) const override;
    bool verify_spend(const ZkSpendPublicInputs& inputs,
                      const ZkProofMaterial& proof,
                      std::string& error) const override;
};

const PrivacyZkVerifier& default_privacy_zk_verifier();

// High-level fail-closed path used by RPC. Never mutates notes while unwired.
// Claiming zk without a valid verified proof always fails closed (zk_pending).
bool privacy_zk_v1_mint_allowed(const ZkMintPublicInputs& inputs,
                                const ZkProofMaterial& proof,
                                std::string& error,
                                PrivacyClaimLabel& claim_out);
bool privacy_zk_v1_spend_allowed(const ZkSpendPublicInputs& inputs,
                                 const ZkProofMaterial& proof,
                                 std::string& error,
                                 PrivacyClaimLabel& claim_out);

// Optional note commitment for spend circuit statement validation (ledger cm).
// When empty, spend path still fail-closes on unwired backend / proof material.
bool privacy_zk_v1_spend_allowed(const ZkSpendPublicInputs& inputs,
                                 const ZkProofMaterial& proof,
                                 const std::string& note_commitment_hex,
                                 std::string& error,
                                 PrivacyClaimLabel& claim_out);

} // namespace addition
