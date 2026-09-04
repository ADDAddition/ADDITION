#include "addition/crypto.hpp"
#include "addition/privacy.hpp"
#include "addition/privacy_zk.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace {

void set_master_key() {
    const char* key = "addition-research-privacy-master-key-32";
#ifdef _WIN32
    _putenv_s("ADDITION_PRIVACY_MASTER_KEY", key);
#else
    setenv("ADDITION_PRIVACY_MASTER_KEY", key, 1);
#endif
}

bool expect(bool cond, const char* label) {
    if (!cond) {
        std::cerr << "test failed: " << label << '\n';
        return false;
    }
    return true;
}

} // namespace

int main() {
    set_master_key();

    addition::OpeningNote prepared{};
    std::string error;
    if (!addition::PrivacyPool::prepare_opening(25, prepared, error)) {
        std::cerr << "test failed: prepare_opening: " << error << '\n';
        return 1;
    }
    if (!expect(prepared.trapdoor.size() == 64, "trapdoor 32 bytes") ||
        !expect(!prepared.commitment.empty(), "commitment") ||
        !expect(!prepared.nullifier.empty(), "nullifier")) {
        return 1;
    }

    std::string cm;
    std::string nf;
    addition::PrivacyPool::compute_opening_relation(25, prepared.trapdoor, cm, nf);
    if (!expect(cm == prepared.commitment && nf == prepared.nullifier, "relation matches prepare")) {
        return 1;
    }
    if (!addition::PrivacyPool::verify_opening(25, prepared.trapdoor, cm, nf, error)) {
        std::cerr << "test failed: verify good opening: " << error << '\n';
        return 1;
    }

    std::string bad_err;
    if (addition::PrivacyPool::verify_opening(25, std::string(64, '0'), cm, nf, bad_err)) {
        std::cerr << "test failed: garbage trapdoor must be rejected\n";
        return 1;
    }
    if (!expect(bad_err == "opening relation rejected", "garbage error text")) {
        std::cerr << "got: " << bad_err << '\n';
        return 1;
    }
    if (addition::PrivacyPool::verify_opening(26, prepared.trapdoor, cm, nf, bad_err)) {
        std::cerr << "test failed: wrong amount must be rejected\n";
        return 1;
    }

    {
        unsetenv("ADDITION_PRIVACY_MASTER_KEY");
#ifdef _WIN32
        _putenv_s("ADDITION_PRIVACY_MASTER_KEY", "");
#endif
        addition::PrivacyPool bare;
        addition::OpeningNote short_prep{};
        std::string short_err;
        if (!addition::PrivacyPool::prepare_opening(1, short_prep, short_err)) {
            std::cerr << "test failed: prepare_opening without key should still hash: " << short_err << '\n';
            return 1;
        }
        if (bare.mint_open("alice", 1, short_prep.commitment, short_prep.nullifier, short_prep.trapdoor, short_err).size()) {
            std::cerr << "test failed: mint_open must fail without master key\n";
            return 1;
        }
        if (short_err.find("ADDITION_PRIVACY_MASTER_KEY") == std::string::npos ||
            short_err.find("min 32") == std::string::npos) {
            std::cerr << "test failed: short/missing key error: " << short_err << '\n';
            return 1;
        }
        if (addition::PrivacyPool::master_key_configured()) {
            std::cerr << "test failed: master_key_configured must be false when unset\n";
            return 1;
        }
#ifdef _WIN32
        _putenv_s("ADDITION_PRIVACY_MASTER_KEY", "too-short");
#else
        setenv("ADDITION_PRIVACY_MASTER_KEY", "too-short", 1);
#endif
        if (addition::PrivacyPool::master_key_configured()) {
            std::cerr << "test failed: master_key_configured must reject <32 chars\n";
            return 1;
        }
        set_master_key();
        if (!addition::PrivacyPool::master_key_configured()) {
            std::cerr << "test failed: master_key_configured after set\n";
            return 1;
        }
    }

    addition::PrivacyPool pool;
    std::string mode_err;
    if (pool.set_native_verifier_mode("bulletproofs", mode_err) ||
        mode_err.find("SHA3 opening") == std::string::npos) {
        std::cerr << "test failed: bulletproofs mode must fail: " << mode_err << '\n';
        return 1;
    }
    if (pool.set_native_verifier_mode("zk-snark", mode_err) ||
        mode_err.find("SHA3 opening") == std::string::npos) {
        std::cerr << "test failed: zk-snark mode must fail: " << mode_err << '\n';
        return 1;
    }
    const auto note_id = pool.mint_open("alice", 25, prepared.commitment, prepared.nullifier, prepared.trapdoor, error);
    if (note_id.empty()) {
        std::cerr << "test failed: mint_open: " << error << '\n';
        return 1;
    }
    if (pool.note_count() != 1) {
        std::cerr << "test failed: note_count after mint\n";
        return 1;
    }

    const auto dup = pool.mint_open("alice", 25, prepared.commitment, prepared.nullifier, prepared.trapdoor, error);
    if (!dup.empty() || error != "nullifier already assigned") {
        std::cerr << "test failed: duplicate nullifier: " << error << '\n';
        return 1;
    }

    std::string new_note;
    addition::OpeningNote recv{};
    std::string change_note;
    addition::OpeningNote change{};
    if (pool.spend_open("alice", note_id, "bob", 25, std::string(64, 'a'), new_note, recv, change_note, change, error)) {
        std::cerr << "test failed: spend with wrong trapdoor must fail\n";
        return 1;
    }

    if (!pool.spend_open("alice", note_id, "bob", 10, prepared.trapdoor, new_note, recv, change_note, change, error)) {
        std::cerr << "test failed: spend_open: " << error << '\n';
        return 1;
    }
    if (!expect(!new_note.empty(), "recipient note") ||
        !expect(!change_note.empty(), "change note") ||
        !expect(pool.used_nullifier_count() == 1, "one used nullifier") ||
        !expect(pool.note_count() == 3, "spent + recipient + change")) {
        return 1;
    }
    if (!addition::PrivacyPool::verify_opening(10, recv.trapdoor, recv.commitment, recv.nullifier, error)) {
        std::cerr << "test failed: recipient opening: " << error << '\n';
        return 1;
    }
    if (!addition::PrivacyPool::verify_opening(15, change.trapdoor, change.commitment, change.nullifier, error)) {
        std::cerr << "test failed: change opening: " << error << '\n';
        return 1;
    }

    if (pool.spend_open("alice", note_id, "bob", 10, prepared.trapdoor, new_note, recv, change_note, change, error)) {
        std::cerr << "test failed: double spend must fail\n";
        return 1;
    }

    std::string cm_amt;
    std::string nf_amt;
    addition::PrivacyPool::compute_opening_relation(26, prepared.trapdoor, cm_amt, nf_amt);
    if (cm_amt == prepared.commitment || nf_amt == prepared.nullifier) {
        std::cerr << "test failed: v1 opening must bind amount into commitment and nullifier\n";
        return 1;
    }

    const auto remint = pool.mint_open("alice", 25, prepared.commitment, prepared.nullifier, prepared.trapdoor, error);
    if (!remint.empty() || (error != "nullifier already used" && error != "commitment already spent")) {
        std::cerr << "test failed: remint spent opening: " << error << '\n';
        return 1;
    }
    if (pool.spent_commitment_count() == 0) {
        std::cerr << "test failed: spent commitment must be recorded\n";
        return 1;
    }

    // --- Real ZK path scaffold: fail-closed, honest labels ---
    {
        const auto& zk = addition::default_privacy_zk_verifier();
        if (!expect(!zk.backend_wired(), "zk backend unwired") ||
            !expect(zk.backend_id() == "fail_closed_stub", "zk backend id") ||
            !expect(zk.claim_label() == addition::PrivacyClaimLabel::ZkPending, "zk claim pending") ||
            !expect(addition::live_privacy_claim() == "opening_not_zk", "live claim opening") ||
            !expect(addition::privacy_zk_roadmap_label() == "zk_pending", "roadmap pending") ||
            !expect(addition::privacy_claim_label_string(addition::PrivacyClaimLabel::ZkV1) == "zk_v1",
                    "zk_v1 string exists for future")) {
            return 1;
        }

        addition::ZkMintPublicInputs mint_in{"alice", 5, std::string(128, 'a'), std::string(128, 'b')};
        addition::ZkProofMaterial material{std::string(64, 'c'), std::string(64, 'd')};
        addition::PrivacyClaimLabel claim = addition::PrivacyClaimLabel::ZkV1;
        std::string zk_err;
        if (addition::privacy_zk_v1_mint_allowed(mint_in, material, zk_err, claim)) {
            std::cerr << "test failed: zk_v1 mint must reject without proofs\n";
            return 1;
        }
        if (!expect(claim == addition::PrivacyClaimLabel::ZkPending, "mint reject claim") ||
            !expect(zk_err.find("fail-closed") != std::string::npos, "mint fail-closed text") ||
            !expect(zk_err.find("opening_not_zk") != std::string::npos, "mint keeps live claim honest")) {
            std::cerr << "got claim/err: " << addition::privacy_claim_label_string(claim) << " / " << zk_err
                      << '\n';
            return 1;
        }
        // Empty / odd hex must not slip through as zk_v1.
        addition::ZkMintPublicInputs bad_hex{"alice", 1, "abc", "00"};
        claim = addition::PrivacyClaimLabel::ZkV1;
        if (addition::privacy_zk_v1_mint_allowed(bad_hex, material, zk_err, claim)) {
            std::cerr << "test failed: odd commitment hex must reject\n";
            return 1;
        }
        if (claim == addition::PrivacyClaimLabel::ZkV1) {
            std::cerr << "test failed: reject must not leave claim=zk_v1\n";
            return 1;
        }

        addition::ZkSpendPublicInputs spend_in{"alice", "note", "bob", 1, std::string(128, 'e')};
        claim = addition::PrivacyClaimLabel::ZkV1;
        if (addition::privacy_zk_v1_spend_allowed(spend_in, material, zk_err, claim)) {
            std::cerr << "test failed: zk_v1 spend must reject without proofs\n";
            return 1;
        }
        if (!expect(claim == addition::PrivacyClaimLabel::ZkPending, "spend reject claim") ||
            !expect(zk_err.find("fail-closed") != std::string::npos, "spend fail-closed text")) {
            std::cerr << "got: " << zk_err << '\n';
            return 1;
        }

        // Opening path still works after zk rejects (no shared state mutation).
        addition::OpeningNote still{};
        if (!addition::PrivacyPool::prepare_opening(3, still, error)) {
            std::cerr << "test failed: opening still works after zk reject: " << error << '\n';
            return 1;
        }
        addition::PrivacyPool pool2;
        const auto n2 = pool2.mint_open("carol", 3, still.commitment, still.nullifier, still.trapdoor, error);
        if (n2.empty()) {
            std::cerr << "test failed: mint_open after zk scaffold: " << error << '\n';
            return 1;
        }
    }

    std::cout << "all privacy opening + zk scaffold tests passed\n";
    return 0;
}
