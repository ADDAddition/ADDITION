#include "addition/crypto.hpp"
#include "addition/privacy.hpp"
#include "addition/wallet_keys.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

#ifdef _WIN32
void set_privacy_master_key() {
    _putenv_s("ADDITION_PRIVACY_MASTER_KEY", "research-testnet-privacy-master-key-32chars");
}
#else
void set_privacy_master_key() {
    setenv("ADDITION_PRIVACY_MASTER_KEY", "research-testnet-privacy-master-key-32chars", 1);
}
#endif

bool expect_contains(const std::string& hay, const std::string& needle, const char* label) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "test failed: " << label << " missing [" << needle << "] in [" << hay << "]\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    set_privacy_master_key();

    addition::PrivacyPool pool;
    if (pool.native_verifier_mode() != "pq_mldsa87") {
        std::cerr << "test failed: default native verifier is not pq_mldsa87\n";
        return 1;
    }
    if (!pool.strict_zk_mode()) {
        std::cerr << "test failed: strict_zk_mode must be on\n";
        return 1;
    }

    std::string mode_err;
    if (pool.set_native_verifier_mode("groth16", mode_err)) {
        std::cerr << "test failed: groth16 must not be accepted as a native verifier\n";
        return 1;
    }
    if (!expect_contains(mode_err, "pq_mldsa87", "unsupported mode error")) {
        return 1;
    }
    if (!pool.set_native_verifier_mode("pq_mldsa87", mode_err)) {
        std::cerr << "test failed: pq_mldsa87 rejected: " << mode_err << '\n';
        return 1;
    }

    addition::WalletKeys keys{};
    try {
        keys = addition::generate_wallet_keys();
    } catch (const std::exception& e) {
        std::cerr << "test failed: generate_wallet_keys: " << e.what() << '\n';
        return 1;
    }

    const std::string owner = keys.address;
    const std::uint64_t amount = 7;
    const std::string commitment = addition::to_hex(addition::sha3_512_bytes("cm|research|1"));
    const std::string nullifier = addition::to_hex(addition::sha3_512_bytes("nf|research|1"));
    const std::string public_input =
        "mint|" + owner + "|" + std::to_string(amount) + "|" + commitment + "|" + nullifier;

    std::string error;
    const auto garbage = pool.mint_zk(owner, amount, commitment, nullifier, "deadbeef", keys.public_key, error);
    if (!garbage.empty() || error.find("native verifier rejected proof") == std::string::npos) {
        std::cerr << "test failed: garbage proof must be rejected, got note=[" << garbage
                  << "] error=[" << error << "]\n";
        return 1;
    }

    error.clear();
    const auto even_junk = std::string(128, 'a');
    const auto junk_note = pool.mint_zk(owner, amount, commitment, nullifier, even_junk, keys.public_key, error);
    if (!junk_note.empty() || error.find("native verifier rejected proof") == std::string::npos) {
        std::cerr << "test failed: even-length junk must be rejected, got note=[" << junk_note
                  << "] error=[" << error << "]\n";
        return 1;
    }

    std::string pq_sig;
    try {
        pq_sig = addition::sign_message_hybrid(keys.private_key, public_input);
    } catch (const std::exception& e) {
        std::cerr << "test failed: sign mint public_input: " << e.what() << '\n';
        return 1;
    }
    if (pq_sig.rfind("pq=", 0) != 0) {
        std::cerr << "test failed: hybrid signature missing pq= prefix\n";
        return 1;
    }
    const auto proof_hex = pq_sig.substr(3);

    error.clear();
    const auto note_id = pool.mint_zk(owner, amount, commitment, nullifier, proof_hex, keys.public_key, error);
    if (note_id.empty() || !error.empty()) {
        std::cerr << "test failed: ML-DSA-wrapped mint_zk rejected: " << error << '\n';
        return 1;
    }
    if (pool.note_count() != 1) {
        std::cerr << "test failed: expected 1 note after mint, got " << pool.note_count() << '\n';
        return 1;
    }

    const std::string recipient = "recipient-research";
    const std::string spend_input = "spend|" + owner + "|" + note_id + "|" + recipient + "|" +
                                    std::to_string(amount) + "|" + nullifier;
    std::string spend_sig;
    try {
        spend_sig = addition::sign_message_hybrid(keys.private_key, spend_input);
    } catch (const std::exception& e) {
        std::cerr << "test failed: sign spend public_input: " << e.what() << '\n';
        return 1;
    }
    const auto spend_proof = spend_sig.substr(3);

    error.clear();
    std::string new_note;
    if (!pool.spend_zk(owner, note_id, recipient, amount, nullifier, spend_proof, keys.public_key, new_note, error) ||
        new_note.empty()) {
        std::cerr << "test failed: ML-DSA-wrapped spend_zk rejected: " << error << '\n';
        return 1;
    }

    error.clear();
    std::string replay_note;
    if (pool.spend_zk(owner, note_id, recipient, amount, nullifier, spend_proof, keys.public_key, replay_note, error)) {
        std::cerr << "test failed: replay spend_zk must fail\n";
        return 1;
    }
    if (error.find("already spent") == std::string::npos &&
        error.find("nullifier already used") == std::string::npos) {
        std::cerr << "test failed: replay error unexpected: " << error << '\n';
        return 1;
    }

    std::cout << "privacy tests passed\n";
    std::cout << "honest: privacy_mint_zk/privacy_spend_zk verify an ML-DSA-87 signature of a "
                 "public string (mint|... / spend|...). This is not a range proof, Groth16, or "
                 "Bulletproofs circuit.\n";
    std::cout << "note_id=" << note_id << " spend_note=" << new_note << '\n';
    return 0;
}
