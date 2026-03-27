#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

namespace fate {

struct NetAddress {
    sockaddr_storage storage{};
#ifdef _WIN32
    int addrLen = sizeof(sockaddr_in);  // Windows uses int for addr lengths
#else
    socklen_t addrLen = sizeof(sockaddr_in);
#endif
    uint16_t port = 0;  // host byte order (convenience copy)

    int family() const { return storage.ss_family; }

    bool operator==(const NetAddress& o) const {
        if (addrLen != o.addrLen) return false;
        return std::memcmp(&storage, &o.storage, addrLen) == 0;
    }
    bool operator!=(const NetAddress& o) const { return !(*this == o); }

    /// Resolve hostname via getaddrinfo (supports IPv4 and IPv6, iOS DNS64/NAT64 compatible)
    static bool resolve(const char* host, uint16_t port, NetAddress& out);

    /// Create from raw IPv4 address (host byte order) — convenience for tests/hardcoded addresses
    static NetAddress makeIPv4(uint32_t ip, uint16_t port) {
        NetAddress addr;
        auto* sin = reinterpret_cast<sockaddr_in*>(&addr.storage);
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = htonl(ip);
        sin->sin_port = htons(port);
        addr.addrLen = sizeof(sockaddr_in);
        addr.port = port;
        return addr;
    }

    /// IP-only string (no port) for per-IP tracking (e.g. "127.0.0.1" or "::1")
    std::string ipString() const;

    /// Human-readable address string for logging (e.g. "127.0.0.1:7777" or "[::1]:7777")
    std::string toString() const;
};

class NetSocket {
public:
    static bool initPlatform();
    static void shutdownPlatform();

    NetSocket() = default;
    ~NetSocket();

    NetSocket(const NetSocket&) = delete;
    NetSocket& operator=(const NetSocket&) = delete;

    bool open(uint16_t port);
    void close();

    int sendTo(const uint8_t* data, size_t size, const NetAddress& to);
    int recvFrom(uint8_t* buffer, size_t bufferSize, NetAddress& from);

    uint16_t port() const { return boundPort_; }
    bool isOpen() const { return socket_ != INVALID; }

    int socketFamily() const { return socketFamily_; }

private:
    static constexpr uintptr_t INVALID = ~uintptr_t(0);
    uintptr_t socket_ = INVALID;
    uint16_t boundPort_ = 0;
    int socketFamily_ = AF_INET;  // tracks whether this socket is IPv4 or IPv6
};

} // namespace fate
