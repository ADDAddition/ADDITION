#include "addition/privacy_zk.hpp"

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

std::string reject_unwired(const char* op, std::string& error) {
    std::ostringstream oss;
    oss << "zk_v1 " << op << " rejected: backend not wired (fail-closed); "
        << "live privacy_claim remains opening_not_zk; roadmap="
        << privacy_zk_roadmap_label();
    error = oss.str();
    return error;
}

} // namespace

std::string privacy_claim_label_string(PrivacyClaimLabel label) {
    switch (label) {
        case PrivacyClaimLabel::OpeningNotZk:
            return "opening_not_zk";
        case PrivacyClaimLabel::MldsaWrapNotZk:
            return "mldsa_wrap_not_zk";
        case PrivacyClaimLabel::ZkPending:
            return "zk_pending";
        case PrivacyClaimLabel::ZkV1:
            return "zk_v1";
    }
    const PrivacyClaimLabel missing = label;
    switch (missing) {
        case PrivacyClaimLabel::OpeningNotZk:
        case PrivacyClaimLabel::MldsaWrapNotZk:
        case PrivacyClaimLabel::ZkPending:
        case PrivacyClaimLabel::ZkV1:
            break;
    }
    return "opening_not_zk";
}

std::string live_privacy_claim() {
    // Product live path is SHA3 opening until a wired verifier ships.
    return privacy_claim_label_string(PrivacyClaimLabel::OpeningNotZk);
}

std::string privacy_zk_roadmap_label() {
    const auto& v = default_privacy_zk_verifier();
    if (v.backend_wired() && v.claim_label() == PrivacyClaimLabel::ZkV1) {
        return privacy_claim_label_string(PrivacyClaimLabel::ZkV1);
    }
    return privacy_claim_label_string(PrivacyClaimLabel::ZkPending);
}

std::string FailClosedPrivacyZkVerifier::backend_id() const {
    return "fail_closed_stub";
}

bool FailClosedPrivacyZkVerifier::backend_wired() const {
    return false;
}

PrivacyClaimLabel FailClosedPrivacyZkVerifier::claim_label() const {
    return PrivacyClaimLabel::ZkPending;
}

bool FailClosedPrivacyZkVerifier::verify_mint(const ZkMintPublicInputs& inputs,
                                              const ZkProofMaterial& proof,
                                              std::string& error) const {
    (void)inputs;
    (void)proof;
    reject_unwired("mint", error);
    return false;
}

bool FailClosedPrivacyZkVerifier::verify_spend(const ZkSpendPublicInputs& inputs,
                                               const ZkProofMaterial& proof,
                                               std::string& error) const {
    (void)inputs;
    (void)proof;
    reject_unwired("spend", error);
    return false;
}

const PrivacyZkVerifier& default_privacy_zk_verifier() {
    static const FailClosedPrivacyZkVerifier instance;
    return instance;
}

bool privacy_zk_v1_mint_allowed(const ZkMintPublicInputs& inputs,
                                const ZkProofMaterial& proof,
                                std::string& error,
                                PrivacyClaimLabel& claim_out) {
    const auto& verifier = default_privacy_zk_verifier();
    claim_out = verifier.claim_label();

    if (inputs.owner.empty() || inputs.amount == 0) {
        error = "invalid zk_v1 mint params";
        return false;
    }
    if (!is_hex_even(inputs.commitment) || !is_hex_even(inputs.nullifier)) {
        error = "commitment/nullifier must be even-length hex";
        return false;
    }
    if (!is_hex_even(proof.proof_hex) || !is_hex_even(proof.verification_key_hex)) {
        error = "proof/vk must be even-length hex";
        return false;
    }

    if (!verifier.backend_wired()) {
        // Fail-closed: never mint; never upgrade claim to zk_v1.
        claim_out = PrivacyClaimLabel::ZkPending;
        reject_unwired("mint", error);
        return false;
    }

    if (!verifier.verify_mint(inputs, proof, error)) {
        if (claim_out == PrivacyClaimLabel::ZkV1) {
            // A wired backend that rejects must not advertise a successful zk_v1 mint.
            claim_out = PrivacyClaimLabel::ZkPending;
        }
        return false;
    }

    claim_out = PrivacyClaimLabel::ZkV1;
    return true;
}

bool privacy_zk_v1_spend_allowed(const ZkSpendPublicInputs& inputs,
                                 const ZkProofMaterial& proof,
                                 std::string& error,
                                 PrivacyClaimLabel& claim_out) {
    const auto& verifier = default_privacy_zk_verifier();
    claim_out = verifier.claim_label();

    if (inputs.owner.empty() || inputs.recipient.empty() || inputs.note_id.empty() ||
        inputs.amount == 0) {
        error = "invalid zk_v1 spend params";
        return false;
    }
    if (!is_hex_even(inputs.nullifier)) {
        error = "nullifier must be even-length hex";
        return false;
    }
    if (!is_hex_even(proof.proof_hex) || !is_hex_even(proof.verification_key_hex)) {
        error = "proof/vk must be even-length hex";
        return false;
    }

    if (!verifier.backend_wired()) {
        claim_out = PrivacyClaimLabel::ZkPending;
        reject_unwired("spend", error);
        return false;
    }

    if (!verifier.verify_spend(inputs, proof, error)) {
        if (claim_out == PrivacyClaimLabel::ZkV1) {
            claim_out = PrivacyClaimLabel::ZkPending;
        }
        return false;
    }

    claim_out = PrivacyClaimLabel::ZkV1;
    return true;
}

} // namespace addition
