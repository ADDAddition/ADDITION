#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace addition {

using Hash512 = std::array<std::uint8_t, 64>;

Hash512 sha3_512_bytes(const std::vector<std::uint8_t>& data);
Hash512 sha3_512_bytes(const std::string& data);
std::string to_hex(const Hash512& hash);
std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes);
bool hex_to_bytes(const std::string& hex,
				  std::vector<std::uint8_t>& out,
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

bool random_hex(std::size_t nbytes, std::string& out, std::string& error);

bool crypto_selftest(std::string& report);

} // namespace addition
