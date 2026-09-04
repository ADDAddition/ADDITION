#include "addition/config.hpp"
#include "addition/privacy.hpp"
#include "addition/privacy_zk.hpp"
#include "addition/zk_circuit_v1.hpp"
#include "addition/zk_toy_proof.hpp"

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
    // Hard locks: mainnet PoW + live claim unchanged by toy proof success.
    const auto mainnet = addition::mainnet_chain_config();
    if (mainnet.pow_algorithm != addition::PowAlgorithm::MemoryHard ||
        mainnet.initial_difficulty_target != 0x000000FFFFFFFFFFULL ||
        mainnet.min_difficulty_target != 0x000000FFFFFFFFFFULL) {
        std::cerr << "test failed: mainnet memory_hard target must stay 0x000000FFFFFFFFFF\n";
        return 1;
    }

    if (!expect(std::string(addition::kZkToyProofId) == "zk_toy_schnorr_fs_dl_v1", "toy id") ||
        !expect(std::string(addition::kZkToyProofHonesty).find("not_live") != std::string::npos,
                "toy honesty") ||
        !expect(!addition::zk_circuit_v1_proven(), "circuit not proven before")) {
        return 1;
    }

    addition::ZkToySchnorrPublic pub;
    addition::ZkToySchnorrWitness wit;
    std::string error;
    if (!addition::zk_toy_schnorr_keygen(pub, wit, error)) {
        std::cerr << "test failed: keygen: " << error << '\n';
        return 1;
    }
    if (!expect(!pub.Y_hex.empty() && !wit.x_hex.empty(), "keygen non-empty")) {
        return 1;
    }

    addition::ZkToySchnorrProof proof;
    if (!addition::zk_toy_schnorr_prove(pub, wit, proof, error)) {
        std::cerr << "test failed: prove: " << error << '\n';
        return 1;
    }
    if (!expect(!proof.R_hex.empty() && !proof.s_hex.empty(), "proof fields")) {
        return 1;
    }

    // REAL verify accepts honest proof.
    if (!addition::zk_toy_schnorr_verify(pub, proof, error)) {
        std::cerr << "test failed: verify honest proof: " << error << '\n';
        return 1;
    }

    // Invalid proofs reject.
    addition::ZkToySchnorrProof bad = proof;
    if (!bad.s_hex.empty()) {
        // Flip last nibble.
        char& c = bad.s_hex.back();
        c = (c == '0') ? '1' : '0';
    }
    if (addition::zk_toy_schnorr_verify(pub, bad, error)) {
        std::cerr << "test failed: tampered s must reject\n";
        return 1;
    }
    if (!expect(error.find("verification equation failed") != std::string::npos ||
                    error.find("out of range") != std::string::npos,
                "tamper error")) {
        std::cerr << "got: " << error << '\n';
        return 1;
    }

    addition::ZkToySchnorrProof empty{};
    if (addition::zk_toy_schnorr_verify(pub, empty, error)) {
        std::cerr << "test failed: empty proof must reject\n";
        return 1;
    }

    // Wrong public key rejects a proof for another key.
    addition::ZkToySchnorrPublic pub2;
    addition::ZkToySchnorrWitness wit2;
    if (!addition::zk_toy_schnorr_keygen(pub2, wit2, error)) {
        std::cerr << "test failed: keygen2: " << error << '\n';
        return 1;
    }
    if (pub2.Y_hex == pub.Y_hex) {
        // Extremely unlikely; still fail closed on coincidence.
        std::cerr << "test failed: keygen collision (retry)\n";
        return 1;
    }
    if (addition::zk_toy_schnorr_verify(pub2, proof, error)) {
        std::cerr << "test failed: proof must not verify under wrong Y\n";
        return 1;
    }

    // CRITICAL: successful toy verify must NOT flip live product / circuit proven.
    if (addition::zk_circuit_v1_proven() ||
        std::string(addition::zk_circuit_v1_status_label()) != "not_proven" ||
        addition::live_privacy_claim() != "opening_not_zk" ||
        addition::privacy_zk_roadmap_label() != "zk_pending") {
        std::cerr << "test failed: toy proof must not upgrade live ZK claims\n";
        return 1;
    }
    const auto& verifier = addition::default_privacy_zk_verifier();
    if (!expect(!verifier.backend_wired(), "production verifier still unwired") ||
        !expect(verifier.claim_label() == addition::PrivacyClaimLabel::ZkPending,
                "claim still pending")) {
        return 1;
    }

    // Garbage on zk_v1 mint path still fail-closed (toy proof is not a mint proof).
    addition::ZkMintPublicInputs mint_in{"alice", 1, std::string(128, 'a'), std::string(128, 'b')};
    addition::ZkProofMaterial garbage{proof.R_hex + proof.s_hex, pub.Y_hex};
    // Pad to even hex if needed — R/s/Y are already hex.
    if ((garbage.proof_hex.size() % 2) != 0) {
        garbage.proof_hex.push_back('0');
    }
    if ((garbage.verification_key_hex.size() % 2) != 0) {
        garbage.verification_key_hex.push_back('0');
    }
    // Commitments must be 128 hex for circuit validate — already are.
    addition::PrivacyClaimLabel claim = addition::PrivacyClaimLabel::ZkV1;
    // Use valid-looking cm/nf lengths; still fail-closed on unproven circuit.
    addition::OpeningNote prepared{};
    if (!addition::PrivacyPool::prepare_opening(3, prepared, error)) {
        std::cerr << "test failed: prepare for mint gate: " << error << '\n';
        return 1;
    }
    mint_in.commitment = prepared.commitment;
    mint_in.nullifier = prepared.nullifier;
    if (addition::privacy_zk_v1_mint_allowed(mint_in, garbage, error, claim)) {
        std::cerr << "test failed: toy schnorr bytes must not mint on zk_v1 path\n";
        return 1;
    }
    if (claim == addition::PrivacyClaimLabel::ZkV1) {
        std::cerr << "test failed: mint reject must not claim zk_v1\n";
        return 1;
    }

    const auto info = addition::zk_circuit_v1_info_fields();
    if (!expect(info.find("zk_toy_proof_id=zk_toy_schnorr_fs_dl_v1") != std::string::npos,
                "info toy id") ||
        !expect(info.find("zk_circuit_proven=false") != std::string::npos, "info proven false")) {
        std::cerr << "got info: " << info << '\n';
        return 1;
    }

    std::cout << "all zk_toy_schnorr tests passed (real prove+verify; live claim unchanged)\n";
    return 0;
}
