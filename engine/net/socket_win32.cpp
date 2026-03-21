#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "engine/net/socket.h"
#include "engine/core/logger.h"

namespace fate {

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
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("Net", "socket() failed: %d", WSAGetLastError());
        return false;
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
    boundPort_ = ntohs(boundAddr.sin_port);
    return true;
}

void NetSocket::close() {
    if (socket_ != INVALID) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = INVALID;
        boundPort_ = 0;
    }
}

int NetSocket::sendTo(const uint8_t* data, size_t size, const NetAddress& to) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);

    int result = ::sendto(
        static_cast<SOCKET>(socket_),
        reinterpret_cast<const char*>(data),
        static_cast<int>(size),
        0,
        reinterpret_cast<sockaddr*>(&addr),
        sizeof(addr)
    );

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    return result;
}

int NetSocket::recvFrom(uint8_t* buffer, size_t bufferSize, NetAddress& from) {
    sockaddr_in addr{};
    int addrLen = sizeof(addr);

    int result = ::recvfrom(
        static_cast<SOCKET>(socket_),
        reinterpret_cast<char*>(buffer),
        static_cast<int>(bufferSize),
        0,
        reinterpret_cast<sockaddr*>(&addr),
        &addrLen
    );

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return 0;
        }
        return -1;
    }

    from.ip = ntohl(addr.sin_addr.s_addr);
    from.port = ntohs(addr.sin_port);
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

    // Prefer IPv4 (server stays IPv4; iOS DNS64/NAT64 provides IPv4-mapped addresses)
    for (auto* rp = result; rp; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            auto* sin = reinterpret_cast<sockaddr_in*>(rp->ai_addr);
            out.ip = ntohl(sin->sin_addr.s_addr);
            out.port = port;
            ::freeaddrinfo(result);
            return true;
        }
    }

    LOG_ERROR("Net", "No IPv4 address found for '%s'", host);
    ::freeaddrinfo(result);
    return false;
}

} // namespace fate
