#include "addition/net_io.hpp"

#include "addition/rpc_access.hpp"

#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace addition {
namespace {

#ifdef _WIN32
using SocketT = SOCKET;
#else
using SocketT = int;
#endif

bool is_http_prefix(const std::string& raw) {
    return is_http_rpc_request(raw);
}

std::size_t http_content_length(const std::string& raw) {
    const auto lower_find = [](const std::string& s, const char* key) -> std::size_t {
        const std::string needle = key;
        for (std::size_t i = 0; i + needle.size() <= s.size(); ++i) {
            bool match = true;
            for (std::size_t j = 0; j < needle.size(); ++j) {
                char a = s[i + j];
                char b = needle[j];
                if (a >= 'A' && a <= 'Z') {
                    a = static_cast<char>(a - 'A' + 'a');
                }
                if (a != b) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return i;
            }
        }
        return std::string::npos;
    };

    const auto pos = lower_find(raw, "content-length:");
    if (pos == std::string::npos) {
        return 0;
    }
    std::size_t i = pos + 15;
    while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) {
        ++i;
    }
    std::size_t n = 0;
    while (i < raw.size() && raw[i] >= '0' && raw[i] <= '9') {
        n = n * 10 + static_cast<std::size_t>(raw[i] - '0');
        ++i;
    }
    return n;
}

bool headers_complete(const std::string& raw, std::size_t& header_end) {
    auto pos = raw.find("\r\n\r\n");
    if (pos != std::string::npos) {
        header_end = pos + 4;
        return true;
    }
    pos = raw.find("\n\n");
    if (pos != std::string::npos) {
        header_end = pos + 2;
        return true;
    }
    return false;
}

int recv_chunk(SocketT sock, char* buf, std::size_t cap) {
#ifdef _WIN32
    return recv(sock, buf, static_cast<int>(cap), 0);
#else
    return static_cast<int>(recv(sock, buf, cap, 0));
#endif
}

} // namespace

void socket_apply_wan_opts(std::uintptr_t sock_raw) {
    const SocketT sock = static_cast<SocketT>(sock_raw);
    int one = 1;
#ifdef _WIN32
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    int buf = static_cast<int>(kMaxLineBytes);
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof(buf));
#else
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef TCP_MAXSEG
    int mss = kWanMss;
    setsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, &mss, sizeof(mss));
#endif
    int buf = static_cast<int>(kMaxLineBytes);
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
#endif
}

bool socket_connect(std::uintptr_t sock_raw, const void* addr, std::size_t addr_len, int timeout_ms) {
    const SocketT sock = static_cast<SocketT>(sock_raw);
    if (addr == nullptr || addr_len == 0) {
        return false;
    }
#ifdef _WIN32
    u_long nonblock = 1;
    if (ioctlsocket(sock, FIONBIO, &nonblock) != 0) {
        return false;
    }
    const int rc = ::connect(sock, static_cast<const sockaddr*>(addr), static_cast<int>(addr_len));
    if (rc == 0) {
        nonblock = 0;
        ioctlsocket(sock, FIONBIO, &nonblock);
        return true;
    }
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
        nonblock = 0;
        ioctlsocket(sock, FIONBIO, &nonblock);
        return false;
    }
    fd_set writers;
    FD_ZERO(&writers);
    FD_SET(sock, &writers);
    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    const int sel = select(0, nullptr, &writers, nullptr, &tv);
    int so_err = 0;
    int so_len = sizeof(so_err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_err), &so_len);
    nonblock = 0;
    ioctlsocket(sock, FIONBIO, &nonblock);
    return sel > 0 && so_err == 0;
#else
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) != 0) {
        return false;
    }
    const int rc = ::connect(sock, static_cast<const sockaddr*>(addr), static_cast<socklen_t>(addr_len));
    if (rc == 0) {
        fcntl(sock, F_SETFL, flags);
        return true;
    }
    if (errno != EINPROGRESS) {
        fcntl(sock, F_SETFL, flags);
        return false;
    }
    pollfd pfd{};
    pfd.fd = sock;
    pfd.events = POLLOUT;
    const int pr = poll(&pfd, 1, timeout_ms);
    int so_err = 0;
    socklen_t so_len = sizeof(so_err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_err, &so_len);
    fcntl(sock, F_SETFL, flags);
    return pr > 0 && so_err == 0;
#endif
}

bool socket_send_all(std::uintptr_t sock_raw, const char* data, std::size_t n) {
    const SocketT sock = static_cast<SocketT>(sock_raw);
    std::size_t sent_total = 0;
    while (sent_total < n) {
#ifdef _WIN32
        const int sent = send(sock, data + sent_total, static_cast<int>(n - sent_total), 0);
#else
        const int sent = static_cast<int>(send(sock, data + sent_total, n - sent_total, MSG_NOSIGNAL));
#endif
        if (sent <= 0) {
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

bool socket_send_paced(std::uintptr_t sock_raw, const char* data, std::size_t n) {
    if (data == nullptr && n != 0) {
        return false;
    }
    std::size_t off = 0;
    while (off < n) {
        const std::size_t chunk = std::min(kWanSendChunk, n - off);
        if (!socket_send_all(sock_raw, data + off, chunk)) {
            return false;
        }
        off += chunk;
        if (off < n) {
#ifdef _WIN32
            Sleep(12);
#else
            usleep(12000);
#endif
        }
    }
    return true;
}

bool socket_recv_line(std::uintptr_t sock_raw, std::string& line, std::size_t max_bytes) {
    line.clear();
    const SocketT sock = static_cast<SocketT>(sock_raw);
    char buf[4096];
    while (line.size() < max_bytes) {
        const int n = recv_chunk(sock, buf, std::min<std::size_t>(sizeof(buf), max_bytes - line.size()));
        if (n <= 0) {
            return !line.empty();
        }
        line.append(buf, buf + n);
        const auto nl = line.find('\n');
        if (nl != std::string::npos) {
            line.resize(nl);
            while (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
    }
    return false;
}

bool socket_recv_request(std::uintptr_t sock_raw, std::string& req, std::size_t max_bytes) {
    req.clear();
    const SocketT sock = static_cast<SocketT>(sock_raw);
    char buf[4096];
    bool saw_http = false;
    std::size_t header_end = 0;
    std::size_t body_needed = 0;

    while (req.size() < max_bytes) {
        if (!saw_http) {
            const auto nl = req.find('\n');
            if (nl != std::string::npos && !is_http_prefix(req)) {
                req.resize(nl);
                while (!req.empty() && req.back() == '\r') {
                    req.pop_back();
                }
                return true;
            }
            if (is_http_prefix(req)) {
                saw_http = true;
            }
        }

        if (saw_http && header_end == 0 && headers_complete(req, header_end)) {
            body_needed = http_content_length(req);
        }
        if (saw_http && header_end != 0 && req.size() >= header_end + body_needed) {
            return true;
        }

        const int n = recv_chunk(sock, buf, std::min<std::size_t>(sizeof(buf), max_bytes - req.size()));
        if (n <= 0) {
            return !req.empty();
        }
        req.append(buf, buf + n);
    }
    return false;
}

bool socket_recv_http_response(std::uintptr_t sock_raw, std::string& raw, std::size_t max_bytes) {
    raw.clear();
    const SocketT sock = static_cast<SocketT>(sock_raw);
    char buf[4096];
    std::size_t header_end = 0;
    std::size_t body_needed = 0;
    while (raw.size() < max_bytes) {
        if (header_end == 0 && headers_complete(raw, header_end)) {
            body_needed = http_content_length(raw);
        }
        if (header_end != 0 && raw.size() >= header_end + body_needed) {
            return true;
        }
        const int n = recv_chunk(sock, buf, std::min<std::size_t>(sizeof(buf), max_bytes - raw.size()));
        if (n <= 0) {
            return !raw.empty();
        }
        raw.append(buf, buf + n);
    }
    return false;
}

} // namespace addition
