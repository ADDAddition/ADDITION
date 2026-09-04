#include "addition/config.hpp"
#include "addition/privacy_zk.hpp"
#include "addition/zk_circuit_v1.hpp"
#include "addition/zk_snark_v1.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "test failed: " << label << '\n';
        return false;
    }
    return true;
}

void set_flag(bool on) {
    if (on) {
        setenv("ADDITION_ZK_SNARK_V1", "1", 1);
    } else {
        unsetenv("ADDITION_ZK_SNARK_V1");
    }
}

} // namespace

int main() {
    // --- Mainnet PoW untouched ---
    const auto mainnet = addition::mainnet_chain_config();
    if (mainnet.pow_algorithm != addition::PowAlgorithm::MemoryHard ||
        mainnet.initial_difficulty_target != 0x000000FFFFFFFFFFULL) {
        std::cerr << "test failed: mainnet memory_hard target must stay 0x000000FFFFFFFFFF\n";
        return 1;
    }

    // --- Production honesty locks (always) ---
    if (!expect(addition::live_privacy_claim() == "opening_not_zk", "live claim") ||
        !expect(!addition::zk_circuit_v1_proven(), "sha3 circuit not proven") ||
        !expect(std::string(addition::zk_circuit_v1_status_label()) == "not_proven",
                "zk_circuit_status") ||
        !expect(!addition::default_privacy_zk_verifier().backend_wired(),
                "default zk verifier unwired")) {
        return 1;
    }

    // --- Fail-closed when flag off ---
    set_flag(false);
    if (!expect(!addition::zk_snark_v1_flag_enabled(), "flag off") ||
        !expect(std::string(addition::zk_snark_v1_status_label()) == "disabled", "status disabled")) {
        return 1;
    }
    addition::ZkSnarkV1Keys keys;
    std::string error;
    if (addition::zk_snark_v1_setup(keys, error)) {
        std::cerr << "test failed: setup must fail-closed without flag\n";
        return 1;
    }
    if (!expect(error.find("fail-closed") != std::string::npos, "setup fail-closed msg")) {
        std::cerr << "got: " << error << '\n';
        return 1;
    }
    if (!expect(!addition::optional_zk_snark_v1_verifier().backend_wired(),
                "optional verifier unwired")) {
        return 1;
    }

    // --- REAL SNARK: setup → prove → verify ---
    set_flag(true);
    if (!expect(addition::zk_snark_v1_flag_enabled(), "flag on") ||
        !expect(std::string(addition::zk_snark_v1_status_label()) == "available_poseidon_lab",
                "status available") ||
        !expect(std::string(addition::zk_snark_v1_system_name()).find("Groth16") != std::string::npos,
                "system name") ||
        !expect(std::string(addition::zk_snark_v1_circuit_hash_label()).find("Poseidon") !=
                    std::string::npos,
                "poseidon label") ||
        !expect(std::string(addition::zk_snark_v1_setup_label()).find("trusted_setup") !=
                    std::string::npos,
                "trusted setup label")) {
        return 1;
    }

    // Enabling SNARK lab must NOT flip production claims.
    if (!expect(addition::live_privacy_claim() == "opening_not_zk", "live claim still opening") ||
        !expect(!addition::zk_circuit_v1_proven(), "still not_proven for SHA3") ||
        !expect(!addition::default_privacy_zk_verifier().backend_wired(),
                "default verifier still unwired")) {
        return 1;
    }

    if (!addition::zk_snark_v1_setup(keys, error)) {
        std::cerr << "test failed: setup: " << error << '\n';
        return 1;
    }
    if (!expect(!keys.proving_key.empty() && !keys.verifying_key.empty(), "keys non-empty")) {
        return 1;
    }

    std::uint8_t trapdoor[32]{};
    trapdoor[31] = 0x2a;
    addition::ZkSnarkV1ProofBundle proof;
    if (!addition::zk_snark_v1_prove(keys, 7, trapdoor, proof, error)) {
        std::cerr << "test failed: prove: " << error << '\n';
        return 1;
    }
    if (!expect(!proof.proof.empty(), "proof bytes") ||
        !expect(proof.commitment32.size() == 32 && proof.nullifier32.size() == 32, "public frs")) {
        return 1;
    }

    if (!addition::zk_snark_v1_verify(keys.verifying_key, proof, 7, error)) {
        std::cerr << "test failed: verify good proof: " << error << '\n';
        return 1;
    }

    // Wrong public amount must FAIL.
    if (addition::zk_snark_v1_verify(keys.verifying_key, proof, 8, error)) {
        std::cerr << "test failed: wrong amount must reject\n";
        return 1;
    }
    if (!expect(error.find("rejected") != std::string::npos ||
                    error.find("invalid") != std::string::npos,
                "wrong amount error")) {
        std::cerr << "got: " << error << '\n';
        return 1;
    }

    // Tampered proof must FAIL.
    addition::ZkSnarkV1ProofBundle tampered = proof;
    if (!tampered.proof.empty()) {
        tampered.proof.back() ^= 0xff;
    }
    if (addition::zk_snark_v1_verify(keys.verifying_key, tampered, 7, error)) {
        std::cerr << "test failed: tampered proof must reject\n";
        return 1;
    }

    // Tampered commitment public input must FAIL.
    addition::ZkSnarkV1ProofBundle bad_cm = proof;
    bad_cm.commitment32[0] ^= 0x01;
    if (addition::zk_snark_v1_verify(keys.verifying_key, bad_cm, 7, error)) {
        std::cerr << "test failed: wrong cm must reject\n";
        return 1;
    }

    // Optional verifier accepts good proof when flag on.
    const auto& opt = addition::optional_zk_snark_v1_verifier();
    if (!expect(opt.backend_wired(), "optional wired") ||
        !opt.verify_opening_proof(keys.verifying_key, proof, 7, error)) {
        std::cerr << "test failed: optional verify: " << error << '\n';
        return 1;
    }

    // Production zk_v1 mint path still fail-closed (default verifier unwired).
    addition::ZkMintPublicInputs mint_in{
        "alice", 7,
        std::string(64, 'a'), // not used as SHA3; just even hex for scaffold checks
        std::string(64, 'b')};
    // Need 128 hex for circuit validate — pad.
    mint_in.commitment = std::string(128, 'a');
    mint_in.nullifier = std::string(128, 'b');
    addition::ZkProofMaterial garbage{std::string(64, 'c'), std::string(64, 'd')};
    addition::PrivacyClaimLabel claim = addition::PrivacyClaimLabel::ZkV1;
    if (addition::privacy_zk_v1_mint_allowed(mint_in, garbage, error, claim)) {
        std::cerr << "test failed: production zk_v1 mint must stay fail-closed\n";
        return 1;
    }
    if (claim == addition::PrivacyClaimLabel::ZkV1) {
        std::cerr << "test failed: must not claim zk_v1\n";
        return 1;
    }

    const auto info = addition::zk_snark_v1_info_fields();
    if (!expect(info.find("zk_snark_live_privacy_claim=opening_not_zk") != std::string::npos,
                "info live claim") ||
        !expect(info.find("zk_snark_production_sha3_circuit=not_proven") != std::string::npos,
                "info sha3 not proven")) {
        std::cerr << "got info: " << info << '\n';
        return 1;
    }

    std::cout << "all zk_snark_v1 tests passed\n";
    std::cout << "system=" << addition::zk_snark_v1_system_name() << '\n';
    std::cout << "hash=" << addition::zk_snark_v1_circuit_hash_label() << '\n';
    return 0;
}
