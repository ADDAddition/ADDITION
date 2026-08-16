#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace addition {

// ML-DSA-87 HELLO/ACK is ~15 KiB. A BLKDATA line with a signed tx is larger.
// One TCP recv/send of 32 KiB is not enough on a real path.
constexpr std::size_t kMaxLineBytes = 262144;

bool socket_send_all(std::uintptr_t sock, const char* data, std::size_t n);
bool socket_recv_line(std::uintptr_t sock, std::string& line, std::size_t max_bytes);
bool socket_recv_request(std::uintptr_t sock, std::string& req, std::size_t max_bytes);

} // namespace addition
