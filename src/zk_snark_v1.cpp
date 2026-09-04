#include "addition/zk_snark_v1.hpp"

#include "addition_snark_v1.h"

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace addition {
namespace {

std::string err_code_string(int code) {
    switch (code) {
        case ADDITION_SNARK_V1_OK:
            return "ok";
        case ADDITION_SNARK_V1_ERR_NULL:
            return "null_arg";
        case ADDITION_SNARK_V1_ERR_BAD_ARG:
            return "bad_arg";
        case ADDITION_SNARK_V1_ERR_SETUP:
            return "setup_failed";
        case ADDITION_SNARK_V1_ERR_PROVE:
            return "prove_failed";
        case ADDITION_SNARK_V1_ERR_VERIFY:
            return "verify_failed";
        case ADDITION_SNARK_V1_ERR_SERIALIZE:
            return "serialize_failed";
        case ADDITION_SNARK_V1_ERR_DISABLED:
            return "disabled_fail_closed";
        default:
            return "unknown";
    }
}

std::string reject_disabled(const char* op, std::string& error) {
    std::ostringstream oss;
    oss << "zk_snark_v1 " << op
        << " rejected: ADDITION_ZK_SNARK_V1 not enabled (fail-closed); "
        << "live privacy_claim remains opening_not_zk; "
        << "zk_circuit_status remains not_proven for production SHA3 statement; "
        << "see docs/ZK_SNARK_V1.md";
    error = oss.str();
    return error;
}

} // namespace

bool zk_snark_v1_flag_enabled() {
    return addition_snark_v1_enabled() != 0;
}

const char* zk_snark_v1_backend_id() {
    return addition_snark_v1_backend_id();
}

const char* zk_snark_v1_system_name() {
    return addition_snark_v1_system_name();
}

const char* zk_snark_v1_circuit_hash_label() {
    return addition_snark_v1_circuit_hash_label();
}

const char* zk_snark_v1_setup_label() {
    return addition_snark_v1_setup_label();
}

const char* zk_snark_v1_status_label() {
    if (zk_snark_v1_flag_enabled()) {
        return "available_poseidon_lab";
    }
    return "disabled";
}

bool zk_snark_v1_setup(ZkSnarkV1Keys& keys_out, std::string& error) {
    keys_out = {};
    if (!zk_snark_v1_flag_enabled()) {
        reject_disabled("setup", error);
        return false;
    }
    size_t pk_len = 0;
    size_t vk_len = 0;
    int rc = addition_snark_v1_setup(nullptr, 0, &pk_len, nullptr, 0, &vk_len);
    if (rc != ADDITION_SNARK_V1_OK || pk_len == 0 || vk_len == 0) {
        error = "zk_snark_v1 setup size query failed: " + err_code_string(rc);
        return false;
    }
    keys_out.proving_key.assign(pk_len, 0);
    keys_out.verifying_key.assign(vk_len, 0);
    rc = addition_snark_v1_setup(keys_out.proving_key.data(), keys_out.proving_key.size(), &pk_len,
                                 keys_out.verifying_key.data(), keys_out.verifying_key.size(),
                                 &vk_len);
    if (rc != ADDITION_SNARK_V1_OK) {
        keys_out = {};
        error = "zk_snark_v1 setup failed: " + err_code_string(rc);
        return false;
    }
    keys_out.proving_key.resize(pk_len);
    keys_out.verifying_key.resize(vk_len);
    error.clear();
    return true;
}

bool zk_snark_v1_prove(const ZkSnarkV1Keys& keys,
                       std::uint64_t amount,
                       const std::uint8_t trapdoor32[32],
                       ZkSnarkV1ProofBundle& proof_out,
                       std::string& error) {
    proof_out = {};
    if (!zk_snark_v1_flag_enabled()) {
        reject_disabled("prove", error);
        return false;
    }
    if (keys.proving_key.empty() || trapdoor32 == nullptr || amount == 0) {
        error = "zk_snark_v1 prove: missing pk/trapdoor or amount==0";
        return false;
    }
    // Groth16 compressed proof on BN254 is small; allocate generously once.
    constexpr size_t kProofCap = 4096;
    proof_out.proof.assign(kProofCap, 0);
    size_t proof_len = 0;
    std::uint8_t cm[32]{};
    std::uint8_t nf[32]{};
    const int rc = addition_snark_v1_prove(keys.proving_key.data(), keys.proving_key.size(), amount,
                                           trapdoor32, proof_out.proof.data(), proof_out.proof.size(),
                                           &proof_len, cm, nf);
    if (rc != ADDITION_SNARK_V1_OK || proof_len == 0 || proof_len > kProofCap) {
        proof_out = {};
        error = "zk_snark_v1 prove failed: " + err_code_string(rc);
        return false;
    }
    proof_out.proof.resize(proof_len);
    proof_out.commitment32.assign(cm, cm + 32);
    proof_out.nullifier32.assign(nf, nf + 32);
    error.clear();
    return true;
}

bool zk_snark_v1_verify(const std::vector<std::uint8_t>& verifying_key,
                        const ZkSnarkV1ProofBundle& proof,
                        std::uint64_t amount,
                        std::string& error) {
    if (!zk_snark_v1_flag_enabled()) {
        reject_disabled("verify", error);
        return false;
    }
    if (verifying_key.empty() || proof.proof.empty() || proof.commitment32.size() != 32 ||
        proof.nullifier32.size() != 32) {
        error = "zk_snark_v1 verify: missing vk/proof/public inputs";
        return false;
    }
    int ok = 0;
    const int rc = addition_snark_v1_verify(verifying_key.data(), verifying_key.size(),
                                            proof.proof.data(), proof.proof.size(), amount,
                                            proof.commitment32.data(), proof.nullifier32.data(),
                                            &ok);
    if (rc == ADDITION_SNARK_V1_ERR_DISABLED) {
        reject_disabled("verify", error);
        return false;
    }
    if (rc != ADDITION_SNARK_V1_OK) {
        error = "zk_snark_v1 verify error: " + err_code_string(rc);
        return false;
    }
    if (ok != 1) {
        error = "zk_snark_v1 verify rejected proof (invalid or wrong public inputs)";
        return false;
    }
    error.clear();
    return true;
}

std::string OptionalZkSnarkV1Verifier::backend_id() const {
    return zk_snark_v1_backend_id();
}

bool OptionalZkSnarkV1Verifier::backend_wired() const {
    return zk_snark_v1_flag_enabled();
}

bool OptionalZkSnarkV1Verifier::verify_opening_proof(const std::vector<std::uint8_t>& verifying_key,
                                                     const ZkSnarkV1ProofBundle& proof,
                                                     std::uint64_t amount,
                                                     std::string& error) const {
    if (!backend_wired()) {
        reject_disabled("optional_verifier", error);
        return false;
    }
    return zk_snark_v1_verify(verifying_key, proof, amount, error);
}

const OptionalZkSnarkV1Verifier& optional_zk_snark_v1_verifier() {
    static const OptionalZkSnarkV1Verifier instance;
    return instance;
}

std::string zk_snark_v1_info_fields() {
    std::ostringstream out;
    out << " zk_snark_id=" << kZkSnarkV1Id
        << " zk_snark_status=" << zk_snark_v1_status_label()
        << " zk_snark_flag=" << (zk_snark_v1_flag_enabled() ? "1" : "0")
        << " zk_snark_system=\"" << zk_snark_v1_system_name() << "\""
        << " zk_snark_hash=\"" << zk_snark_v1_circuit_hash_label() << "\""
        << " zk_snark_setup=\"" << zk_snark_v1_setup_label() << "\""
        << " zk_snark_live_privacy_claim=opening_not_zk"
        << " zk_snark_production_sha3_circuit=not_proven";
    return out.str();
}

} // namespace addition
