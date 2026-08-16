#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace addition {

using Hash512 = std::array<std::uint8_t, 64>;

enum class SigScheme {
    Unknown = 0,
    MlDsa87 = 1,
    SlhDsaShake256s = 2,
};

Hash512 sha3_512_bytes(const std::vector<std::uint8_t>& data);
Hash512 sha3_512_bytes(const std::string& data);
std::string to_hex(const Hash512& hash);
std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);
bool hex_to_bytes(const std::string& hex,
                  std::vector<std::uint8_t>& out,
                  std::string& error);

const char* sig_scheme_id(SigScheme scheme);
const char* sig_scheme_oqs_alg(SigScheme scheme);
bool parse_sig_scheme(const std::string& name, SigScheme& out);
bool sig_scheme_available(SigScheme scheme);
bool sig_scheme_allowed_strict(SigScheme scheme);
std::string allowed_sig_algs_list();
SigScheme infer_sig_scheme_from_pubkey_hex(const std::string& pubkey_hex);

// SHA3-512(scheme_id || 0x00 || pubkey_bytes). Full 128-hex digest (short vs raw ML-DSA-87).
std::string hash_committed_address(SigScheme scheme, const std::vector<std::uint8_t>& pubkey);
std::string hash_committed_address_hex(SigScheme scheme, const std::string& pubkey_hex);
bool address_binds_pubkey(const std::string& address,
                          SigScheme scheme,
                          const std::string& pubkey_hex,
                          std::string& error);

// FIPS 204 context: ADDITION|<mode>|<chain-id>|<genesis-hash>, <=255 bytes, non-empty.
std::string make_consensus_sign_context(const std::string& network_mode,
                                        const std::string& network_id,
                                        const std::string& genesis_hash);
void set_default_sign_context(const std::string& ctx);
std::string default_sign_context();

std::string sign_message_hybrid(const std::string& private_key,
                                const std::string& message,
                                const std::string& ctx = {},
                                const std::string& scheme = "ml-dsa-87");
bool verify_message_signature_hybrid(const std::string& public_key,
                                     const std::string& message,
                                     const std::string& signature,
                                     const std::string& ctx = {},
                                     const std::string& scheme = "ml-dsa-87");

bool pq_sign_message(const std::vector<std::uint8_t>& secret_key,
                     const std::string& message,
                     const std::string& ctx,
                     SigScheme scheme,
                     std::vector<std::uint8_t>& signature,
                     std::string& error);
bool pq_verify_message(const std::vector<std::uint8_t>& public_key,
                       const std::string& message,
                       const std::vector<std::uint8_t>& signature,
                       const std::string& ctx,
                       SigScheme scheme,
                       std::string& error);

bool random_hex(std::size_t nbytes, std::string& out, std::string& error);

// Prints only values this process just computed. No theoretical TPS.
bool crypto_selftest(std::string& report);

} // namespace addition
