#pragma once

#include "addition/privacy_zk.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace addition {

// ADDITION zk_circuit_v1 — typed statement + fail-closed prover scaffold.
// Circuit work in progress / not live. See docs/ZK_CIRCUIT_V1.md.
// Self-test opening checks are REAL SHA3-512 relation tests, NOT zero-knowledge.

inline constexpr bool kZkCircuitV1Proven = false;
inline constexpr const char* kZkCircuitV1Id = "zk_circuit_v1";
inline constexpr const char* kZkCircuitV1DomainPrefix = "addition.zk_circuit_v1";

// Reject proof blobs that try to self-advertise zk_v1 without a wired backend.
inline constexpr const char* kZkFakeClaimMagic = "CLAIM_ZK_V1";

enum class ZkCircuitKind {
    Mint,
    Spend,
};

struct ZkCircuitMintWitness {
    std::string trapdoor_hex; // 64 hex; witness-only
};

struct ZkCircuitSpendWitness {
    std::string trapdoor_hex;
};

struct ZkCircuitMintStatement {
    std::uint64_t amount{0};
    std::string commitment_hex;
    std::string nullifier_hex;
};

struct ZkCircuitSpendStatement {
    std::uint64_t amount{0};
    std::string note_commitment_hex;
    std::string nullifier_hex;
    std::string recipient_tag;
};

enum class ZkConstraintId {
    CCm,
    CNf,
    CNfFresh,
    CValueConserved, // future
    CNoteMember,     // future
};

const char* zk_constraint_id_string(ZkConstraintId id);

struct ZkConstraintSpec {
    ZkConstraintId id;
    ZkCircuitKind kind;
    const char* informal;
};

// Named constraints a future backend must implement (not proven here).
std::vector<ZkConstraintSpec> zk_circuit_v1_constraint_specs();

bool zk_circuit_v1_proven();
const char* zk_circuit_v1_status_label(); // "not_proven" while scaffold

// Canonical domain-separated public-input encodings (future Fiat–Shamir input).
std::string zk_circuit_v1_encode_mint_public(const ZkCircuitMintStatement& st);
std::string zk_circuit_v1_encode_spend_public(const ZkCircuitSpendStatement& st);

bool zk_circuit_v1_validate_mint_public(const ZkCircuitMintStatement& st, std::string& error);
bool zk_circuit_v1_validate_spend_public(const ZkCircuitSpendStatement& st, std::string& error);
bool zk_circuit_v1_validate_proof_material(const ZkProofMaterial& proof, std::string& error);

// REAL: recomputes SHA3-512 opening with the witness. NOT a ZK proof.
bool zk_circuit_v1_self_test_opening(std::uint64_t amount,
                                     const std::string& trapdoor_hex,
                                     const std::string& commitment_hex,
                                     const std::string& nullifier_hex,
                                     std::string& error);

// Named-constraint evaluation (REAL checks where implemented).
// Honesty: this is constraint_check_not_zk — the evaluator sees the witness.
// It does NOT produce a ZK proof and must not flip zk_circuit_v1_proven / zk_v1.
struct ZkConstraintEvalResult {
    ZkConstraintId id;
    bool satisfied{false};
    bool implemented{false};
    const char* method;   // e.g. sha3_opening_check_not_zk | r1cs_field_eval | unimplemented
    const char* honesty;  // always constraint_check_not_zk when implemented
};

// Evaluate C_cm / C_nf via SHA3-512 opening (witness required). Other IDs:
// C_value_conserved can be checked via R1CS when in/out/change provided through
// the dedicated helper; C_nf_fresh / C_note_member remain unimplemented here.
bool zk_circuit_v1_eval_opening_constraints(std::uint64_t amount,
                                            const std::string& trapdoor_hex,
                                            const std::string& commitment_hex,
                                            const std::string& nullifier_hex,
                                            std::vector<ZkConstraintEvalResult>& results_out,
                                            std::string& error);

// REAL R1CS evaluation of C_value_conserved toy form (in == out + change).
// Label remains constraint_check_not_zk.
bool zk_circuit_v1_eval_value_conservation_r1cs(std::uint64_t in_value,
                                                std::uint64_t out_value,
                                                std::uint64_t change,
                                                ZkConstraintEvalResult& result_out,
                                                std::string& error);

// Convert RPC-facing public inputs into circuit statements.
ZkCircuitMintStatement zk_circuit_mint_from_rpc(const ZkMintPublicInputs& in);
ZkCircuitSpendStatement zk_circuit_spend_from_rpc(const ZkSpendPublicInputs& in,
                                                  const std::string& note_commitment_hex);

class ZkCircuitV1Prover {
public:
    virtual ~ZkCircuitV1Prover() = default;
    virtual std::string backend_id() const = 0;
    virtual bool circuit_proven() const = 0;
    virtual bool prove_mint(const ZkCircuitMintStatement& st,
                            const ZkCircuitMintWitness& wit,
                            ZkProofMaterial& proof_out,
                            std::string& error) const = 0;
    virtual bool prove_spend(const ZkCircuitSpendStatement& st,
                             const ZkCircuitSpendWitness& wit,
                             ZkProofMaterial& proof_out,
                             std::string& error) const = 0;
};

// Production default: always refuses to emit a proof.
class FailClosedZkCircuitV1Prover final : public ZkCircuitV1Prover {
public:
    std::string backend_id() const override;
    bool circuit_proven() const override;
    bool prove_mint(const ZkCircuitMintStatement& st,
                    const ZkCircuitMintWitness& wit,
                    ZkProofMaterial& proof_out,
                    std::string& error) const override;
    bool prove_spend(const ZkCircuitSpendStatement& st,
                     const ZkCircuitSpendWitness& wit,
                     ZkProofMaterial& proof_out,
                     std::string& error) const override;
};

const ZkCircuitV1Prover& default_zk_circuit_v1_prover();

// Space-prefixed status fields for privacy_status / getinfo honesty.
std::string zk_circuit_v1_info_fields();

} // namespace addition
