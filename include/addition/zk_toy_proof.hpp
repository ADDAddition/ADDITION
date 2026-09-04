#pragma once

#include <string>

namespace addition {

// REAL Fiat–Shamir Schnorr proof of knowledge of discrete log over a fixed
// toy safe-prime group (OpenSSL BN). This is cryptographically real for the
// toy statement "I know x such that Y = g^x mod p" — it is NOT the SHA3-512
// opening circuit, NOT PQ, NOT live privacy, and must never flip
// privacy_claim / zk_circuit_v1_proven. See docs/ZK_CIRCUIT_V1.md.

inline constexpr const char* kZkToyProofId = "zk_toy_schnorr_fs_dl_v1";
inline constexpr const char* kZkToyProofDomain = "addition.zk_toy_schnorr|v1";
inline constexpr const char* kZkToyProofHonesty =
    "real_nizk_toy_dl_not_opening_circuit_not_live";

struct ZkToySchnorrPublic {
    std::string Y_hex; // g^x mod p
};

struct ZkToySchnorrWitness {
    std::string x_hex; // secret exponent mod q
};

struct ZkToySchnorrProof {
    std::string R_hex; // commitment g^k
    std::string s_hex; // response
};

// Fixed group parameters (hex, no 0x prefix). Toy size ~256-bit safeprime.
const char* zk_toy_schnorr_p_hex();
const char* zk_toy_schnorr_q_hex();
const char* zk_toy_schnorr_g_hex();

bool zk_toy_schnorr_keygen(ZkToySchnorrPublic& pub_out,
                           ZkToySchnorrWitness& wit_out,
                           std::string& error);

bool zk_toy_schnorr_prove(const ZkToySchnorrPublic& pub,
                          const ZkToySchnorrWitness& wit,
                          ZkToySchnorrProof& proof_out,
                          std::string& error);

// Verifier accepts only well-formed proofs that satisfy the Schnorr equation.
// Tampered / empty / garbage proofs must return false.
bool zk_toy_schnorr_verify(const ZkToySchnorrPublic& pub,
                           const ZkToySchnorrProof& proof,
                           std::string& error);

} // namespace addition
