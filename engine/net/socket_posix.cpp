#ifndef _WIN32

#include "engine/net/socket.h"
#include "engine/core/logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace fate {

bool NetSocket::initPlatform() {
    return true; // No init needed on POSIX
}

void NetSocket::shutdownPlatform() {
    // No cleanup needed on POSIX
}

NetSocket::~NetSocket() {
    close();
}

bool NetSocket::open(uint16_t port) {
    // Try IPv6 dual-stack first (accepts both IPv4 and IPv6 connections)
    int s = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (s >= 0) {
        // Disable IPv6-only to allow IPv4-mapped addresses (dual-stack)
        int v6only = 0;
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));

        // Set non-blocking
        int flags = fcntl(s, F_GETFL, 0);
        if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
            LOG_ERROR("Net", "fcntl O_NONBLOCK failed: %d", errno);
            ::close(s);
            return false;
        }

        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);
        addr.sin6_addr = in6addr_any;

        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) >= 0) {
            sockaddr_in6 boundAddr{};
            socklen_t addrLen = sizeof(boundAddr);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen) < 0) {
                LOG_ERROR("Net", "getsockname() failed: %d", errno);
                ::close(s);
                return false;
            }
            socket_ = static_cast<uintptr_t>(s);
            socketFamily_ = AF_INET6;
            boundPort_ = ntohs(boundAddr.sin6_port);
            LOG_INFO("Net", "Opened IPv6 dual-stack socket on port %d", boundPort_);
            return true;
        }

        // IPv6 bind failed, fall through to IPv4
        ::close(s);
        LOG_WARN("Net", "IPv6 dual-stack bind failed (%d), falling back to IPv4", errno);
    }

    // Fallback: IPv4-only socket
    s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        LOG_ERROR("Net", "socket() failed: %d", errno);
        return false;
    }

    // Set non-blocking
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
        LOG_ERROR("Net", "fcntl O_NONBLOCK failed: %d", errno);
        ::close(s);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("Net", "bind() failed: %d", errno);
        ::close(s);
        return false;
    }

    sockaddr_in boundAddr{};
    socklen_t addrLen = sizeof(boundAddr);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen) < 0) {
        LOG_ERROR("Net", "getsockname() failed: %d", errno);
        ::close(s);
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
        ::close(static_cast<int>(socket_));
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

        ssize_t result = ::sendto(
            static_cast<int>(socket_),
            data, size, 0,
            reinterpret_cast<const sockaddr*>(&mapped), sizeof(mapped)
        );
        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        return static_cast<int>(result);
    }

    ssize_t result = ::sendto(
        static_cast<int>(socket_),
        data, size, 0,
        reinterpret_cast<const sockaddr*>(&to.storage), to.addrLen
    );

    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return static_cast<int>(result);
}

int NetSocket::recvFrom(uint8_t* buffer, size_t bufferSize, NetAddress& from) {
    from.addrLen = sizeof(from.storage);

    ssize_t result = ::recvfrom(
        static_cast<int>(socket_),
        buffer, bufferSize, 0,
        reinterpret_cast<sockaddr*>(&from.storage), &from.addrLen
    );

    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }

    // Extract port and normalize IPv4-mapped IPv6 addresses back to plain IPv4
    if (from.family() == AF_INET6) {
        auto* sin6 = reinterpret_cast<sockaddr_in6*>(&from.storage);
        from.port = ntohs(sin6->sin6_port);
        // Check for IPv4-mapped IPv6 (::ffff:x.x.x.x) and convert to plain IPv4
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
    return static_cast<int>(result);
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
        LOG_ERROR("Net", "getaddrinfo failed for '%s': %s", host, gai_strerror(ret));
        return false;
    }

    // Use first result (getaddrinfo returns preferred order)
    std::memset(&out.storage, 0, sizeof(out.storage));
    std::memcpy(&out.storage, result->ai_addr, result->ai_addrlen);
    out.addrLen = static_cast<socklen_t>(result->ai_addrlen);
    out.port = port;
    ::freeaddrinfo(result);
    return true;
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

#endif // !_WIN32
