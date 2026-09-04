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
std::uint64_t hash_head64(const std::string& hex_hash);

// Mainnet PoW mix (ADDITION_MAINNET_V1). Consensus: do not change size/rounds/mix.
// Design is SHA3-512 + this scratch (not SHA3-256; testnet PoW stays plain sha3_512).
inline constexpr std::size_t kMemoryHardScratchBytes = 1U << 20; // 1 MiB
inline constexpr std::size_t kMemoryHardRounds = 16;
std::uint64_t memory_hard_head64(const std::string& seed_hex);
std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);
bool hex_to_bytes(const std::string& hex,
				  std::vector<std::uint8_t>& out,
				  std::string& error);

// Address = SHA3-512(scheme_id || 0x00 || pubkey_bytes), full 128-hex digest.
// scheme_id is in the preimage so ML-DSA-87 and SLH-DSA addresses differ.
inline constexpr const char* kMlDsa87SchemeId = "ml-dsa-87";
inline constexpr const char* kSlhDsaShake256sSchemeId = "slh-dsa-shake-256s";
inline constexpr std::size_t kHashCommittedAddressHexLen = 128;

const char* sig_scheme_id(SigScheme scheme);
const char* sig_scheme_oqs_alg(SigScheme scheme);
bool parse_sig_scheme(const std::string& name, SigScheme& out);
bool sig_scheme_available(SigScheme scheme);
bool sig_scheme_allowed_strict(SigScheme scheme);
std::string allowed_sig_algs_list();
SigScheme infer_sig_scheme_from_pubkey_hex(const std::string& pubkey_hex);

std::string hash_committed_address(const std::string& scheme_id,
								   const std::vector<std::uint8_t>& pubkey);
std::string hash_committed_address(SigScheme scheme,
								   const std::vector<std::uint8_t>& pubkey);
std::string hash_committed_address_hex(const std::string& scheme_id,
									   const std::string& pubkey_hex);
std::string hash_committed_address_hex(SigScheme scheme,
									   const std::string& pubkey_hex);
bool address_binds_pubkey(const std::string& address,
						  const std::string& scheme_id,
						  const std::string& pubkey_hex,
						  std::string& error);
bool address_binds_pubkey(const std::string& address,
						  SigScheme scheme,
						  const std::string& pubkey_hex,
						  std::string& error);

// Hybrid signature wrapper (current deterministic base + optional PQ context marker)
std::string sign_message_hybrid(const std::string& private_key,
								const std::string& message,
								const std::string& pq_context = "ml-dsa-87");
bool verify_message_signature_hybrid(const std::string& public_key,
									 const std::string& message,
									 const std::string& signature,
									 const std::string& pq_context = "ml-dsa-87");

bool pq_sign_message(const std::vector<std::uint8_t>& secret_key,
					 const std::string& message,
					 std::vector<std::uint8_t>& signature,
					 std::string& error);
bool pq_verify_message(const std::vector<std::uint8_t>& public_key,
					   const std::string& message,
					   const std::vector<std::uint8_t>& signature,
					   std::string& error);

struct PqVerifyItem {
	std::string public_key_hex;
	std::string message;
	std::string signature;
};

// Parallel ML-DSA-87 verify. Each worker reuses a thread-local OQS_SIG.
// This is ADDITION's own batch path (not a copied runtime).
bool pq_verify_messages_parallel(const std::vector<PqVerifyItem>& items,
								 std::size_t threads,
								 std::size_t& accepted,
								 std::uint64_t& elapsed_ms,
								 std::string& error);

bool random_hex(std::size_t nbytes, std::string& out, std::string& error);

bool crypto_selftest(std::string& report);

} // namespace addition
