#ifndef _WIN32

#include "engine/net/socket.h"
#include "engine/core/logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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
    int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
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
    boundPort_ = ntohs(boundAddr.sin_port);
    return true;
}

void NetSocket::close() {
    if (socket_ != INVALID) {
        ::close(static_cast<int>(socket_));
        socket_ = INVALID;
        boundPort_ = 0;
    }
}

int NetSocket::sendTo(const uint8_t* data, size_t size, const NetAddress& to) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);

    ssize_t result = ::sendto(
        static_cast<int>(socket_),
        data, size, 0,
        reinterpret_cast<sockaddr*>(&addr), sizeof(addr)
    );

    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    return static_cast<int>(result);
}

int NetSocket::recvFrom(uint8_t* buffer, size_t bufferSize, NetAddress& from) {
    sockaddr_in addr{};
    socklen_t addrLen = sizeof(addr);

    ssize_t result = ::recvfrom(
        static_cast<int>(socket_),
        buffer, bufferSize, 0,
        reinterpret_cast<sockaddr*>(&addr), &addrLen
    );

    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }

    from.ip = ntohl(addr.sin_addr.s_addr);
    from.port = ntohs(addr.sin_port);
    return static_cast<int>(result);
}

} // namespace fate

#endif // !_WIN32
