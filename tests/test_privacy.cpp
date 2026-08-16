#include "addition/crypto.hpp"
#include "addition/privacy.hpp"

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

    std::cout << "all privacy opening tests passed\n";
    return 0;
}
