#include "addition/config.hpp"
#include "addition/privacy.hpp"
#include "addition/privacy_zk.hpp"
#include "addition/zk_circuit_v1.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "test failed: " << label << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    // --- Mainnet PoW untouched ---
    const auto mainnet = addition::mainnet_chain_config();
    if (mainnet.pow_algorithm != addition::PowAlgorithm::MemoryHard ||
        mainnet.initial_difficulty_target != 0x000000FFFFFFFFFFULL ||
        mainnet.min_difficulty_target != 0x000000FFFFFFFFFFULL ||
        mainnet.max_difficulty_target != 0x000000FFFFFFFFFFULL) {
        std::cerr << "test failed: mainnet memory_hard target must stay 0x000000FFFFFFFFFF\n";
        return 1;
    }

    // --- Circuit status honesty ---
    if (!expect(!addition::zk_circuit_v1_proven(), "circuit not proven") ||
        !expect(std::string(addition::zk_circuit_v1_status_label()) == "not_proven", "status") ||
        !expect(std::string(addition::kZkCircuitV1Id) == "zk_circuit_v1", "circuit id")) {
        return 1;
    }
    const auto specs = addition::zk_circuit_v1_constraint_specs();
    if (!expect(specs.size() >= 5, "constraint scaffolding present")) {
        return 1;
    }
    bool saw_cm = false;
    bool saw_nf = false;
    for (const auto& s : specs) {
        if (s.id == addition::ZkConstraintId::CCm) {
            saw_cm = true;
        }
        if (s.id == addition::ZkConstraintId::CNf) {
            saw_nf = true;
        }
    }
    if (!expect(saw_cm && saw_nf, "C_cm and C_nf listed")) {
        return 1;
    }

    // --- REAL self-test: SHA3 opening relation (NOT ZK) ---
    addition::OpeningNote prepared{};
    std::string error;
    if (!addition::PrivacyPool::prepare_opening(7, prepared, error)) {
        std::cerr << "test failed: prepare_opening: " << error << '\n';
        return 1;
    }
    if (!addition::zk_circuit_v1_self_test_opening(7, prepared.trapdoor, prepared.commitment,
                                                   prepared.nullifier, error)) {
        std::cerr << "test failed: self-test must accept good opening: " << error << '\n';
        return 1;
    }
    if (addition::zk_circuit_v1_self_test_opening(8, prepared.trapdoor, prepared.commitment,
                                                  prepared.nullifier, error)) {
        std::cerr << "test failed: self-test must reject wrong amount\n";
        return 1;
    }
    // Self-test success must NEVER imply circuit proven / zk_v1.
    if (addition::zk_circuit_v1_proven() ||
        addition::live_privacy_claim() != "opening_not_zk" ||
        addition::privacy_zk_roadmap_label() != "zk_pending") {
        std::cerr << "test failed: self-test must not upgrade ZK claims\n";
        return 1;
    }

    // --- Public-input encoding + validation ---
    addition::ZkCircuitMintStatement mint_st{7, prepared.commitment, prepared.nullifier};
    const auto enc = addition::zk_circuit_v1_encode_mint_public(mint_st);
    if (!expect(enc.find("addition.zk_circuit_v1|mint|7|") == 0, "mint domain prefix") ||
        !expect(enc.find(prepared.commitment) != std::string::npos, "mint encodes cm") ||
        !addition::zk_circuit_v1_validate_mint_public(mint_st, error)) {
        std::cerr << "test failed: mint encode/validate: " << error << " enc=" << enc << '\n';
        return 1;
    }
    addition::ZkCircuitMintStatement bad_amt{0, prepared.commitment, prepared.nullifier};
    if (addition::zk_circuit_v1_validate_mint_public(bad_amt, error)) {
        std::cerr << "test failed: amount 0 must reject\n";
        return 1;
    }

    addition::ZkCircuitSpendStatement spend_st{7, prepared.commitment, prepared.nullifier, "bob"};
    const auto spend_enc = addition::zk_circuit_v1_encode_spend_public(spend_st);
    if (!expect(spend_enc.find("addition.zk_circuit_v1|spend|7|") == 0, "spend domain") ||
        !addition::zk_circuit_v1_validate_spend_public(spend_st, error)) {
        std::cerr << "test failed: spend encode/validate: " << error << '\n';
        return 1;
    }

    // --- Fail-closed prover: never emits ---
    const auto& prover = addition::default_zk_circuit_v1_prover();
    if (!expect(!prover.circuit_proven(), "prover unproven") ||
        !expect(prover.backend_id() == "fail_closed_circuit_prover", "prover id")) {
        return 1;
    }
    addition::ZkProofMaterial proof_out;
    addition::ZkCircuitMintWitness mint_wit{prepared.trapdoor};
    if (prover.prove_mint(mint_st, mint_wit, proof_out, error)) {
        std::cerr << "test failed: prove_mint must refuse while unproven\n";
        return 1;
    }
    if (!expect(error.find("fail-closed") != std::string::npos, "prove fail-closed") ||
        !expect(error.find("not proven") != std::string::npos, "prove not proven") ||
        !expect(proof_out.proof_hex.empty() && proof_out.verification_key_hex.empty(),
                "no proof bytes emitted")) {
        std::cerr << "got: " << error << '\n';
        return 1;
    }
    addition::ZkCircuitSpendWitness spend_wit{prepared.trapdoor};
    if (prover.prove_spend(spend_st, spend_wit, proof_out, error)) {
        std::cerr << "test failed: prove_spend must refuse\n";
        return 1;
    }

    // --- Claiming zk without valid proof fails closed ---
    addition::ZkMintPublicInputs mint_in{"alice", 7, prepared.commitment, prepared.nullifier};
    addition::ZkProofMaterial garbage{std::string(64, 'a'), std::string(64, 'b')};
    addition::PrivacyClaimLabel claim = addition::PrivacyClaimLabel::ZkV1;
    if (addition::privacy_zk_v1_mint_allowed(mint_in, garbage, error, claim)) {
        std::cerr << "test failed: garbage proof must not mint\n";
        return 1;
    }
    if (!expect(claim == addition::PrivacyClaimLabel::ZkPending, "claim pending") ||
        !expect(error.find("fail-closed") != std::string::npos, "mint fail-closed") ||
        !expect(error.find("opening_not_zk") != std::string::npos, "live claim honest")) {
        std::cerr << "got: " << error << '\n';
        return 1;
    }

    // Fake magic CLAIM_ZK_V1 as hex of ASCII must be rejected explicitly.
    const std::string magic_hex = "434c41494d5f5a4b5f563100"; // CLAIM_ZK_V1\0
    addition::ZkProofMaterial fake{magic_hex, std::string(64, 'c')};
    claim = addition::PrivacyClaimLabel::ZkV1;
    if (addition::privacy_zk_v1_mint_allowed(mint_in, fake, error, claim)) {
        std::cerr << "test failed: CLAIM_ZK_V1 magic must reject\n";
        return 1;
    }
    if (!expect(claim != addition::PrivacyClaimLabel::ZkV1, "no zk_v1 on fake") ||
        !expect(error.find("CLAIM_ZK_V1") != std::string::npos, "mentions fake magic")) {
        std::cerr << "got: " << error << '\n';
        return 1;
    }

    addition::ZkSpendPublicInputs spend_in{"alice", "note1", "bob", 7, prepared.nullifier};
    claim = addition::PrivacyClaimLabel::ZkV1;
    if (addition::privacy_zk_v1_spend_allowed(spend_in, garbage, prepared.commitment, error, claim)) {
        std::cerr << "test failed: spend must fail-closed\n";
        return 1;
    }
    if (claim == addition::PrivacyClaimLabel::ZkV1) {
        std::cerr << "test failed: spend reject must not claim zk_v1\n";
        return 1;
    }

    // Info fields honest.
    const auto info = addition::zk_circuit_v1_info_fields();
    if (!expect(info.find("zk_circuit_status=not_proven") != std::string::npos, "info status") ||
        !expect(info.find("zk_circuit_proven=false") != std::string::npos, "info proven") ||
        !expect(info.find("zk_circuit_id=zk_circuit_v1") != std::string::npos, "info id")) {
        std::cerr << "got info: " << info << '\n';
        return 1;
    }

    // Verifier still unwired.
    const auto& zk = addition::default_privacy_zk_verifier();
    if (!expect(!zk.backend_wired(), "verifier unwired") ||
        !expect(zk.claim_label() == addition::PrivacyClaimLabel::ZkPending, "verifier pending")) {
        return 1;
    }

    std::cout << "all zk_circuit_v1 tests passed\n";
    return 0;
}
