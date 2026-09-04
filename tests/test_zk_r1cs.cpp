#include "addition/config.hpp"
#include "addition/privacy.hpp"
#include "addition/privacy_zk.hpp"
#include "addition/zk_circuit_v1.hpp"
#include "addition/zk_r1cs.hpp"

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
    // Mainnet PoW untouched (hard lock).
    const auto mainnet = addition::mainnet_chain_config();
    if (mainnet.pow_algorithm != addition::PowAlgorithm::MemoryHard ||
        mainnet.initial_difficulty_target != 0x000000FFFFFFFFFFULL) {
        std::cerr << "test failed: mainnet memory_hard must stay untouched\n";
        return 1;
    }

    // Honesty labels.
    if (!expect(std::string(addition::kZkR1csHonestyLabel) == "constraint_check_not_zk",
                "honesty label") ||
        !expect(!addition::zk_circuit_v1_proven(), "circuit still not_proven")) {
        return 1;
    }

    // --- Square circuit: good witness ---
    {
        addition::ZkR1csAssignment z;
        addition::ZkFieldElem y;
        std::string error;
        if (!addition::zk_r1cs_square_witness(12, z, y, error)) {
            std::cerr << "test failed: square witness: " << error << '\n';
            return 1;
        }
        if (!expect(y.value() == (12ULL * 12ULL) % addition::kZkR1csFieldPrime, "y=x^2")) {
            return 1;
        }
        const auto inst = addition::zk_r1cs_square_circuit();
        std::size_t failed = 0;
        if (!addition::zk_r1cs_evaluate(inst, z, error, failed)) {
            std::cerr << "test failed: square must satisfy: " << error << '\n';
            return 1;
        }
        // Tamper witness → reject.
        z[1] = addition::ZkFieldElem::from_u64(13);
        if (addition::zk_r1cs_evaluate(inst, z, error, failed)) {
            std::cerr << "test failed: tampered square must reject\n";
            return 1;
        }
        if (!expect(error.find("constraint_check_not_zk") != std::string::npos,
                    "tamper error labels honesty")) {
            std::cerr << "got: " << error << '\n';
            return 1;
        }
    }

    // --- Value conservation R1CS (schema-related C_value_conserved toy) ---
    {
        std::string error;
        addition::ZkConstraintEvalResult res{};
        if (!addition::zk_circuit_v1_eval_value_conservation_r1cs(100, 60, 40, res, error)) {
            std::cerr << "test failed: conservation must pass: " << error << '\n';
            return 1;
        }
        if (!expect(res.satisfied && res.implemented, "conservation satisfied") ||
            !expect(std::string(res.honesty) == "constraint_check_not_zk", "conservation honesty") ||
            !expect(res.id == addition::ZkConstraintId::CValueConserved, "conservation id")) {
            return 1;
        }
        if (addition::zk_circuit_v1_eval_value_conservation_r1cs(100, 60, 41, res, error)) {
            std::cerr << "test failed: bad conservation must reject\n";
            return 1;
        }
    }

    // --- Named opening constraints (C_cm / C_nf) via SHA3 — not ZK ---
    {
        addition::OpeningNote prepared{};
        std::string error;
        if (!addition::PrivacyPool::prepare_opening(9, prepared, error)) {
            std::cerr << "test failed: prepare_opening: " << error << '\n';
            return 1;
        }
        std::vector<addition::ZkConstraintEvalResult> results;
        if (!addition::zk_circuit_v1_eval_opening_constraints(9, prepared.trapdoor,
                                                              prepared.commitment,
                                                              prepared.nullifier, results,
                                                              error)) {
            std::cerr << "test failed: opening constraints: " << error << '\n';
            return 1;
        }
        bool saw_cm = false;
        bool saw_nf = false;
        for (const auto& r : results) {
            if (r.id == addition::ZkConstraintId::CCm) {
                saw_cm = r.satisfied && r.implemented &&
                         std::string(r.method) == "sha3_opening_check_not_zk";
            }
            if (r.id == addition::ZkConstraintId::CNf) {
                saw_nf = r.satisfied && r.implemented;
            }
        }
        if (!expect(saw_cm && saw_nf, "C_cm and C_nf evaluated")) {
            return 1;
        }
        // Wrong amount → not satisfied.
        if (addition::zk_circuit_v1_eval_opening_constraints(10, prepared.trapdoor,
                                                             prepared.commitment,
                                                             prepared.nullifier, results,
                                                             error)) {
            std::cerr << "test failed: wrong amount must fail constraint eval\n";
            return 1;
        }
    }

    // R1CS / constraint eval must NEVER upgrade product claims.
    if (addition::zk_circuit_v1_proven() ||
        addition::live_privacy_claim() != "opening_not_zk" ||
        addition::privacy_zk_roadmap_label() != "zk_pending") {
        std::cerr << "test failed: constraint eval must not upgrade ZK claims\n";
        return 1;
    }

    const auto info = addition::zk_circuit_v1_info_fields();
    if (!expect(info.find("zk_r1cs_evaluator=") != std::string::npos, "info r1cs") ||
        !expect(info.find("constraint_check_not_zk") != std::string::npos, "info honesty") ||
        !expect(info.find("zk_circuit_status=not_proven") != std::string::npos, "info status")) {
        std::cerr << "got info: " << info << '\n';
        return 1;
    }

    std::cout << "all zk_r1cs / constraint-eval tests passed\n";
    return 0;
}
