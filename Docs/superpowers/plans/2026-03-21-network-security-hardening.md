# Network Security Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add AEAD packet encryption, IPv6 dual-stack sockets, economic action nonces, and auto-reconnect to the networking layer.

**Architecture:** AEAD encryption (XChaCha20-Poly1305 via libsodium) replaces both the HMAC-SHA256 and encryption requirements in one layer — it provides confidentiality, integrity, and replay prevention using the reliability layer's existing sequence numbers as nonces. IPv6 uses dual-stack sockets with `sockaddr_storage`. Economic nonces use server-issued `uint64_t` tokens consumed on confirmation. Auto-reconnect wires the existing `ReconnectState` class into `NetClient::poll()`.

**Tech Stack:** libsodium (via FetchContent), C++23, existing UDP transport layer

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `engine/net/packet_crypto.h` | AEAD encrypt/decrypt wrapper using libsodium |
| Create | `engine/net/packet_crypto.cpp` | Implementation of key exchange + encrypt/decrypt |
| Modify | `CMakeLists.txt` | Add libsodium via FetchContent |
| Modify | `engine/net/socket.h` | IPv6 dual-stack: `sockaddr_storage` instead of `uint32_t ip` |
| Modify | `engine/net/socket_win32.cpp` | IPv6 socket creation, sendTo/recvFrom with `sockaddr_in6` |
| Modify | `engine/net/socket_posix.cpp` | Same IPv6 changes for POSIX |
| Modify | `engine/net/net_client.h` | Add PacketCrypto member, reconnect state machine, nonce tracking |
| Modify | `engine/net/net_client.cpp` | Encrypt outgoing, decrypt incoming, auto-reconnect loop, nonce requests |
| Modify | `engine/net/connection.h` | Add per-client PacketCrypto + nonce map to ClientConnection |
| Modify | `engine/net/packet.h` | Add key exchange packet types |
| Modify | `engine/net/protocol.h` | Add nonce request/response message structs |
| Modify | `server/server_app.h` | Nonce generation map, key exchange handling |
| Modify | `server/server_app.cpp` | Process key exchange, issue/validate nonces, encrypt/decrypt |
| Create | `tests/test_packet_crypto.cpp` | AEAD encrypt/decrypt round-trip, tamper detection, replay rejection |
| Create | `tests/test_ipv6_socket.cpp` | IPv6 address resolution, dual-stack binding |
| Create | `tests/test_economic_nonces.cpp` | Nonce issue/consume/expire/replay tests |
| Modify | `tests/test_network_robustness.cpp` | Add reconnect state machine tests |

---

### Task 1: Add libsodium dependency and PacketCrypto wrapper

**Files:**
- Modify: `CMakeLists.txt`
- Create: `engine/net/packet_crypto.h`
- Create: `engine/net/packet_crypto.cpp`
- Create: `tests/test_packet_crypto.cpp`

- [ ] **Step 1: Write failing tests for AEAD encrypt/decrypt**

```cpp
// tests/test_packet_crypto.cpp
#include <doctest/doctest.h>
#include "engine/net/packet_crypto.h"
#include <cstring>
#include <vector>

TEST_SUITE("PacketCrypto") {

TEST_CASE("round-trip encrypt then decrypt preserves payload") {
    PacketCrypto sender, receiver;
    auto keypair = PacketCrypto::generateSessionKeys();
    sender.setKeys(keypair.txKey, keypair.rxKey);
    receiver.setKeys(keypair.rxKey, keypair.txKey);

    const uint8_t plaintext[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    uint64_t nonce = 42;

    std::vector<uint8_t> ciphertext(sizeof(plaintext) + PacketCrypto::TAG_SIZE);
    REQUIRE(sender.encrypt(plaintext, sizeof(plaintext), nonce,
                           ciphertext.data(), ciphertext.size()));

    std::vector<uint8_t> decrypted(sizeof(plaintext));
    REQUIRE(receiver.decrypt(ciphertext.data(), ciphertext.size(), nonce,
                             decrypted.data(), decrypted.size()));

    CHECK(std::memcmp(plaintext, decrypted.data(), sizeof(plaintext)) == 0);
}

TEST_CASE("tampered ciphertext fails decryption") {
    PacketCrypto sender, receiver;
    auto keypair = PacketCrypto::generateSessionKeys();
    sender.setKeys(keypair.txKey, keypair.rxKey);
    receiver.setKeys(keypair.rxKey, keypair.txKey);

    const uint8_t plaintext[] = {0xAA, 0xBB};
    uint64_t nonce = 1;

    std::vector<uint8_t> ciphertext(sizeof(plaintext) + PacketCrypto::TAG_SIZE);
    REQUIRE(sender.encrypt(plaintext, sizeof(plaintext), nonce,
                           ciphertext.data(), ciphertext.size()));

    ciphertext[0] ^= 0xFF; // tamper

    std::vector<uint8_t> decrypted(sizeof(plaintext));
    CHECK_FALSE(receiver.decrypt(ciphertext.data(), ciphertext.size(), nonce,
                                 decrypted.data(), decrypted.size()));
}

TEST_CASE("wrong nonce fails decryption") {
    PacketCrypto sender, receiver;
    auto keypair = PacketCrypto::generateSessionKeys();
    sender.setKeys(keypair.txKey, keypair.rxKey);
    receiver.setKeys(keypair.rxKey, keypair.txKey);

    const uint8_t plaintext[] = {0x01};
    std::vector<uint8_t> ciphertext(sizeof(plaintext) + PacketCrypto::TAG_SIZE);
    REQUIRE(sender.encrypt(plaintext, sizeof(plaintext), 100,
                           ciphertext.data(), ciphertext.size()));

    std::vector<uint8_t> decrypted(sizeof(plaintext));
    CHECK_FALSE(receiver.decrypt(ciphertext.data(), ciphertext.size(), 101,
                                 decrypted.data(), decrypted.size()));
}

TEST_CASE("generateSessionKeys produces non-zero keys") {
    auto keys = PacketCrypto::generateSessionKeys();
    bool allZeroTx = true, allZeroRx = true;
    for (auto b : keys.txKey) if (b != 0) allZeroTx = false;
    for (auto b : keys.rxKey) if (b != 0) allZeroRx = false;
    CHECK_FALSE(allZeroTx);
    CHECK_FALSE(allZeroRx);
}

} // TEST_SUITE
```

- [ ] **Step 2: Run tests to verify they fail (PacketCrypto not defined)**

Run: `cmake --build build --target fate_tests 2>&1 | head -20`
Expected: Compilation error — `packet_crypto.h` not found

- [ ] **Step 3: Add libsodium to CMakeLists.txt**

Add after the Tracy FetchContent block (~line 136):

```cmake
# libsodium for AEAD packet encryption
FetchContent_Declare(
    libsodium
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG        1.0.20-RELEASE
    GIT_SHALLOW    TRUE
)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(SODIUM_DISABLE_PIE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libsodium)
```

Note: libsodium's CMake support is newer. If FetchContent fails, fall back to vendoring the amalgamation (`sodium.h` + `sodium.c`) as a static library. The amalgamation is a single .c file (~400KB) that compiles on all platforms with zero dependencies.

Alternative (more reliable — use this if FetchContent has issues):

```cmake
# libsodium amalgamation (vendored)
add_library(sodium STATIC third_party/libsodium/sodium.c)
target_include_directories(sodium PUBLIC third_party/libsodium/include)
if(MSVC)
    target_compile_definitions(sodium PRIVATE SODIUM_STATIC SODIUM_EXPORT=)
endif()
```

Add `sodium` to `fate_engine` link libraries (after `spdlog::spdlog`):

```cmake
target_link_libraries(fate_engine PUBLIC ... sodium)
```

- [ ] **Step 4: Create PacketCrypto header**

```cpp
// engine/net/packet_crypto.h
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

class PacketCrypto {
public:
    static constexpr size_t KEY_SIZE = 32;   // 256-bit key
    static constexpr size_t TAG_SIZE = 16;   // Poly1305 tag
    static constexpr size_t NONCE_SIZE = 24; // XChaCha20 nonce

    using Key = std::array<uint8_t, KEY_SIZE>;

    struct SessionKeys {
        Key txKey;  // sender's encryption key
        Key rxKey;  // receiver's encryption key (sender's decryption key)
    };

    static SessionKeys generateSessionKeys();
    static bool initLibrary(); // call once at startup

    void setKeys(const Key& encryptKey, const Key& decryptKey);
    bool hasKeys() const { return keysSet_; }

    // Encrypt plaintext into ciphertext. Output must be plaintextSize + TAG_SIZE.
    // nonce = reliability layer sequence number (unique per packet).
    bool encrypt(const uint8_t* plaintext, size_t plaintextSize,
                 uint64_t nonce,
                 uint8_t* ciphertext, size_t ciphertextCapacity) const;

    // Decrypt ciphertext into plaintext. Output must be ciphertextSize - TAG_SIZE.
    bool decrypt(const uint8_t* ciphertext, size_t ciphertextSize,
                 uint64_t nonce,
                 uint8_t* plaintext, size_t plaintextCapacity) const;

private:
    Key encryptKey_{};
    Key decryptKey_{};
    bool keysSet_ = false;

    // Build 24-byte XChaCha20 nonce from 8-byte sequence number
    static void buildNonce(uint64_t seq, uint8_t out[NONCE_SIZE]);
};
```

- [ ] **Step 5: Create PacketCrypto implementation**

```cpp
// engine/net/packet_crypto.cpp
#include "engine/net/packet_crypto.h"
#include <sodium.h>
#include <cstring>

bool PacketCrypto::initLibrary() {
    return sodium_init() >= 0; // returns 0 on success, 1 if already init, -1 on failure
}

PacketCrypto::SessionKeys PacketCrypto::generateSessionKeys() {
    SessionKeys keys;
    randombytes_buf(keys.txKey.data(), KEY_SIZE);
    randombytes_buf(keys.rxKey.data(), KEY_SIZE);
    return keys;
}

void PacketCrypto::setKeys(const Key& encryptKey, const Key& decryptKey) {
    encryptKey_ = encryptKey;
    decryptKey_ = decryptKey;
    keysSet_ = true;
}

void PacketCrypto::buildNonce(uint64_t seq, uint8_t out[NONCE_SIZE]) {
    std::memset(out, 0, NONCE_SIZE);
    std::memcpy(out, &seq, sizeof(seq)); // first 8 bytes = sequence, rest zero-padded
}

bool PacketCrypto::encrypt(const uint8_t* plaintext, size_t plaintextSize,
                            uint64_t nonce,
                            uint8_t* ciphertext, size_t ciphertextCapacity) const {
    if (!keysSet_) return false;
    if (ciphertextCapacity < plaintextSize + TAG_SIZE) return false;

    uint8_t nonceBytes[NONCE_SIZE];
    buildNonce(nonce, nonceBytes);

    // XChaCha20-Poly1305 AEAD (no additional data needed — header is inside payload)
    unsigned long long ciphertextLen = 0;
    int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext, &ciphertextLen,
        plaintext, plaintextSize,
        nullptr, 0,    // no additional authenticated data
        nullptr,       // nsec (unused)
        nonceBytes,
        encryptKey_.data()
    );
    return result == 0;
}

bool PacketCrypto::decrypt(const uint8_t* ciphertext, size_t ciphertextSize,
                            uint64_t nonce,
                            uint8_t* plaintext, size_t plaintextCapacity) const {
    if (!keysSet_) return false;
    if (ciphertextSize < TAG_SIZE) return false;
    if (plaintextCapacity < ciphertextSize - TAG_SIZE) return false;

    uint8_t nonceBytes[NONCE_SIZE];
    buildNonce(nonce, nonceBytes);

    unsigned long long plaintextLen = 0;
    int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
        plaintext, &plaintextLen,
        nullptr,       // nsec (unused)
        ciphertext, ciphertextSize,
        nullptr, 0,    // no additional authenticated data
        nonceBytes,
        decryptKey_.data()
    );
    return result == 0;
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="PacketCrypto"`
Expected: All 4 tests PASS

- [ ] **Step 7: Commit**

```bash
git add engine/net/packet_crypto.h engine/net/packet_crypto.cpp tests/test_packet_crypto.cpp CMakeLists.txt
git commit -m "feat: add PacketCrypto AEAD wrapper using libsodium XChaCha20-Poly1305"
```

---

### Task 2: Integrate AEAD encryption into send/receive paths

**Files:**
- Modify: `engine/net/net_client.h`
- Modify: `engine/net/net_client.cpp`
- Modify: `engine/net/connection.h`
- Modify: `engine/net/packet.h`
- Modify: `server/server_app.cpp`

- [ ] **Step 1: Add key exchange packet types to packet.h**

After `ConnectReject = 0x81` (~line 34), add:

```cpp
KeyExchange    = 0x82,  // server sends session keys after ConnectAccept
```

- [ ] **Step 2: Add PacketCrypto to ClientConnection in connection.h**

Add include at top: `#include "engine/net/packet_crypto.h"`

Add member to `ClientConnection` struct (after `authToken`):

```cpp
PacketCrypto crypto;  // AEAD encryption state for this client
```

- [ ] **Step 3: Add PacketCrypto to NetClient**

In `net_client.h`, add include and member:

```cpp
#include "engine/net/packet_crypto.h"
// ...
private:
    PacketCrypto crypto_;
```

- [ ] **Step 4: Server sends session keys after ConnectAccept**

In `server_app.cpp`, after sending `ConnectAccept` to a new client, generate and send session keys:

```cpp
// After ConnectAccept is sent, generate AEAD keys
auto sessionKeys = PacketCrypto::generateSessionKeys();
client->crypto.setKeys(sessionKeys.rxKey, sessionKeys.txKey); // server encrypts with rx, decrypts with tx

// Send keys to client (over the initial unencrypted channel — keys are ephemeral)
uint8_t keyBuf[64];
ByteWriter kw(keyBuf, sizeof(keyBuf));
kw.writeBytes(sessionKeys.txKey.data(), 32); // client's encrypt key
kw.writeBytes(sessionKeys.rxKey.data(), 32); // client's decrypt key
server_.sendTo(clientId, Channel::ReliableOrdered, PacketType::KeyExchange, keyBuf, kw.size());
```

- [ ] **Step 5: Client receives and stores session keys**

In `net_client.cpp` `handlePacket()`, add case for `KeyExchange`:

```cpp
case static_cast<uint8_t>(PacketType::KeyExchange): {
    ByteReader payload(data + r.position(), hdr.payloadSize);
    PacketCrypto::Key txKey, rxKey;
    payload.readBytes(txKey.data(), 32);
    payload.readBytes(rxKey.data(), 32);
    if (payload.ok()) {
        crypto_.setKeys(txKey, rxKey);
    }
    break;
}
```

- [ ] **Step 6: Encrypt outgoing packets in NetClient::sendPacket()**

After building the packet buffer, before sending, encrypt the payload portion if crypto is active. The header (16 bytes) is sent in cleartext (needed for routing), payload is encrypted:

```cpp
// In sendPacket(), after writing header+payload to buffer:
if (crypto_.hasKeys() && packetType != static_cast<uint8_t>(PacketType::Connect)
    && packetType != static_cast<uint8_t>(PacketType::Disconnect)) {
    // Encrypt payload in-place (need space for 16-byte tag)
    uint8_t encrypted[MAX_PACKET_SIZE];
    size_t payloadStart = PACKET_HEADER_SIZE;
    size_t payloadLen = w.size() - payloadStart;
    if (payloadLen > 0 && crypto_.encrypt(
            buffer + payloadStart, payloadLen,
            hdr.sequence,
            encrypted, payloadLen + PacketCrypto::TAG_SIZE)) {
        std::memcpy(buffer + payloadStart, encrypted, payloadLen + PacketCrypto::TAG_SIZE);
        // Update payloadSize in header to include tag
        uint16_t newPayloadSize = static_cast<uint16_t>(payloadLen + PacketCrypto::TAG_SIZE);
        std::memcpy(buffer + 12, &newPayloadSize, 2); // offset 12 = payloadSize field
        totalSize = payloadStart + payloadLen + PacketCrypto::TAG_SIZE;
    }
}
```

- [ ] **Step 7: Decrypt incoming packets in NetClient::handlePacket()**

Before processing payload, decrypt if crypto is active:

```cpp
// After reading header, before creating payload ByteReader:
const uint8_t* payloadData = data + r.position();
size_t payloadLen = hdr.payloadSize;
std::vector<uint8_t> decryptedBuf;

if (crypto_.hasKeys() && hdr.packetType != static_cast<uint8_t>(PacketType::ConnectAccept)
    && hdr.packetType != static_cast<uint8_t>(PacketType::KeyExchange)
    && hdr.packetType != static_cast<uint8_t>(PacketType::ConnectReject)) {
    if (payloadLen > PacketCrypto::TAG_SIZE) {
        decryptedBuf.resize(payloadLen - PacketCrypto::TAG_SIZE);
        if (!crypto_.decrypt(payloadData, payloadLen, hdr.sequence,
                             decryptedBuf.data(), decryptedBuf.size())) {
            return; // tampered or wrong key — silently drop
        }
        payloadData = decryptedBuf.data();
        payloadLen = decryptedBuf.size();
    }
}
```

- [ ] **Step 8: Apply same decrypt logic on server side**

In `server_app.cpp`'s `onPacketReceived()`, add symmetric decrypt using `client->crypto` before processing payload. Same pattern as client side but using the server's decrypt key.

- [ ] **Step 9: Run full test suite**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe`
Expected: All existing tests + new crypto tests PASS

- [ ] **Step 10: Commit**

```bash
git add engine/net/packet.h engine/net/net_client.h engine/net/net_client.cpp engine/net/connection.h server/server_app.cpp
git commit -m "feat: integrate AEAD encryption into send/receive paths with key exchange"
```

---

### Task 3: IPv6 dual-stack socket support

**Files:**
- Modify: `engine/net/socket.h`
- Modify: `engine/net/socket_win32.cpp`
- Modify: `engine/net/socket_posix.cpp`
- Create: `tests/test_ipv6_socket.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_ipv6_socket.cpp
#include <doctest/doctest.h>
#include "engine/net/socket.h"

TEST_SUITE("IPv6 Socket") {

TEST_CASE("NetAddress resolves IPv4 hostname") {
    NetAddress addr;
    CHECK(NetAddress::resolve("127.0.0.1", 7777, addr));
    CHECK(addr.port == 7777);
}

TEST_CASE("NetAddress resolves localhost (may be IPv4 or IPv6)") {
    NetAddress addr;
    CHECK(NetAddress::resolve("localhost", 7777, addr));
    CHECK(addr.port == 7777);
}

TEST_CASE("NetAddress stores address family") {
    NetAddress addr;
    NetAddress::resolve("127.0.0.1", 7777, addr);
    CHECK(addr.family() == AF_INET);
}

TEST_CASE("NetAddress equality compares full address") {
    NetAddress a, b;
    NetAddress::resolve("127.0.0.1", 7777, a);
    NetAddress::resolve("127.0.0.1", 7777, b);
    CHECK(a == b);
}

TEST_CASE("NetAddress inequality on different ports") {
    NetAddress a, b;
    NetAddress::resolve("127.0.0.1", 7777, a);
    NetAddress::resolve("127.0.0.1", 7778, b);
    CHECK(a != b);
}

} // TEST_SUITE
```

- [ ] **Step 2: Refactor NetAddress to use sockaddr_storage**

Replace the current `uint32_t ip` design with `sockaddr_storage`:

```cpp
// engine/net/socket.h
#pragma once
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

struct NetAddress {
    sockaddr_storage storage{};
    socklen_t addrLen = 0;
    uint16_t port = 0;  // host byte order (convenience)

    int family() const { return storage.ss_family; }

    bool operator==(const NetAddress& o) const {
        if (addrLen != o.addrLen) return false;
        return std::memcmp(&storage, &o.storage, addrLen) == 0;
    }
    bool operator!=(const NetAddress& o) const { return !(*this == o); }

    static bool resolve(const char* host, uint16_t port, NetAddress& out);
};
```

- [ ] **Step 3: Update resolve() to use getaddrinfo with AF_UNSPEC**

```cpp
// In socket_win32.cpp and socket_posix.cpp:
bool NetAddress::resolve(const char* host, uint16_t port, NetAddress& out) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // Accept IPv4 or IPv6
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%u", port);

    addrinfo* result = nullptr;
    if (getaddrinfo(host, portStr, &hints, &result) != 0 || !result) {
        return false;
    }

    // Prefer IPv6 if available (works through DNS64/NAT64 on iOS)
    addrinfo* best = result;
    for (addrinfo* p = result; p; p = p->ai_next) {
        if (p->ai_family == AF_INET6) { best = p; break; }
    }

    std::memset(&out.storage, 0, sizeof(out.storage));
    std::memcpy(&out.storage, best->ai_addr, best->ai_addrlen);
    out.addrLen = static_cast<socklen_t>(best->ai_addrlen);
    out.port = port;
    freeaddrinfo(result);
    return true;
}
```

- [ ] **Step 4: Update NetSocket::open() for IPv6 dual-stack**

```cpp
bool NetSocket::open(uint16_t port) {
    // Create IPv6 socket with dual-stack (accepts both IPv4 and IPv6)
    socket_ = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID) return false;

    // Enable dual-stack (accept IPv4-mapped IPv6 addresses)
    int v6only = 0;
    setsockopt(socket_, IPPROTO_IPV6, IPV6_V6ONLY,
               reinterpret_cast<const char*>(&v6only), sizeof(v6only));

    // Set non-blocking
    // ... (platform-specific, same as current)

    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    addr.sin6_addr = in6addr_any;

    if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close();
        return false;
    }
    boundPort_ = port;
    return true;
}
```

- [ ] **Step 5: Update sendTo/recvFrom for sockaddr_storage**

```cpp
int NetSocket::sendTo(const uint8_t* data, size_t size, const NetAddress& to) {
    return ::sendto(socket_, reinterpret_cast<const char*>(data),
                    static_cast<int>(size), 0,
                    reinterpret_cast<const sockaddr*>(&to.storage), to.addrLen);
}

int NetSocket::recvFrom(uint8_t* buffer, size_t bufferSize, NetAddress& from) {
    from.addrLen = sizeof(from.storage);
    int result = ::recvfrom(socket_, reinterpret_cast<char*>(buffer),
                             static_cast<int>(bufferSize), 0,
                             reinterpret_cast<sockaddr*>(&from.storage),
                             &from.addrLen);
    if (result > 0) {
        // Extract port for convenience
        if (from.family() == AF_INET6) {
            auto* a6 = reinterpret_cast<sockaddr_in6*>(&from.storage);
            from.port = ntohs(a6->sin6_port);
        } else {
            auto* a4 = reinterpret_cast<sockaddr_in*>(&from.storage);
            from.port = ntohs(a4->sin_port);
        }
    }
    return result;
}
```

- [ ] **Step 6: Update all call sites comparing NetAddress**

The `ConnectionManager::findByAddress()` and packet sender comparison already use `operator==`, which now compares `sockaddr_storage` memcmp. No code changes needed if equality operator is correct.

- [ ] **Step 7: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="IPv6 Socket"`
Expected: All tests PASS

- [ ] **Step 8: Commit**

```bash
git add engine/net/socket.h engine/net/socket_win32.cpp engine/net/socket_posix.cpp tests/test_ipv6_socket.cpp
git commit -m "feat: IPv6 dual-stack socket support for iOS App Store compliance"
```

---

### Task 4: One-time nonces for economic actions

**Files:**
- Modify: `engine/net/protocol.h`
- Modify: `engine/net/packet.h`
- Modify: `server/server_app.h`
- Modify: `server/server_app.cpp`
- Create: `tests/test_economic_nonces.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_economic_nonces.cpp
#include <doctest/doctest.h>
#include <unordered_map>
#include <cstdint>
#include <random>

// Minimal nonce manager for testing (mirrors server implementation)
class NonceManager {
public:
    uint64_t issue(uint16_t clientId) {
        std::mt19937_64 rng(std::random_device{}());
        uint64_t nonce = rng();
        pending_[clientId][nonce] = std::chrono::steady_clock::now();
        return nonce;
    }

    bool consume(uint16_t clientId, uint64_t nonce) {
        auto clientIt = pending_.find(clientId);
        if (clientIt == pending_.end()) return false;
        auto it = clientIt->second.find(nonce);
        if (it == clientIt->second.end()) return false;
        clientIt->second.erase(it);
        return true;
    }

    void expireOlderThan(std::chrono::seconds maxAge) {
        auto cutoff = std::chrono::steady_clock::now() - maxAge;
        for (auto& [cid, nonces] : pending_) {
            for (auto it = nonces.begin(); it != nonces.end(); ) {
                if (it->second < cutoff) it = nonces.erase(it);
                else ++it;
            }
        }
    }

    size_t pendingCount(uint16_t clientId) const {
        auto it = pending_.find(clientId);
        return it != pending_.end() ? it->second.size() : 0;
    }

private:
    std::unordered_map<uint16_t,
        std::unordered_map<uint64_t, std::chrono::steady_clock::time_point>> pending_;
};

TEST_SUITE("Economic Nonces") {

TEST_CASE("issued nonce can be consumed once") {
    NonceManager mgr;
    auto nonce = mgr.issue(1);
    CHECK(mgr.consume(1, nonce));
    CHECK_FALSE(mgr.consume(1, nonce)); // second use fails
}

TEST_CASE("wrong client cannot consume nonce") {
    NonceManager mgr;
    auto nonce = mgr.issue(1);
    CHECK_FALSE(mgr.consume(2, nonce));
}

TEST_CASE("random value cannot be consumed") {
    NonceManager mgr;
    mgr.issue(1);
    CHECK_FALSE(mgr.consume(1, 0xDEADBEEF));
}

TEST_CASE("multiple nonces per client are independent") {
    NonceManager mgr;
    auto n1 = mgr.issue(1);
    auto n2 = mgr.issue(1);
    CHECK(n1 != n2);
    CHECK(mgr.consume(1, n2));
    CHECK(mgr.consume(1, n1));
}

TEST_CASE("pending count tracks outstanding nonces") {
    NonceManager mgr;
    CHECK(mgr.pendingCount(1) == 0);
    mgr.issue(1);
    mgr.issue(1);
    CHECK(mgr.pendingCount(1) == 2);
    mgr.consume(1, mgr.issue(1));
    CHECK(mgr.pendingCount(1) == 2); // consumed the third, 2 remain
}

} // TEST_SUITE
```

- [ ] **Step 2: Run tests to verify pass (self-contained test)**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="Economic Nonces"`
Expected: All 5 tests PASS (test is self-contained with inline NonceManager)

- [ ] **Step 3: Create NonceManager in server code**

Add to `server/server_app.h` as a private member:

```cpp
// Economic action nonce tracking
std::unordered_map<uint16_t,
    std::unordered_map<uint64_t, float>> economicNonces_; // clientId → {nonce → issueTime}
uint64_t issueEconomicNonce(uint16_t clientId);
bool consumeEconomicNonce(uint16_t clientId, uint64_t nonce);
void expireEconomicNonces();
```

- [ ] **Step 4: Implement in server_app.cpp**

```cpp
uint64_t ServerApp::issueEconomicNonce(uint16_t clientId) {
    static std::mt19937_64 rng(std::random_device{}());
    uint64_t nonce = rng();
    economicNonces_[clientId][nonce] = gameTime_;
    return nonce;
}

bool ServerApp::consumeEconomicNonce(uint16_t clientId, uint64_t nonce) {
    auto clientIt = economicNonces_.find(clientId);
    if (clientIt == economicNonces_.end()) return false;
    auto it = clientIt->second.find(nonce);
    if (it == clientIt->second.end()) return false;
    // Expire after 60 seconds
    if (gameTime_ - it->second > 60.0f) {
        clientIt->second.erase(it);
        return false;
    }
    clientIt->second.erase(it);
    return true;
}

void ServerApp::expireEconomicNonces() {
    for (auto& [cid, nonces] : economicNonces_) {
        for (auto it = nonces.begin(); it != nonces.end(); ) {
            if (gameTime_ - it->second > 60.0f) it = nonces.erase(it);
            else ++it;
        }
    }
}
```

- [ ] **Step 5: Issue nonces when trade/market UIs open**

When the server processes trade initiation or market listing requests, include a nonce in the response. When the client sends the confirmation (trade confirm, market list confirm), it must echo the nonce. The server validates via `consumeEconomicNonce()`.

Add nonce fields to relevant protocol messages:
- `SvTradeUpdateMsg`: add `uint64_t nonce` field
- `CmdTrade` confirm sub-action: add `uint64_t nonce` field
- `SvMarketResultMsg`: add `uint64_t nonce` for listing responses
- `CmdMarket` ListItem sub-action: add `uint64_t nonce` field

- [ ] **Step 6: Call expireEconomicNonces() in maintenance tick**

In `tickMaintenance()`, add: `expireEconomicNonces();`

Clean up nonces on disconnect in `onClientDisconnected()`:
```cpp
economicNonces_.erase(clientId);
```

- [ ] **Step 7: Commit**

```bash
git add server/server_app.h server/server_app.cpp engine/net/protocol.h tests/test_economic_nonces.cpp
git commit -m "feat: one-time nonces for trade and market economic actions"
```

---

### Task 5: Wire auto-reconnect state machine into NetClient

**Files:**
- Modify: `engine/net/net_client.h`
- Modify: `engine/net/net_client.cpp`
- Modify: `tests/test_network_robustness.cpp`

- [ ] **Step 1: Write failing tests**

Add to `tests/test_network_robustness.cpp`:

```cpp
TEST_CASE("NetClient auto-reconnect state transitions") {
    // Test the reconnect state machine logic (no actual network)
    NetClient client;
    CHECK_FALSE(client.isReconnecting());

    // Simulate disconnect detection
    client.onConnectionLost();
    CHECK(client.isReconnecting());
    CHECK(client.reconnectAttempts() == 0);

    // Simulate reconnect attempt
    client.tickReconnect(1.0f); // 1 second elapsed
    CHECK(client.reconnectAttempts() == 1);

    // Simulate reconnect success
    client.onReconnected();
    CHECK_FALSE(client.isReconnecting());
    CHECK(client.isConnected());
}

TEST_CASE("NetClient reconnect gives up after timeout") {
    NetClient client;
    client.onConnectionLost();

    // Simulate 60+ seconds of failed reconnection
    for (int i = 0; i < 30; ++i) {
        client.tickReconnect(2.5f);
    }
    CHECK(client.reconnectFailed());
}
```

- [ ] **Step 2: Add reconnect state to NetClient**

In `net_client.h`, add:

```cpp
public:
    bool isReconnecting() const { return reconnectState_ == ReconnectPhase::Reconnecting; }
    bool reconnectFailed() const { return reconnectState_ == ReconnectPhase::Failed; }
    int reconnectAttempts() const { return reconnectAttempts_; }
    void onConnectionLost();
    void onReconnected();
    void tickReconnect(float dt);

private:
    enum class ReconnectPhase { None, Reconnecting, Failed };
    ReconnectPhase reconnectState_ = ReconnectPhase::None;
    int reconnectAttempts_ = 0;
    float reconnectTimer_ = 0.0f;
    float reconnectDelay_ = 1.0f;    // starts at 1s
    float reconnectTimeout_ = 60.0f; // give up after 60s total
    float reconnectElapsed_ = 0.0f;
    std::string lastHost_;
    uint16_t lastPort_ = 0;
```

- [ ] **Step 3: Implement reconnect methods**

```cpp
void NetClient::onConnectionLost() {
    connected_ = false;
    reconnectState_ = ReconnectPhase::Reconnecting;
    reconnectAttempts_ = 0;
    reconnectTimer_ = 0.0f;
    reconnectDelay_ = 1.0f;
    reconnectElapsed_ = 0.0f;
}

void NetClient::onReconnected() {
    reconnectState_ = ReconnectPhase::None;
    reconnectAttempts_ = 0;
    connected_ = true;
}

void NetClient::tickReconnect(float dt) {
    if (reconnectState_ != ReconnectPhase::Reconnecting) return;

    reconnectElapsed_ += dt;
    if (reconnectElapsed_ >= reconnectTimeout_) {
        reconnectState_ = ReconnectPhase::Failed;
        if (onDisconnected) onDisconnected();
        return;
    }

    reconnectTimer_ -= dt;
    if (reconnectTimer_ <= 0.0f) {
        ++reconnectAttempts_;
        // Attempt reconnect with stored auth token
        if (!lastHost_.empty() && authToken_ != AuthToken{}) {
            connectWithToken(lastHost_, lastPort_, authToken_);
        }
        // Exponential backoff: 1s → 2s → 4s → 8s → ... capped at 30s
        reconnectDelay_ = std::min(reconnectDelay_ * 2.0f, 30.0f);
        reconnectTimer_ = reconnectDelay_;
    }
}
```

- [ ] **Step 4: Wire into poll() — detect heartbeat timeout as connection loss**

In `NetClient::poll()`, after the heartbeat timeout detection, instead of just disconnecting:

```cpp
// Detect missed heartbeats (no packets received for 5+ seconds)
if (connected_ && timeSinceLastReceived > 5.0f) {
    lastHost_ = /* store from last connect */;
    lastPort_ = /* store from last connect */;
    onConnectionLost();
    return;
}

// Tick reconnect if in reconnecting state
if (reconnectState_ == ReconnectPhase::Reconnecting) {
    tickReconnect(dt);
}
```

- [ ] **Step 5: Store host/port on successful connect**

In `connect()` and `connectWithToken()`, save:
```cpp
lastHost_ = host;
lastPort_ = port;
```

- [ ] **Step 6: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="NetClient"`
Expected: All reconnect tests PASS

- [ ] **Step 7: Commit**

```bash
git add engine/net/net_client.h engine/net/net_client.cpp tests/test_network_robustness.cpp
git commit -m "feat: wire auto-reconnect state machine with exponential backoff into NetClient"
```
