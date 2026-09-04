#include "addition/zk_circuit_v1.hpp"

#include "addition/privacy.hpp"

#include <cctype>
#include <sstream>

namespace addition {
namespace {

bool is_hex_even(const std::string& s) {
    if (s.empty() || (s.size() % 2) != 0) {
        return false;
    }
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (!((u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F'))) {
            return false;
        }
    }
    return true;
}

bool looks_like_fake_zk_claim(const std::string& hex_or_ascii) {
    // Detect ASCII magic embedded in proof/vk material (hex-decoded or raw).
    if (hex_or_ascii.find(kZkFakeClaimMagic) != std::string::npos) {
        return true;
    }
    // Hex encoding of ASCII "CLAIM_ZK_V1" prefix (uppercase/lowercase hex).
    static const char* kMagicHexUpper = "434C41494D5F5A4B5F5631";
    static const char* kMagicHexLower = "434c41494d5f5a4b5f5631";
    std::string lower;
    lower.reserve(hex_or_ascii.size());
    for (char c : hex_or_ascii) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower.find(kMagicHexLower) != std::string::npos) {
        return true;
    }
    if (hex_or_ascii.find(kMagicHexUpper) != std::string::npos) {
        return true;
    }
    return false;
}

std::string reject_unproven(const char* op, std::string& error) {
    std::ostringstream oss;
    oss << "zk_circuit_v1 " << op
        << " rejected: circuit not proven (fail-closed); "
        << "status=" << zk_circuit_v1_status_label()
        << "; live privacy_claim remains opening_not_zk; "
        << "see docs/ZK_CIRCUIT_V1.md";
    error = oss.str();
    return error;
}

} // namespace

const char* zk_constraint_id_string(ZkConstraintId id) {
    switch (id) {
        case ZkConstraintId::CCm:
            return "C_cm";
        case ZkConstraintId::CNf:
            return "C_nf";
        case ZkConstraintId::CNfFresh:
            return "C_nf_fresh";
        case ZkConstraintId::CValueConserved:
            return "C_value_conserved";
        case ZkConstraintId::CNoteMember:
            return "C_note_member";
    }
    const ZkConstraintId missing = id;
    switch (missing) {
        case ZkConstraintId::CCm:
        case ZkConstraintId::CNf:
        case ZkConstraintId::CNfFresh:
        case ZkConstraintId::CValueConserved:
        case ZkConstraintId::CNoteMember:
            break;
    }
    return "C_unknown";
}

std::vector<ZkConstraintSpec> zk_circuit_v1_constraint_specs() {
    return {
        {ZkConstraintId::CCm, ZkCircuitKind::Mint,
         "cm == SHA3-512(cm|v1|amount|trapdoor)"},
        {ZkConstraintId::CNf, ZkCircuitKind::Mint,
         "nf == SHA3-512(nf|v1|cm|trapdoor)"},
        {ZkConstraintId::CCm, ZkCircuitKind::Spend,
         "note_cm == SHA3-512(cm|v1|amount|trapdoor)"},
        {ZkConstraintId::CNf, ZkCircuitKind::Spend,
         "nf == SHA3-512(nf|v1|note_cm|trapdoor)"},
        {ZkConstraintId::CNfFresh, ZkCircuitKind::Spend,
         "nullifier not previously used (ledger)"},
        {ZkConstraintId::CValueConserved, ZkCircuitKind::Spend,
         "future: in_value == out_value + change"},
        {ZkConstraintId::CNoteMember, ZkCircuitKind::Spend,
         "future: note membership / Merkle root"},
    };
}

bool zk_circuit_v1_proven() {
    return kZkCircuitV1Proven;
}

const char* zk_circuit_v1_status_label() {
    if (zk_circuit_v1_proven()) {
        return "proven";
    }
    return "not_proven";
}

std::string zk_circuit_v1_encode_mint_public(const ZkCircuitMintStatement& st) {
    std::ostringstream oss;
    oss << kZkCircuitV1DomainPrefix << "|mint|" << st.amount << '|' << st.commitment_hex << '|'
        << st.nullifier_hex;
    return oss.str();
}

std::string zk_circuit_v1_encode_spend_public(const ZkCircuitSpendStatement& st) {
    std::ostringstream oss;
    oss << kZkCircuitV1DomainPrefix << "|spend|" << st.amount << '|' << st.note_commitment_hex << '|'
        << st.nullifier_hex << '|' << st.recipient_tag;
    return oss.str();
}

bool zk_circuit_v1_validate_mint_public(const ZkCircuitMintStatement& st, std::string& error) {
    if (st.amount == 0) {
        error = "zk_circuit_v1 mint: amount must be > 0";
        return false;
    }
    if (!is_hex_even(st.commitment_hex) || st.commitment_hex.size() != 128) {
        error = "zk_circuit_v1 mint: commitment must be 128 hex chars";
        return false;
    }
    if (!is_hex_even(st.nullifier_hex) || st.nullifier_hex.size() != 128) {
        error = "zk_circuit_v1 mint: nullifier must be 128 hex chars";
        return false;
    }
    error.clear();
    return true;
}

bool zk_circuit_v1_validate_spend_public(const ZkCircuitSpendStatement& st, std::string& error) {
    if (st.amount == 0) {
        error = "zk_circuit_v1 spend: amount must be > 0";
        return false;
    }
    if (st.recipient_tag.empty()) {
        error = "zk_circuit_v1 spend: recipient_tag required";
        return false;
    }
    if (!is_hex_even(st.note_commitment_hex) || st.note_commitment_hex.size() != 128) {
        error = "zk_circuit_v1 spend: note_commitment must be 128 hex chars";
        return false;
    }
    if (!is_hex_even(st.nullifier_hex) || st.nullifier_hex.size() != 128) {
        error = "zk_circuit_v1 spend: nullifier must be 128 hex chars";
        return false;
    }
    error.clear();
    return true;
}

bool zk_circuit_v1_validate_proof_material(const ZkProofMaterial& proof, std::string& error) {
    if (!is_hex_even(proof.proof_hex) || !is_hex_even(proof.verification_key_hex)) {
        error = "zk_circuit_v1: proof/vk must be even-length hex";
        return false;
    }
    if (looks_like_fake_zk_claim(proof.proof_hex) || looks_like_fake_zk_claim(proof.verification_key_hex)) {
        error = "zk_circuit_v1: rejecting self-advertised CLAIM_ZK_V1 proof material (fail-closed)";
        return false;
    }
    if (!zk_circuit_v1_proven()) {
        reject_unproven("verify", error);
        return false;
    }
    error.clear();
    return true;
}

bool zk_circuit_v1_self_test_opening(std::uint64_t amount,
                                     const std::string& trapdoor_hex,
                                     const std::string& commitment_hex,
                                     const std::string& nullifier_hex,
                                     std::string& error) {
    // Honest label path: this is the opening relation hash check, not ZK.
    if (!PrivacyPool::verify_opening(amount, trapdoor_hex, commitment_hex, nullifier_hex, error)) {
        if (error.empty()) {
            error = "zk_circuit_v1 self-test opening rejected";
        }
        return false;
    }
    error.clear();
    return true;
}

ZkCircuitMintStatement zk_circuit_mint_from_rpc(const ZkMintPublicInputs& in) {
    return ZkCircuitMintStatement{in.amount, in.commitment, in.nullifier};
}

ZkCircuitSpendStatement zk_circuit_spend_from_rpc(const ZkSpendPublicInputs& in,
                                                  const std::string& note_commitment_hex) {
    return ZkCircuitSpendStatement{in.amount, note_commitment_hex, in.nullifier, in.recipient};
}

std::string FailClosedZkCircuitV1Prover::backend_id() const {
    return "fail_closed_circuit_prover";
}

bool FailClosedZkCircuitV1Prover::circuit_proven() const {
    return zk_circuit_v1_proven();
}

bool FailClosedZkCircuitV1Prover::prove_mint(const ZkCircuitMintStatement& st,
                                             const ZkCircuitMintWitness& wit,
                                             ZkProofMaterial& proof_out,
                                             std::string& error) const {
    (void)wit;
    proof_out = {};
    if (!zk_circuit_v1_validate_mint_public(st, error)) {
        return false;
    }
    // Even with a valid witness self-test, refuse to emit a proof while unproven.
    reject_unproven("prove_mint", error);
    return false;
}

bool FailClosedZkCircuitV1Prover::prove_spend(const ZkCircuitSpendStatement& st,
                                              const ZkCircuitSpendWitness& wit,
                                              ZkProofMaterial& proof_out,
                                              std::string& error) const {
    (void)wit;
    proof_out = {};
    if (!zk_circuit_v1_validate_spend_public(st, error)) {
        return false;
    }
    reject_unproven("prove_spend", error);
    return false;
}

const ZkCircuitV1Prover& default_zk_circuit_v1_prover() {
    static const FailClosedZkCircuitV1Prover instance;
    return instance;
}

std::string zk_circuit_v1_info_fields() {
    std::ostringstream out;
    out << " zk_circuit_id=" << kZkCircuitV1Id
        << " zk_circuit_status=" << zk_circuit_v1_status_label()
        << " zk_circuit_proven=" << (zk_circuit_v1_proven() ? "true" : "false")
        << " zk_circuit_prover=" << default_zk_circuit_v1_prover().backend_id()
        << " zk_circuit_constraints=" << zk_circuit_v1_constraint_specs().size();
    return out.str();
}

} // namespace addition
