#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace addition {

// ML-DSA-87 HELLO/ACK is ~15 KiB. A BLKDATA line with a signed tx is larger.
// One TCP recv/send of 32 KiB is not enough on a real path.
// v3: 4 MiB so a max-weight block (2 MiB binary, ~2.8 MiB base64) fits one line.
// Raised only together with the RpcNetworkServer connection / memory caps below.
constexpr std::size_t kMaxLineBytes = 4u * 1024u * 1024u;
// Lines read by a connection that did not get a "large" slot.
constexpr std::size_t kSmallLineBytes = 262144;
// Kernel socket buffers are per-socket; keep them independent of the line cap.
constexpr int kSocketBufBytes = 512 * 1024;

// Inbound DoS limits (RpcNetworkServer).
constexpr std::size_t kMaxInboundConnections = 128;
constexpr std::size_t kMaxInboundPerIp = 8;
// Loopback carries every Tor hidden-service inbound, so it gets a larger share.
constexpr std::size_t kMaxInboundLoopback = 64;
// Concurrent connections allowed to buffer up to kMaxLineBytes; the rest are
// capped at kSmallLineBytes. Worst-case request memory:
//   16 * 4 MiB + 112 * 256 KiB = 92 MiB.
constexpr std::size_t kMaxLargeSlots = 16;

// Residential WAN: first-write bursts of ~15 KiB often die. Cap MSS and
// write in ~1 KiB paced chunks so a loop-read peer can assemble the line.
constexpr std::size_t kWanSendChunk = 1024;
constexpr int kWanMss = 1200;

void socket_apply_wan_opts(std::uintptr_t sock);
bool socket_connect(std::uintptr_t sock, const void* addr, std::size_t addr_len, int timeout_ms);
bool socket_send_all(std::uintptr_t sock, const char* data, std::size_t n);
bool socket_send_paced(std::uintptr_t sock, const char* data, std::size_t n);
bool socket_recv_line(std::uintptr_t sock, std::string& line, std::size_t max_bytes);
bool socket_recv_request(std::uintptr_t sock, std::string& req, std::size_t max_bytes);
bool socket_recv_http_response(std::uintptr_t sock, std::string& raw, std::size_t max_bytes);
// Line reader for persistent connections: bytes read past the '\n' stay in
// `carry` for the next call instead of being dropped.
bool socket_recv_line_buffered(std::uintptr_t sock, std::string& carry, std::string& line, std::size_t max_bytes);

// Node-state lock released during slow network I/O. RPC, inbound P2P and the
// background sync all hold the same mutex; a thread doing a Tor round trip
// registers its lock here so send_p2p_request() can drop it while waiting.
void set_thread_state_lock(std::unique_lock<std::mutex>* lock);
std::unique_lock<std::mutex>* thread_state_lock();
struct ThreadStateLockScope {
    std::unique_lock<std::mutex>* prev;
    explicit ThreadStateLockScope(std::unique_lock<std::mutex>* l) : prev(thread_state_lock()) { set_thread_state_lock(l); }
    ~ThreadStateLockScope() { set_thread_state_lock(prev); }
    ThreadStateLockScope(const ThreadStateLockScope&) = delete;
    ThreadStateLockScope& operator=(const ThreadStateLockScope&) = delete;
};
// Unlocks the registered state lock (if held) for its lifetime.
struct StateUnlockDuringIo {
    std::unique_lock<std::mutex>* l;
    bool released = false;
    StateUnlockDuringIo() : l(thread_state_lock()) {
        if (l != nullptr && l->owns_lock()) { l->unlock(); released = true; }
    }
    ~StateUnlockDuringIo() { if (released) l->lock(); }
    StateUnlockDuringIo(const StateUnlockDuringIo&) = delete;
    StateUnlockDuringIo& operator=(const StateUnlockDuringIo&) = delete;
};

} // namespace addition
