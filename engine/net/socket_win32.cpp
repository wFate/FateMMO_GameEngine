#include "engine/net/socket.h"
#pragma comment(lib, "ws2_32.lib")
#include "engine/core/logger.h"
#include <MSWSock.h>  // SIO_UDP_CONNRESET
#include <atomic>

namespace fate {

// Rate-limited log of UDP send-buffer-full drops. Called from every sendto()
// path on WSAEWOULDBLOCK. Aggregates counts across all send sites and prints
// at most once per second so the log stays readable under burst conditions.
static void logSendBufferFull(size_t packetSize) {
    static std::atomic<int>      totalDrops{0};
    static std::atomic<int64_t>  lastLogMs{0};
    int n = totalDrops.fetch_add(1, std::memory_order_relaxed) + 1;
    int64_t nowMs = static_cast<int64_t>(GetTickCount64());
    int64_t last = lastLogMs.load(std::memory_order_relaxed);
    if (nowMs - last >= 1000) {
        if (lastLogMs.compare_exchange_strong(last, nowMs)) {
            LOG_WARN("Net", "UDP sendto WOULDBLOCK (kernel send buffer full) — total drops=%d (latest size=%zu)",
                     n, packetSize);
        }
    }
}

bool NetSocket::initPlatform() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        LOG_ERROR("Net", "WSAStartup failed: %d", result);
        return false;
    }
    return true;
}

void NetSocket::shutdownPlatform() {
    WSACleanup();
}

NetSocket::~NetSocket() {
    close();
}

bool NetSocket::open(uint16_t port) {
    // Try IPv6 dual-stack first (accepts both IPv4 and IPv6 connections)
    SOCKET s = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        // Disable IPv6-only to allow IPv4-mapped addresses (dual-stack)
        int v6only = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6only, sizeof(v6only));

        // Bump UDP socket buffers to 1 MB each. Default Windows UDP receive
        // buffer is 64 KB; with 231 mob-delta updates per server tick plus
        // occasional SvEntityEnter/Leave bursts (leashing, AOE kills), bursts
        // exceed 64 KB and the OS silently drops packets. The client's poll()
        // drains into a 16 KB app buffer per call, but if the kernel buffer
        // overflows before poll runs, packets are already gone. A 1 MB cap
        // covers ~20 tick intervals of worst-case traffic.
        // NOTE: Windows can silently clamp the requested buffer size (group
        // policy, driver limits). We verify with getsockopt and log so that
        // it's obvious when the OS rejected our request.
        int bufSize = 1024 * 1024;
        int actualRcv = 0, actualSnd = 0;
        int optLen = sizeof(actualRcv);
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&bufSize, sizeof(bufSize)) == SOCKET_ERROR) {
            LOG_WARN("Net", "setsockopt(SO_RCVBUF, %d) failed: %d", bufSize, WSAGetLastError());
        }
        if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&bufSize, sizeof(bufSize)) == SOCKET_ERROR) {
            LOG_WARN("Net", "setsockopt(SO_SNDBUF, %d) failed: %d", bufSize, WSAGetLastError());
        }
        getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&actualRcv, &optLen);
        optLen = sizeof(actualSnd);
        getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&actualSnd, &optLen);
        LOG_INFO("Net", "UDP socket buffers: RCVBUF=%d bytes (%d KB), SNDBUF=%d bytes (%d KB)",
                 actualRcv, actualRcv / 1024, actualSnd, actualSnd / 1024);

        // Prevent ICMP Port Unreachable from poisoning the UDP socket.
        // Without this, sending to a closed port causes the NEXT recvfrom()
        // to fail with WSAECONNRESET, blocking reception of real datagrams.
        DWORD bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(s, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior),
                 NULL, 0, &dwBytesReturned, NULL, NULL);

        // Set non-blocking
        u_long nonBlocking = 1;
        if (ioctlsocket(s, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
            LOG_ERROR("Net", "ioctlsocket FIONBIO failed: %d", WSAGetLastError());
            closesocket(s);
            return false;
        }

        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);
        addr.sin6_addr = in6addr_any;

        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR) {
            // Query actual bound port
            sockaddr_in6 boundAddr{};
            int addrLen = sizeof(boundAddr);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen) == SOCKET_ERROR) {
                LOG_ERROR("Net", "getsockname() failed: %d", WSAGetLastError());
                closesocket(s);
                return false;
            }
            socket_ = static_cast<uintptr_t>(s);
            socketFamily_ = AF_INET6;
            boundPort_ = ntohs(boundAddr.sin6_port);
            LOG_INFO("Net", "Opened IPv6 dual-stack socket on port %d", boundPort_);
            return true;
        }

        // IPv6 bind failed, fall through to IPv4
        closesocket(s);
        LOG_WARN("Net", "IPv6 dual-stack bind failed (%d), falling back to IPv4", WSAGetLastError());
    }

    // Fallback: IPv4-only socket
    s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("Net", "socket() failed: %d", WSAGetLastError());
        return false;
    }

    // Prevent ICMP Port Unreachable from poisoning the UDP socket (same as IPv6 path)
    {
        DWORD bNewBehavior = FALSE;
        DWORD dwBytesReturned = 0;
        WSAIoctl(s, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior),
                 NULL, 0, &dwBytesReturned, NULL, NULL);
    }

    // Bump UDP socket buffers (see IPv6 path above for rationale).
    {
        int bufSize = 1024 * 1024;
        int actualRcv = 0, actualSnd = 0;
        int optLen = sizeof(actualRcv);
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&bufSize, sizeof(bufSize)) == SOCKET_ERROR) {
            LOG_WARN("Net", "setsockopt(SO_RCVBUF, %d) failed: %d", bufSize, WSAGetLastError());
        }
        if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&bufSize, sizeof(bufSize)) == SOCKET_ERROR) {
            LOG_WARN("Net", "setsockopt(SO_SNDBUF, %d) failed: %d", bufSize, WSAGetLastError());
        }
        getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&actualRcv, &optLen);
        optLen = sizeof(actualSnd);
        getsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&actualSnd, &optLen);
        LOG_INFO("Net", "UDP socket buffers: RCVBUF=%d bytes (%d KB), SNDBUF=%d bytes (%d KB)",
                 actualRcv, actualRcv / 1024, actualSnd, actualSnd / 1024);
    }

    // Set non-blocking
    u_long nonBlocking = 1;
    if (ioctlsocket(s, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        LOG_ERROR("Net", "ioctlsocket FIONBIO failed: %d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("Net", "bind() failed: %d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    // Query actual bound port
    sockaddr_in boundAddr{};
    int addrLen = sizeof(boundAddr);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen) == SOCKET_ERROR) {
        LOG_ERROR("Net", "getsockname() failed: %d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    socket_ = static_cast<uintptr_t>(s);
    socketFamily_ = AF_INET;
    boundPort_ = ntohs(boundAddr.sin_port);
    LOG_INFO("Net", "Opened IPv4 socket on port %d", boundPort_);
    return true;
}

void NetSocket::close() {
    if (socket_ != INVALID) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = INVALID;
        boundPort_ = 0;
        socketFamily_ = AF_INET;
    }
}

int NetSocket::sendTo(const uint8_t* data, size_t size, const NetAddress& to) {
    // If socket is IPv6 but destination is IPv4, convert to IPv4-mapped IPv6 address
    if (socketFamily_ == AF_INET6 && to.family() == AF_INET) {
        sockaddr_in6 mapped{};
        mapped.sin6_family = AF_INET6;
        mapped.sin6_port = reinterpret_cast<const sockaddr_in*>(&to.storage)->sin_port;
        // Build ::ffff:x.x.x.x mapped address
        std::memset(&mapped.sin6_addr, 0, 10);
        mapped.sin6_addr.s6_addr[10] = 0xFF;
        mapped.sin6_addr.s6_addr[11] = 0xFF;
        std::memcpy(&mapped.sin6_addr.s6_addr[12],
                     &reinterpret_cast<const sockaddr_in*>(&to.storage)->sin_addr, 4);

        int result = ::sendto(
            static_cast<SOCKET>(socket_),
            reinterpret_cast<const char*>(data),
            static_cast<int>(size),
            0,
            reinterpret_cast<const sockaddr*>(&mapped),
            sizeof(mapped)
        );
        if (result == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                // WSAEWOULDBLOCK on UDP sendto = kernel send buffer is FULL.
                // The packet never leaves this machine. Rate-limit log to once
                // per second (~20 ticks worth) so we see the issue without
                // flooding. Counter is aggregated across all send sites.
                logSendBufferFull(size);
                return 0;
            }
            LOG_WARN("Net", "UDP sendto (IPv4-mapped) failed: err=%d size=%zu", err, size);
            return -1;
        }
        return result;
    }

    int result = ::sendto(
        static_cast<SOCKET>(socket_),
        reinterpret_cast<const char*>(data),
        static_cast<int>(size),
        0,
        reinterpret_cast<const sockaddr*>(&to.storage),
        to.addrLen
    );

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            logSendBufferFull(size);
            return 0;
        }
        LOG_WARN("Net", "UDP sendto failed: err=%d size=%zu", err, size);
        return -1;
    }
    return result;
}

int NetSocket::recvFrom(uint8_t* buffer, size_t bufferSize, NetAddress& from) {
    from.addrLen = sizeof(from.storage);

    int result = ::recvfrom(
        static_cast<SOCKET>(socket_),
        reinterpret_cast<char*>(buffer),
        static_cast<int>(bufferSize),
        0,
        reinterpret_cast<sockaddr*>(&from.storage),
        &from.addrLen
    );

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return 0;
        }
        // WSAECONNRESET (10054) on a UDP socket means a previous sendto()
        // targeted a port that is now closed and the OS received an ICMP
        // "Port Unreachable" response.  This is harmless for connectionless
        // UDP — the error relates to a PAST send, not this recv — so treat
        // it as "no data right now" and let the caller retry on the next poll.
        // Without this, the error breaks the recv loop and blocks reception
        // of real datagrams (e.g. new client Connect packets after a logout).
        if (err == WSAECONNRESET) {
            return 0;
        }
        return -1;
    }

    // Extract port and normalize IPv4-mapped IPv6 addresses back to plain IPv4
    if (from.family() == AF_INET6) {
        auto* sin6 = reinterpret_cast<sockaddr_in6*>(&from.storage);
        from.port = ntohs(sin6->sin6_port);
        // Check for IPv4-mapped IPv6 (::ffff:x.x.x.x) and convert to plain IPv4
        // First 10 bytes zero, bytes 10-11 are 0xFF
        const uint8_t* addr = sin6->sin6_addr.s6_addr;
        bool isV4Mapped = true;
        for (int i = 0; i < 10; ++i) { if (addr[i] != 0) { isV4Mapped = false; break; } }
        if (isV4Mapped && addr[10] == 0xFF && addr[11] == 0xFF) {
            uint16_t p = from.port;
            sockaddr_in sin4{};
            sin4.sin_family = AF_INET;
            sin4.sin_port = htons(p);
            std::memcpy(&sin4.sin_addr, &addr[12], 4);
            std::memset(&from.storage, 0, sizeof(from.storage));
            std::memcpy(&from.storage, &sin4, sizeof(sin4));
            from.addrLen = sizeof(sockaddr_in);
            from.port = p;
        }
    } else if (from.family() == AF_INET) {
        from.port = ntohs(reinterpret_cast<sockaddr_in*>(&from.storage)->sin_port);
    }
    return result;
}

bool NetAddress::resolve(const char* host, uint16_t port, NetAddress& out) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;     // Accept IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);

    addrinfo* result = nullptr;
    int ret = ::getaddrinfo(host, portStr, &hints, &result);
    if (ret != 0 || !result) {
        LOG_ERROR("Net", "getaddrinfo failed for '%s': %d", host, ret);
        return false;
    }

    // Use first result (getaddrinfo returns preferred order)
    std::memset(&out.storage, 0, sizeof(out.storage));
    std::memcpy(&out.storage, result->ai_addr, result->ai_addrlen);
    out.addrLen = static_cast<int>(result->ai_addrlen);
    out.port = port;
    ::freeaddrinfo(result);
    return true;
}

std::string NetAddress::ipString() const {
    char ipbuf[INET6_ADDRSTRLEN];
    if (family() == AF_INET6) {
        auto* a = reinterpret_cast<const sockaddr_in6*>(&storage);
        inet_ntop(AF_INET6, &a->sin6_addr, ipbuf, sizeof(ipbuf));
    } else if (family() == AF_INET) {
        auto* a = reinterpret_cast<const sockaddr_in*>(&storage);
        inet_ntop(AF_INET, &a->sin_addr, ipbuf, sizeof(ipbuf));
    } else {
        return "unknown";
    }
    return ipbuf;
}

std::string NetAddress::toString() const {
    char buf[64];
    if (family() == AF_INET6) {
        auto* a = reinterpret_cast<const sockaddr_in6*>(&storage);
        char ipbuf[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &a->sin6_addr, ipbuf, sizeof(ipbuf));
        snprintf(buf, sizeof(buf), "[%s]:%u", ipbuf, port);
    } else if (family() == AF_INET) {
        auto* a = reinterpret_cast<const sockaddr_in*>(&storage);
        char ipbuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &a->sin_addr, ipbuf, sizeof(ipbuf));
        snprintf(buf, sizeof(buf), "%s:%u", ipbuf, port);
    } else {
        snprintf(buf, sizeof(buf), "unknown:%u", port);
    }
    return buf;
}

} // namespace fate
