# Phase 6A: Transport Layer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a custom reliable UDP transport layer — socket wrapper, packet serialization, reliability with ack bitfields, and connection management with session tokens.

**Architecture:** Platform UDP socket (Winsock2) wrapped in `NetSocket`. `ByteWriter`/`ByteReader` handle binary serialization. `ReliabilityLayer` tracks sequence numbers, ack bitfields, and retransmits unacked reliable packets. `ConnectionManager` handles connect/disconnect handshake, heartbeats, and timeouts.

**Tech Stack:** C++20, Winsock2 (Windows), doctest for tests

**Spec:** `Docs/superpowers/specs/2026-03-17-phase6-networking-design.md` (Transport Layer section)

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_engine
```

---

## File Structure

```
engine/net/
  aoi.h                — (exists) AOI visibility sets
  ghost.h              — (exists) ghost entity flag component
  byte_stream.h        — ByteWriter and ByteReader for binary serialization
  packet.h             — PacketHeader struct, PacketType enum, Channel enum
  socket.h             — NetSocket: non-blocking UDP socket wrapper
  socket_win32.cpp     — Winsock2 implementation
  reliability.h        — ReliabilityLayer: sequence tracking, ack bitfields, retransmit
  reliability.cpp      — Retransmit logic, RTT estimation
  connection.h         — ConnectionManager: connect/disconnect/heartbeat/timeout state machine
  connection.cpp       — Connection lifecycle, session token generation

tests/
  test_byte_stream.cpp — ByteWriter/ByteReader unit tests
  test_reliability.cpp — ReliabilityLayer unit tests
  test_connection.cpp  — ConnectionManager unit tests
```

**Modified files:**
- `CMakeLists.txt` — Link `ws2_32` to `fate_engine` for Winsock2 support

---

### Task 1: ByteWriter and ByteReader

**Files:**
- Create: `engine/net/byte_stream.h`
- Create: `tests/test_byte_stream.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_byte_stream.cpp
#include <doctest/doctest.h>
#include "engine/net/byte_stream.h"

using namespace fate;

TEST_CASE("ByteWriter: write primitives") {
    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));

    w.writeU8(0xAB);
    w.writeU16(0x1234);
    w.writeU32(0xDEADBEEF);
    w.writeFloat(3.14f);

    CHECK(w.size() == 1 + 2 + 4 + 4);
    CHECK(!w.overflowed());
}

TEST_CASE("ByteReader: read primitives") {
    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(42);
    w.writeU16(1000);
    w.writeU32(999999);
    w.writeFloat(2.5f);

    ByteReader r(buf, w.size());
    CHECK(r.readU8() == 42);
    CHECK(r.readU16() == 1000);
    CHECK(r.readU32() == 999999);
    CHECK(r.readFloat() == doctest::Approx(2.5f));
    CHECK(!r.overflowed());
}

TEST_CASE("ByteWriter: overflow detection") {
    uint8_t buf[4];
    ByteWriter w(buf, sizeof(buf));

    w.writeU32(1); // exactly fits
    CHECK(!w.overflowed());

    w.writeU8(1); // overflow
    CHECK(w.overflowed());
}

TEST_CASE("ByteReader: overflow detection") {
    uint8_t buf[2] = {0, 0};
    ByteReader r(buf, 2);

    r.readU16(); // ok
    CHECK(!r.overflowed());

    r.readU8(); // overflow
    CHECK(r.overflowed());
}

TEST_CASE("ByteStream: string round-trip") {
    uint8_t buf[128];
    ByteWriter w(buf, sizeof(buf));
    w.writeString("Hello, MMO!");

    ByteReader r(buf, w.size());
    CHECK(r.readString() == "Hello, MMO!");
}

TEST_CASE("ByteStream: Vec2 round-trip") {
    uint8_t buf[16];
    ByteWriter w(buf, sizeof(buf));
    w.writeVec2({100.5f, -200.25f});

    ByteReader r(buf, w.size());
    auto v = r.readVec2();
    CHECK(v.x == doctest::Approx(100.5f));
    CHECK(v.y == doctest::Approx(-200.25f));
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `byte_stream.h` does not exist.

- [ ] **Step 3: Write byte_stream.h**

```cpp
// engine/net/byte_stream.h
#pragma once
#include "engine/core/types.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace fate {

class ByteWriter {
public:
    ByteWriter(uint8_t* buffer, size_t capacity)
        : buf_(buffer), capacity_(capacity), pos_(0), overflow_(false) {}

    void writeU8(uint8_t v) { write(&v, 1); }
    void writeU16(uint16_t v) { write(&v, 2); }
    void writeU32(uint32_t v) { write(&v, 4); }
    void writeI32(int32_t v) { write(&v, 4); }
    void writeFloat(float v) { write(&v, 4); }

    void writeVec2(const Vec2& v) {
        writeFloat(v.x);
        writeFloat(v.y);
    }

    void writeString(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(s.size());
        writeU16(len);
        write(s.data(), len);
    }

    void writeBytes(const void* data, size_t len) { write(data, len); }

    size_t size() const { return pos_; }
    bool overflowed() const { return overflow_; }
    const uint8_t* data() const { return buf_; }

private:
    void write(const void* data, size_t len) {
        if (pos_ + len > capacity_) { overflow_ = true; return; }
        std::memcpy(buf_ + pos_, data, len);
        pos_ += len;
    }

    uint8_t* buf_;
    size_t capacity_;
    size_t pos_;
    bool overflow_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* buffer, size_t size)
        : buf_(buffer), size_(size), pos_(0), overflow_(false) {}

    uint8_t readU8() { uint8_t v = 0; read(&v, 1); return v; }
    uint16_t readU16() { uint16_t v = 0; read(&v, 2); return v; }
    uint32_t readU32() { uint32_t v = 0; read(&v, 4); return v; }
    int32_t readI32() { int32_t v = 0; read(&v, 4); return v; }
    float readFloat() { float v = 0; read(&v, 4); return v; }

    Vec2 readVec2() {
        float x = readFloat();
        float y = readFloat();
        return {x, y};
    }

    std::string readString() {
        uint16_t len = readU16();
        if (pos_ + len > size_) { overflow_ = true; return ""; }
        std::string s(reinterpret_cast<const char*>(buf_ + pos_), len);
        pos_ += len;
        return s;
    }

    void readBytes(void* out, size_t len) { read(out, len); }

    size_t remaining() const { return overflow_ ? 0 : (size_ - pos_); }
    size_t position() const { return pos_; }
    bool overflowed() const { return overflow_; }

private:
    void read(void* out, size_t len) {
        if (pos_ + len > size_) { overflow_ = true; return; }
        std::memcpy(out, buf_ + pos_, len);
        pos_ += len;
    }

    const uint8_t* buf_;
    size_t size_;
    size_t pos_;
    bool overflow_;
};

} // namespace fate
```

- [ ] **Step 4: Run tests to verify they pass**

Expected: All 6 test cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/net/byte_stream.h tests/test_byte_stream.cpp
git commit -m "feat(net): add ByteWriter and ByteReader for binary packet serialization"
```

---

### Task 2: Packet Header and Types

**Files:**
- Create: `engine/net/packet.h`
- Modify: `tests/test_byte_stream.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_byte_stream.cpp`:

```cpp
#include "engine/net/packet.h"

TEST_CASE("PacketHeader: write and read round-trip") {
    fate::PacketHeader hdr;
    hdr.protocolId = fate::PROTOCOL_ID;
    hdr.sessionToken = 0xCAFEBABE;
    hdr.sequence = 100;
    hdr.ack = 99;
    hdr.ackBits = 0xFFFF;
    hdr.channel = fate::Channel::ReliableOrdered;
    hdr.packetType = 0x01;
    hdr.payloadSize = 42;

    uint8_t buf[64];
    fate::ByteWriter w(buf, sizeof(buf));
    hdr.write(w);

    CHECK(w.size() == 16); // PACKET_HEADER_SIZE

    fate::ByteReader r(buf, w.size());
    auto read = fate::PacketHeader::read(r);

    CHECK(read.protocolId == fate::PROTOCOL_ID);
    CHECK(read.sessionToken == 0xCAFEBABE);
    CHECK(read.sequence == 100);
    CHECK(read.ack == 99);
    CHECK(read.ackBits == 0xFFFF);
    CHECK(read.channel == fate::Channel::ReliableOrdered);
    CHECK(read.packetType == 0x01);
    CHECK(read.payloadSize == 42);
}
```

- [ ] **Step 2: Run test to verify it fails**

Expected: FAIL — `packet.h` does not exist.

- [ ] **Step 3: Write packet.h**

```cpp
// engine/net/packet.h
#pragma once
#include "engine/net/byte_stream.h"
#include <cstdint>

namespace fate {

static constexpr uint16_t PROTOCOL_ID = 0xFA7E; // "FATE"
static constexpr uint16_t PROTOCOL_VERSION = 1;
static constexpr size_t PACKET_HEADER_SIZE = 16; // 2+4+2+2+2+1+1+2 bytes
static constexpr size_t MAX_PACKET_SIZE = 1200;
static constexpr size_t MAX_PAYLOAD_SIZE = MAX_PACKET_SIZE - PACKET_HEADER_SIZE; // 1184 bytes

enum class Channel : uint8_t {
    Unreliable = 0,
    ReliableOrdered = 1,
    ReliableUnordered = 2
};

// Packet types: client→server 0x01-0x7F, server→client 0x80-0xFF
namespace PacketType {
    // System
    constexpr uint8_t Connect       = 0x01;
    constexpr uint8_t Disconnect    = 0x02;
    constexpr uint8_t Heartbeat     = 0x03;
    constexpr uint8_t ConnectAccept = 0x80;
    constexpr uint8_t ConnectReject = 0x81;

    // Client → Server game messages (0x10-0x7F)
    constexpr uint8_t CmdMove       = 0x10;
    constexpr uint8_t CmdAction     = 0x11;
    constexpr uint8_t CmdChat       = 0x12;
    constexpr uint8_t CmdTrade      = 0x13;
    constexpr uint8_t CmdQuestAction = 0x14;

    // Server → Client game messages (0x90-0xFF)
    constexpr uint8_t SvEntityEnter      = 0x90;
    constexpr uint8_t SvEntityLeave      = 0x91;
    constexpr uint8_t SvEntityUpdate     = 0x92;
    constexpr uint8_t SvCombatEvent      = 0x93;
    constexpr uint8_t SvChatMessage      = 0x94;
    constexpr uint8_t SvPlayerState      = 0x95;
    constexpr uint8_t SvMovementCorrection = 0x96;
    constexpr uint8_t SvZoneTransition   = 0x97;
}

struct PacketHeader {
    uint16_t protocolId = PROTOCOL_ID;
    uint32_t sessionToken = 0;
    uint16_t sequence = 0;
    uint16_t ack = 0;
    uint16_t ackBits = 0;
    Channel channel = Channel::Unreliable;
    uint8_t packetType = 0;
    uint16_t payloadSize = 0;

    void write(ByteWriter& w) const {
        w.writeU16(protocolId);
        w.writeU32(sessionToken);
        w.writeU16(sequence);
        w.writeU16(ack);
        w.writeU16(ackBits);
        w.writeU8(static_cast<uint8_t>(channel));
        w.writeU8(packetType);
        w.writeU16(payloadSize);
    }

    static PacketHeader read(ByteReader& r) {
        PacketHeader h;
        h.protocolId = r.readU16();
        h.sessionToken = r.readU32();
        h.sequence = r.readU16();
        h.ack = r.readU16();
        h.ackBits = r.readU16();
        h.channel = static_cast<Channel>(r.readU8());
        h.packetType = r.readU8();
        h.payloadSize = r.readU16();
        return h;
    }
};

// Wrap-safe sequence comparison: returns true if a is more recent than b
inline bool sequenceGreaterThan(uint16_t a, uint16_t b) {
    return static_cast<int16_t>(a - b) > 0;
}

} // namespace fate
```

- [ ] **Step 4: Run test to verify it passes**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/net/packet.h tests/test_byte_stream.cpp
git commit -m "feat(net): add PacketHeader, PacketType enum, and Channel definitions"
```

---

### Task 3: NetSocket — Platform UDP Wrapper

**Files:**
- Create: `engine/net/socket.h`
- Create: `engine/net/socket_win32.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/test_socket.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_socket.cpp
#include <doctest/doctest.h>
#include "engine/net/socket.h"
#include <thread>
#include <chrono>

using namespace fate;

TEST_CASE("NetSocket: create and bind") {
    NetSocket::initPlatform();

    NetSocket sock;
    CHECK(sock.open(0)); // bind to any available port
    CHECK(sock.port() > 0);
    sock.close();

    NetSocket::shutdownPlatform();
}

TEST_CASE("NetSocket: send and receive loopback") {
    NetSocket::initPlatform();

    NetSocket sender, receiver;
    CHECK(sender.open(0));
    CHECK(receiver.open(0));

    uint8_t sendBuf[] = {0xDE, 0xAD, 0xBE, 0xEF};
    NetAddress loopback;
    loopback.ip = 0x7F000001; // 127.0.0.1
    loopback.port = receiver.port();

    int sent = sender.sendTo(sendBuf, 4, loopback);
    CHECK(sent == 4);

    // Brief delay for loopback delivery
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    uint8_t recvBuf[64];
    NetAddress from;
    int received = receiver.recvFrom(recvBuf, sizeof(recvBuf), from);
    CHECK(received == 4);
    CHECK(recvBuf[0] == 0xDE);
    CHECK(recvBuf[3] == 0xEF);
    CHECK(from.ip == 0x7F000001);

    sender.close();
    receiver.close();

    NetSocket::shutdownPlatform();
}
```

- [ ] **Step 2: Write socket.h**

```cpp
// engine/net/socket.h
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
    // Platform init/shutdown (call once per process)
    static bool initPlatform();
    static void shutdownPlatform();

    NetSocket() = default;
    ~NetSocket();

    // Non-copyable, non-movable (socket ownership)
    NetSocket(const NetSocket&) = delete;
    NetSocket& operator=(const NetSocket&) = delete;

    // Open a non-blocking UDP socket and bind to port (0 = any available)
    bool open(uint16_t port);
    void close();

    // Send/receive. Returns bytes sent/received, or -1 on error, 0 if would-block.
    int sendTo(const uint8_t* data, size_t size, const NetAddress& to);
    int recvFrom(uint8_t* buffer, size_t bufferSize, NetAddress& from);

    uint16_t port() const { return boundPort_; }
    bool isOpen() const { return socket_ != INVALID; }

private:
    static constexpr uint64_t INVALID = ~0ULL;
    uint64_t socket_ = INVALID;
    uint16_t boundPort_ = 0;
};

} // namespace fate
```

- [ ] **Step 3: Write socket_win32.cpp**

```cpp
// engine/net/socket_win32.cpp
#ifdef _WIN32

#include "engine/net/socket.h"
#include "engine/core/logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace fate {

bool NetSocket::initPlatform() {
    WSADATA wsa;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (result != 0) {
        LOG_ERROR("NetSocket", "WSAStartup failed: %d", result);
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
    close();

    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("NetSocket", "socket() failed: %d", WSAGetLastError());
        return false;
    }

    // Non-blocking mode
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("NetSocket", "bind() failed: %d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    // Get actual bound port (if port was 0)
    sockaddr_in boundAddr{};
    int addrLen = sizeof(boundAddr);
    getsockname(s, reinterpret_cast<sockaddr*>(&boundAddr), &addrLen);
    boundPort_ = ntohs(boundAddr.sin_port);

    socket_ = static_cast<uint64_t>(s);
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
    addr.sin_port = htons(to.port);
    addr.sin_addr.s_addr = htonl(to.ip);

    int result = ::sendto(static_cast<SOCKET>(socket_),
                          reinterpret_cast<const char*>(data),
                          static_cast<int>(size), 0,
                          reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
        return -1;
    }
    return result;
}

int NetSocket::recvFrom(uint8_t* buffer, size_t bufferSize, NetAddress& from) {
    sockaddr_in addr{};
    int addrLen = sizeof(addr);

    int result = ::recvfrom(static_cast<SOCKET>(socket_),
                            reinterpret_cast<char*>(buffer),
                            static_cast<int>(bufferSize), 0,
                            reinterpret_cast<sockaddr*>(&addr), &addrLen);

    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return 0;
        return -1;
    }

    from.ip = ntohl(addr.sin_addr.s_addr);
    from.port = ntohs(addr.sin_port);
    return result;
}

} // namespace fate

#endif // _WIN32
```

- [ ] **Step 4: Add ws2_32 to CMakeLists.txt**

In `CMakeLists.txt`, after the `target_link_libraries(fate_engine ...)` block, add:

```cmake
if(WIN32)
    target_link_libraries(fate_engine PUBLIC ws2_32)
endif()
```

- [ ] **Step 5: Build and run tests**

Expected: All socket tests PASS (loopback send/receive works).

- [ ] **Step 6: Commit**

```bash
git add engine/net/socket.h engine/net/socket_win32.cpp tests/test_socket.cpp CMakeLists.txt
git commit -m "feat(net): add NetSocket UDP wrapper with Winsock2 backend"
```

---

### Task 4: ReliabilityLayer — Sequence Tracking and Ack Bitfields

**Files:**
- Create: `engine/net/reliability.h`
- Create: `engine/net/reliability.cpp`
- Create: `tests/test_reliability.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_reliability.cpp
#include <doctest/doctest.h>
#include "engine/net/reliability.h"

using namespace fate;

TEST_CASE("ReliabilityLayer: track sent packets") {
    ReliabilityLayer rel;

    uint16_t seq1 = rel.nextLocalSequence();
    uint16_t seq2 = rel.nextLocalSequence();

    CHECK(seq1 == 0);
    CHECK(seq2 == 1);
}

TEST_CASE("ReliabilityLayer: process incoming acks") {
    ReliabilityLayer rel;

    // Send 3 reliable packets
    uint8_t d0[] = {0x01};
    uint8_t d1[] = {0x02};
    uint8_t d2[] = {0x03};
    uint16_t s0 = rel.nextLocalSequence();
    rel.trackReliable(s0, d0, 1);
    uint16_t s1 = rel.nextLocalSequence();
    rel.trackReliable(s1, d1, 1);
    uint16_t s2 = rel.nextLocalSequence();
    rel.trackReliable(s2, d2, 1);

    CHECK(rel.pendingReliableCount() == 3);

    // Ack s2, ack_bits covers s1 and s0
    rel.processAck(s2, 0x0003); // bit 0 = s1, bit 1 = s0

    CHECK(rel.pendingReliableCount() == 0);
}

TEST_CASE("ReliabilityLayer: sequence wrap-safe comparison") {
    CHECK(sequenceGreaterThan(1, 0));
    CHECK(sequenceGreaterThan(0, 65535)); // 0 is more recent than 65535 (wrapped)
    CHECK(!sequenceGreaterThan(65535, 0));
}

TEST_CASE("ReliabilityLayer: build ack fields for received packets") {
    ReliabilityLayer rel;

    rel.onReceive(10);
    rel.onReceive(9);
    rel.onReceive(7);
    // Received: 10, 9, 7 (missing 8)

    uint16_t ack;
    uint16_t ackBits;
    rel.buildAckFields(ack, ackBits);

    CHECK(ack == 10);           // most recent received
    CHECK((ackBits & 0x01) == 1); // bit 0 = seq 9 received
    CHECK((ackBits & 0x02) == 0); // bit 1 = seq 8 NOT received
    CHECK((ackBits & 0x04) == 4); // bit 2 = seq 7 received
}

TEST_CASE("ReliabilityLayer: retransmit list") {
    ReliabilityLayer rel;

    uint8_t data[] = {0x01, 0x02, 0x03};
    uint16_t s0 = rel.nextLocalSequence();
    rel.trackReliable(s0, data, 3);

    // Simulate time passing (200ms+)
    auto needsRetransmit = rel.getRetransmits(0.25f);
    CHECK(needsRetransmit.size() == 1);
    CHECK(needsRetransmit[0].sequence == s0);
    CHECK(needsRetransmit[0].data.size() == 3);
}
```

- [ ] **Step 2: Write reliability.h**

```cpp
// engine/net/reliability.h
#pragma once
#include "engine/net/packet.h"
#include <cstdint>
#include <vector>

namespace fate {

struct PendingPacket {
    uint16_t sequence = 0;
    std::vector<uint8_t> data;      // full packet bytes (header + payload)
    float timeSent = 0.0f;          // elapsed time when sent
    float lastRetransmit = 0.0f;    // elapsed time of last retransmit
    int retransmitCount = 0;
};

class ReliabilityLayer {
public:
    // Local sequence management
    uint16_t nextLocalSequence() { return localSequence_++; }
    uint16_t currentLocalSequence() const { return localSequence_; }

    // Track a reliable packet for retransmission
    void trackReliable(uint16_t sequence, const uint8_t* data, size_t size);

    // Called when we receive a packet — updates remote sequence tracking
    void onReceive(uint16_t remoteSequence);

    // Build ack/ackBits for outgoing packet headers
    void buildAckFields(uint16_t& ack, uint16_t& ackBits) const;

    // Process ack from received packet — removes acked packets from pending, updates RTT
    void processAck(uint16_t ack, uint16_t ackBits, float currentTime = 0.0f);

    // Get packets that need retransmission (sent > retransmitDelay ago)
    // currentTime is in seconds since connection started
    std::vector<PendingPacket> getRetransmits(float currentTime, float retransmitDelay = 0.2f);

    // Update send time on retransmit
    void markRetransmitted(uint16_t sequence, float currentTime);

    size_t pendingReliableCount() const { return pending_.size(); }

    // RTT estimation (exponential moving average)
    float rtt() const { return rtt_; }

private:
    uint16_t localSequence_ = 0;
    uint16_t remoteSequence_ = 0;
    bool receivedAny_ = false;

    // Bitset of received remote sequences (relative to remoteSequence_)
    uint32_t receivedBits_ = 0;

    std::vector<PendingPacket> pending_;

    // RTT tracking
    float rtt_ = 0.1f; // initial estimate: 100ms
};

} // namespace fate
```

- [ ] **Step 3: Write reliability.cpp**

```cpp
// engine/net/reliability.cpp
#include "engine/net/reliability.h"
#include <algorithm>

namespace fate {

void ReliabilityLayer::trackReliable(uint16_t sequence, const uint8_t* data, size_t size) {
    PendingPacket p;
    p.sequence = sequence;
    p.data.assign(data, data + size);
    p.timeSent = 0.0f;
    p.lastRetransmit = 0.0f;
    pending_.push_back(std::move(p));
}

void ReliabilityLayer::onReceive(uint16_t remoteSeq) {
    if (!receivedAny_) {
        remoteSequence_ = remoteSeq;
        receivedAny_ = true;
        receivedBits_ = 0;
        return;
    }

    if (sequenceGreaterThan(remoteSeq, remoteSequence_)) {
        // New most-recent: shift bits
        int shift = static_cast<int16_t>(remoteSeq - remoteSequence_);
        if (shift > 32) {
            receivedBits_ = 0;
        } else {
            receivedBits_ <<= shift;
            receivedBits_ |= (1u << (shift - 1)); // mark old remoteSequence_ as received
        }
        remoteSequence_ = remoteSeq;
    } else {
        // Older packet: set bit
        int offset = static_cast<int16_t>(remoteSequence_ - remoteSeq);
        if (offset > 0 && offset <= 32) {
            receivedBits_ |= (1u << (offset - 1));
        }
    }
}

void ReliabilityLayer::buildAckFields(uint16_t& ack, uint16_t& ackBits) const {
    ack = remoteSequence_;
    ackBits = static_cast<uint16_t>(receivedBits_ & 0xFFFF);
}

void ReliabilityLayer::processAck(uint16_t ack, uint16_t ackBits, float currentTime) {
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(),
            [this, ack, ackBits, currentTime](const PendingPacket& p) {
                bool acked = false;
                if (p.sequence == ack) acked = true;
                else {
                    int offset = static_cast<int16_t>(ack - p.sequence);
                    if (offset > 0 && offset <= 16)
                        acked = (ackBits & (1u << (offset - 1))) != 0;
                }
                if (acked && p.retransmitCount == 0) {
                    // RTT update (only for first-send packets, not retransmits)
                    float sample = currentTime - p.timeSent;
                    rtt_ = rtt_ * 0.875f + sample * 0.125f; // EMA, alpha=0.125
                }
                return acked;
            }),
        pending_.end()
    );
}

std::vector<PendingPacket> ReliabilityLayer::getRetransmits(float currentTime, float retransmitDelay) {
    std::vector<PendingPacket> result;
    for (auto& p : pending_) {
        float elapsed = currentTime - p.lastRetransmit;
        if (elapsed >= retransmitDelay) {
            result.push_back(p);
        }
    }
    return result;
}

void ReliabilityLayer::markRetransmitted(uint16_t sequence, float currentTime) {
    for (auto& p : pending_) {
        if (p.sequence == sequence) {
            p.lastRetransmit = currentTime;
            p.retransmitCount++;
            return;
        }
    }
}

} // namespace fate
```

- [ ] **Step 4: Run tests to verify they pass**

Expected: All 5 test cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/net/reliability.h engine/net/reliability.cpp tests/test_reliability.cpp
git commit -m "feat(net): add ReliabilityLayer with ack bitfields and retransmission"
```

---

### Task 5: ConnectionManager — Handshake, Heartbeat, Timeout

**Files:**
- Create: `engine/net/connection.h`
- Create: `engine/net/connection.cpp`
- Create: `tests/test_connection.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_connection.cpp
#include <doctest/doctest.h>
#include "engine/net/connection.h"

using namespace fate;

TEST_CASE("ConnectionManager: assign client ID") {
    ConnectionManager mgr;

    NetAddress addr1{0x7F000001, 5000};
    NetAddress addr2{0x7F000001, 5001};

    uint16_t id1 = mgr.addClient(addr1);
    uint16_t id2 = mgr.addClient(addr2);

    CHECK(id1 == 1);
    CHECK(id2 == 2);
    CHECK(mgr.clientCount() == 2);
}

TEST_CASE("ConnectionManager: find client by address") {
    ConnectionManager mgr;
    NetAddress addr{0x7F000001, 5000};

    mgr.addClient(addr);
    auto* client = mgr.findByAddress(addr);

    REQUIRE(client != nullptr);
    CHECK(client->address == addr);
    CHECK(client->sessionToken != 0);
}

TEST_CASE("ConnectionManager: timeout detection") {
    ConnectionManager mgr;
    NetAddress addr{0x7F000001, 5000};

    uint16_t id = mgr.addClient(addr);
    auto* client = mgr.findById(id);
    REQUIRE(client != nullptr);

    // Simulate 11 seconds without heartbeat
    client->lastHeartbeat = -11.0f;
    auto timedOut = mgr.getTimedOutClients(0.0f, 10.0f);

    CHECK(timedOut.size() == 1);
    CHECK(timedOut[0] == id);
}

TEST_CASE("ConnectionManager: remove client") {
    ConnectionManager mgr;
    NetAddress addr{0x7F000001, 5000};

    uint16_t id = mgr.addClient(addr);
    CHECK(mgr.clientCount() == 1);

    mgr.removeClient(id);
    CHECK(mgr.clientCount() == 0);
    CHECK(mgr.findById(id) == nullptr);
}

TEST_CASE("ConnectionManager: session token validation") {
    ConnectionManager mgr;
    NetAddress addr{0x7F000001, 5000};

    uint16_t id = mgr.addClient(addr);
    auto* client = mgr.findById(id);
    REQUIRE(client != nullptr);

    uint32_t token = client->sessionToken;
    CHECK(token != 0);
    CHECK(mgr.validateToken(addr, token));
    CHECK(!mgr.validateToken(addr, token + 1));
}
```

- [ ] **Step 2: Write connection.h**

```cpp
// engine/net/connection.h
#pragma once
#include "engine/net/socket.h"
#include "engine/net/reliability.h"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <random>

namespace fate {

struct ClientConnection {
    uint16_t clientId = 0;
    NetAddress address;
    uint32_t sessionToken = 0;
    float lastHeartbeat = 0.0f;
    ReliabilityLayer reliability;
};

class ConnectionManager {
public:
    // Add a new client, returns assigned client ID
    uint16_t addClient(const NetAddress& address);

    // Remove a client by ID
    void removeClient(uint16_t clientId);

    // Lookup
    ClientConnection* findById(uint16_t clientId);
    ClientConnection* findByAddress(const NetAddress& address);

    // Validate session token for a given address
    bool validateToken(const NetAddress& address, uint32_t token);

    // Update heartbeat timestamp for a client
    void heartbeat(uint16_t clientId, float currentTime);

    // Get IDs of clients that have timed out
    std::vector<uint16_t> getTimedOutClients(float currentTime, float timeoutSeconds);

    size_t clientCount() const { return clients_.size(); }

    // Iterate all clients
    template<typename F>
    void forEach(F&& fn) {
        for (auto& [id, client] : clients_) {
            fn(client);
        }
    }

private:
    uint16_t nextClientId_ = 1;
    std::unordered_map<uint16_t, ClientConnection> clients_;
    std::mt19937 rng_{std::random_device{}()};

    uint32_t generateToken();
};

} // namespace fate
```

- [ ] **Step 3: Write connection.cpp**

```cpp
// engine/net/connection.cpp
#include "engine/net/connection.h"

namespace fate {

uint16_t ConnectionManager::addClient(const NetAddress& address) {
    // Find an unused ID (skip 0 and in-use IDs)
    uint16_t id = nextClientId_;
    while (id == 0 || clients_.count(id)) { ++id; }
    nextClientId_ = id + 1;

    ClientConnection conn;
    conn.clientId = id;
    conn.address = address;
    conn.sessionToken = generateToken();
    conn.lastHeartbeat = 0.0f;

    clients_[id] = std::move(conn);
    return id;
}

void ConnectionManager::removeClient(uint16_t clientId) {
    clients_.erase(clientId);
}

ClientConnection* ConnectionManager::findById(uint16_t clientId) {
    auto it = clients_.find(clientId);
    return (it != clients_.end()) ? &it->second : nullptr;
}

ClientConnection* ConnectionManager::findByAddress(const NetAddress& address) {
    for (auto& [id, client] : clients_) {
        if (client.address == address) return &client;
    }
    return nullptr;
}

bool ConnectionManager::validateToken(const NetAddress& address, uint32_t token) {
    auto* client = findByAddress(address);
    return client && client->sessionToken == token;
}

void ConnectionManager::heartbeat(uint16_t clientId, float currentTime) {
    auto* client = findById(clientId);
    if (client) client->lastHeartbeat = currentTime;
}

std::vector<uint16_t> ConnectionManager::getTimedOutClients(float currentTime, float timeoutSeconds) {
    std::vector<uint16_t> result;
    for (auto& [id, client] : clients_) {
        if (currentTime - client.lastHeartbeat > timeoutSeconds) {
            result.push_back(id);
        }
    }
    return result;
}

uint32_t ConnectionManager::generateToken() {
    std::uniform_int_distribution<uint32_t> dist(1, UINT32_MAX);
    return dist(rng_);
}

} // namespace fate
```

- [ ] **Step 4: Run tests to verify they pass**

Expected: All 5 test cases PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/net/connection.h engine/net/connection.cpp tests/test_connection.cpp
git commit -m "feat(net): add ConnectionManager with session tokens, heartbeat, and timeout"
```

---

### Task 6: Integration Test — End-to-End Packet Exchange

**Files:**
- Create: `tests/test_net_integration.cpp`

- [ ] **Step 1: Write the integration test**

This test exercises the full transport stack: two NetSockets communicating via loopback, with PacketHeader serialization, reliability tracking, and connection management.

```cpp
// tests/test_net_integration.cpp
#include <doctest/doctest.h>
#include "engine/net/socket.h"
#include "engine/net/packet.h"
#include "engine/net/byte_stream.h"
#include "engine/net/reliability.h"
#include "engine/net/connection.h"
#include <thread>
#include <chrono>

using namespace fate;

TEST_CASE("Net Integration: reliable packet round-trip via loopback") {
    NetSocket::initPlatform();

    NetSocket serverSock, clientSock;
    REQUIRE(serverSock.open(0));
    REQUIRE(clientSock.open(0));

    NetAddress serverAddr{0x7F000001, serverSock.port()};

    // Client sends a Connect packet
    ReliabilityLayer clientRel;
    uint16_t seq = clientRel.nextLocalSequence();

    uint8_t sendBuf[MAX_PACKET_SIZE];
    ByteWriter w(sendBuf, sizeof(sendBuf));

    PacketHeader hdr;
    hdr.sequence = seq;
    hdr.channel = Channel::ReliableOrdered;
    hdr.packetType = PacketType::Connect;
    hdr.payloadSize = 0;
    hdr.write(w);

    clientSock.sendTo(sendBuf, w.size(), serverAddr);
    clientRel.trackReliable(seq, sendBuf, w.size());

    // Wait for loopback
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Server receives
    uint8_t recvBuf[MAX_PACKET_SIZE];
    NetAddress fromAddr;
    int received = serverSock.recvFrom(recvBuf, sizeof(recvBuf), fromAddr);

    REQUIRE(received > 0);

    ByteReader r(recvBuf, received);
    PacketHeader recvHdr = PacketHeader::read(r);

    CHECK(recvHdr.protocolId == PROTOCOL_ID);
    CHECK(recvHdr.packetType == PacketType::Connect);
    CHECK(recvHdr.channel == Channel::ReliableOrdered);

    // Server tracks the received sequence
    ReliabilityLayer serverRel;
    serverRel.onReceive(recvHdr.sequence);

    // Server sends ConnectAccept with ack
    ConnectionManager mgr;
    uint16_t clientId = mgr.addClient(fromAddr);
    auto* client = mgr.findById(clientId);
    REQUIRE(client != nullptr);

    uint16_t ack;
    uint16_t ackBits;
    serverRel.buildAckFields(ack, ackBits);

    uint8_t respBuf[MAX_PACKET_SIZE];
    ByteWriter rw(respBuf, sizeof(respBuf));

    PacketHeader respHdr;
    respHdr.sessionToken = client->sessionToken;
    respHdr.sequence = serverRel.nextLocalSequence();
    respHdr.ack = ack;
    respHdr.ackBits = ackBits;
    respHdr.channel = Channel::ReliableOrdered;
    respHdr.packetType = PacketType::ConnectAccept;

    // Payload: clientId + sessionToken
    uint8_t payload[6];
    ByteWriter pw(payload, sizeof(payload));
    pw.writeU16(clientId);
    pw.writeU32(client->sessionToken);
    respHdr.payloadSize = static_cast<uint16_t>(pw.size());

    respHdr.write(rw);
    rw.writeBytes(payload, pw.size());

    serverSock.sendTo(respBuf, rw.size(), fromAddr);

    // Wait for loopback
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Client receives ConnectAccept
    received = clientSock.recvFrom(recvBuf, sizeof(recvBuf), fromAddr);
    REQUIRE(received > 0);

    ByteReader cr(recvBuf, received);
    PacketHeader acceptHdr = PacketHeader::read(cr);

    CHECK(acceptHdr.packetType == PacketType::ConnectAccept);

    // Process ack — the original Connect packet should be acked now
    clientRel.processAck(acceptHdr.ack, acceptHdr.ackBits);
    CHECK(clientRel.pendingReliableCount() == 0);

    // Read payload
    uint16_t assignedId = cr.readU16();
    uint32_t token = cr.readU32();
    CHECK(assignedId == clientId);
    CHECK(token == client->sessionToken);

    serverSock.close();
    clientSock.close();
    NetSocket::shutdownPlatform();
}
```

- [ ] **Step 2: Build and run tests**

Expected: PASS — full connect handshake via loopback UDP with reliability tracking.

- [ ] **Step 3: Commit**

```bash
git add tests/test_net_integration.cpp
git commit -m "test(net): add end-to-end transport layer integration test"
```
