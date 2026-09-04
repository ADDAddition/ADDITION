#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace addition {

// Lab Groth16 SNARK (arkworks / BN254) for Poseidon commitment+nullifier opening.
// Schema-analogous to C_cm / C_nf, but the circuit hash is Poseidon — NOT production
// SHA3-512. Live privacy_claim stays opening_not_zk. zk_circuit_v1_proven() stays false
// until a SNARK verifies the production SHA3 privacy statement.
//
// Enable with env ADDITION_ZK_SNARK_V1=1. Default off = fail-closed.
// PR #82 toy Fiat–Shamir Schnorr is NOT this path.

inline constexpr const char* kZkSnarkV1Id = "zk_snark_v1";
inline constexpr const char* kZkSnarkV1EnvFlag = "ADDITION_ZK_SNARK_V1";

struct ZkSnarkV1Keys {
    std::vector<std::uint8_t> proving_key;
    std::vector<std::uint8_t> verifying_key;
};

struct ZkSnarkV1ProofBundle {
    std::vector<std::uint8_t> proof;
    std::vector<std::uint8_t> commitment32; // big-endian Fr
    std::vector<std::uint8_t> nullifier32;  // big-endian Fr
};

bool zk_snark_v1_flag_enabled();
const char* zk_snark_v1_backend_id();
const char* zk_snark_v1_system_name();
const char* zk_snark_v1_circuit_hash_label();
const char* zk_snark_v1_setup_label();
const char* zk_snark_v1_status_label(); // "disabled" | "available_poseidon_lab"

// Trusted setup (circuit-specific Groth16 CRS). Fail-closed unless flag enabled.
bool zk_snark_v1_setup(ZkSnarkV1Keys& keys_out, std::string& error);

// Prove knowledge of trapdoor for Poseidon(cm/nf) opening.
bool zk_snark_v1_prove(const ZkSnarkV1Keys& keys,
                       std::uint64_t amount,
                       const std::uint8_t trapdoor32[32],
                       ZkSnarkV1ProofBundle& proof_out,
                       std::string& error);

// Verify Groth16 proof against public (amount, cm, nf).
bool zk_snark_v1_verify(const std::vector<std::uint8_t>& verifying_key,
                        const ZkSnarkV1ProofBundle& proof,
                        std::uint64_t amount,
                        std::string& error);

// Optional verifier: wired only when ADDITION_ZK_SNARK_V1=1.
// Does NOT become default_privacy_zk_verifier(); does NOT flip live privacy_claim.
class OptionalZkSnarkV1Verifier {
public:
    std::string backend_id() const;
    bool backend_wired() const; // true only when flag enabled
    bool verify_opening_proof(const std::vector<std::uint8_t>& verifying_key,
                              const ZkSnarkV1ProofBundle& proof,
                              std::uint64_t amount,
                              std::string& error) const;
};

const OptionalZkSnarkV1Verifier& optional_zk_snark_v1_verifier();

std::string zk_snark_v1_info_fields();

} // namespace addition
