#pragma once
#include <cstdint>
#include <cstddef>

namespace fate {

struct NetAddress {
    uint32_t ip = 0;      // host byte order
    uint16_t port = 0;    // host byte order
    bool operator==(const NetAddress& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const NetAddress& o) const { return !(*this == o); }
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

private:
    static constexpr uintptr_t INVALID = ~uintptr_t(0);
    uintptr_t socket_ = INVALID;
    uint16_t boundPort_ = 0;
};

} // namespace fate
