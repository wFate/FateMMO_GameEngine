#include "engine/net/auth_client.h"
#include "engine/net/byte_stream.h"
#include "engine/core/logger.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <cstring>

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
#include <unistd.h>
#endif

namespace fate {

// ---------------------------------------------------------------------------
// Platform socket helpers
// ---------------------------------------------------------------------------
static constexpr uintptr_t INVALID_SOCK_HANDLE = ~uintptr_t(0);

#ifdef _WIN32
static void closeSocket(uintptr_t fd) {
    if (fd != INVALID_SOCK_HANDLE) {
        ::closesocket(static_cast<SOCKET>(fd));
    }
}
#else
static void closeSocket(uintptr_t fd) {
    if (fd != INVALID_SOCK_HANDLE) {
        ::close(static_cast<int>(fd));
    }
}
#endif

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------
AuthClient::~AuthClient() {
    cleanup();
}

void AuthClient::cleanup() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

// ---------------------------------------------------------------------------
// loginAsync
// ---------------------------------------------------------------------------
void AuthClient::loginAsync(const std::string& host, uint16_t port,
                            const std::string& username, const std::string& password) {
    if (busy_.load()) return;

    // Serialize LoginRequest (including the type byte from write())
    LoginRequest req;
    req.username = username;
    req.password = password;

    uint8_t buf[1024];
    ByteWriter w(buf, sizeof(buf));
    req.write(w);

    if (w.overflowed()) {
        LOG_ERROR("AuthClient", "LoginRequest serialization overflow");
        return;
    }

    std::vector<uint8_t> requestData(w.data(), w.data() + w.size());

    // Clear previous result and mark busy
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result_.reset();
    }
    busy_.store(true);

    // Join any previous worker before spawning a new one
    cleanup();

    worker_ = std::thread(&AuthClient::doAuth, this, host, port, requestData);
}

// ---------------------------------------------------------------------------
// registerAsync
// ---------------------------------------------------------------------------
void AuthClient::registerAsync(const std::string& host, uint16_t port,
                               const std::string& username, const std::string& password,
                               const std::string& email,
                               const std::string& characterName, const std::string& className) {
    if (busy_.load()) return;

    // Serialize RegisterRequest (including the type byte from write())
    RegisterRequest req;
    req.username = username;
    req.password = password;
    req.email = email;
    req.characterName = characterName;
    req.className = className;

    uint8_t buf[2048];
    ByteWriter w(buf, sizeof(buf));
    req.write(w);

    if (w.overflowed()) {
        LOG_ERROR("AuthClient", "RegisterRequest serialization overflow");
        return;
    }

    std::vector<uint8_t> requestData(w.data(), w.data() + w.size());

    // Clear previous result and mark busy
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result_.reset();
    }
    busy_.store(true);

    // Join any previous worker before spawning a new one
    cleanup();

    worker_ = std::thread(&AuthClient::doAuth, this, host, port, requestData);
}

// ---------------------------------------------------------------------------
// doAuth — runs on background thread
// ---------------------------------------------------------------------------
void AuthClient::doAuth(const std::string& host, uint16_t port,
                        const std::vector<uint8_t>& requestData) {
    auto fail = [this](const std::string& reason) {
        AuthResponse resp;
        resp.success = false;
        resp.errorReason = reason;
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            result_ = resp;
        }
        busy_.store(false);
    };

    // --- OpenSSL context ---
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        fail("Failed to create SSL context");
        return;
    }

    // Enforce TLS 1.2+ and restrict to AEAD cipher suites
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_cipher_list(ctx,
        "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS");
    SSL_CTX_set_ciphersuites(ctx,
        "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256");

#ifdef FATE_DEV_TLS
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
#endif

    // --- TCP connect ---
#ifdef _WIN32
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        SSL_CTX_free(ctx);
        fail("Failed to create TCP socket");
        return;
    }
    uintptr_t sockFd = static_cast<uintptr_t>(sock);
#else
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        SSL_CTX_free(ctx);
        fail("Failed to create TCP socket");
        return;
    }
    uintptr_t sockFd = static_cast<uintptr_t>(sock);
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Parse dotted-quad IP
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(host.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;
        ip = htonl(ip);
        std::memcpy(&addr.sin_addr, &ip, 4);
    } else {
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("Invalid host address: " + host);
        return;
    }

#ifdef _WIN32
    if (::connect(static_cast<SOCKET>(sockFd), reinterpret_cast<sockaddr*>(&addr),
                  sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("TCP connect failed (error " + std::to_string(err) + ")");
        return;
    }
#else
    if (::connect(static_cast<int>(sockFd), reinterpret_cast<sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("TCP connect failed");
        return;
    }
#endif

    // --- TLS handshake ---
    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("SSL_new failed");
        return;
    }

    SSL_set_fd(ssl, static_cast<int>(sockFd));

    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("TLS handshake failed");
        return;
    }

    // --- Write length-prefixed request ---
    uint32_t sendLen = static_cast<uint32_t>(requestData.size());
    if (SSL_write(ssl, &sendLen, 4) != 4) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("Failed to write message length");
        return;
    }

    if (SSL_write(ssl, requestData.data(), static_cast<int>(sendLen)) != static_cast<int>(sendLen)) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("Failed to write message body");
        return;
    }

    // --- Read length-prefixed response ---
    uint8_t lenBuf[4];
    int bytesRead = 0;
    while (bytesRead < 4) {
        int r = SSL_read(ssl, lenBuf + bytesRead, 4 - bytesRead);
        if (r <= 0) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            fail("Failed to read response length");
            return;
        }
        bytesRead += r;
    }

    uint32_t respLen = 0;
    std::memcpy(&respLen, lenBuf, 4);

    static constexpr uint32_t MAX_RESP_SIZE = 4096;
    if (respLen == 0 || respLen > MAX_RESP_SIZE) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("Invalid response length: " + std::to_string(respLen));
        return;
    }

    std::vector<uint8_t> respBuf(respLen);
    int totalRead = 0;
    while (static_cast<uint32_t>(totalRead) < respLen) {
        int r = SSL_read(ssl, respBuf.data() + totalRead,
                         static_cast<int>(respLen) - totalRead);
        if (r <= 0) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(sockFd);
            SSL_CTX_free(ctx);
            fail("Failed to read response body");
            return;
        }
        totalRead += r;
    }

    // --- Parse AuthResponse ---
    ByteReader reader(respBuf.data(), respBuf.size());
    AuthResponse response = AuthResponse::read(reader);

    if (reader.overflowed()) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("Malformed auth response from server");
        return;
    }

    // --- Cleanup TLS/socket ---
    SSL_shutdown(ssl);
    SSL_free(ssl);
    closeSocket(sockFd);
    SSL_CTX_free(ctx);

    // --- Store result ---
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        result_ = response;
    }
    busy_.store(false);

    if (response.success) {
        LOG_INFO("AuthClient", "Auth succeeded — character '%s' (%s) level %d",
                 response.characterName.c_str(), response.className.c_str(), response.level);
    } else {
        LOG_WARN("AuthClient", "Auth failed: %s", response.errorReason.c_str());
    }
}

// ---------------------------------------------------------------------------
// Main-thread polling
// ---------------------------------------------------------------------------
bool AuthClient::hasResult() const {
    std::lock_guard<std::mutex> lock(resultMutex_);
    return result_.has_value();
}

AuthResponse AuthClient::consumeResult() {
    std::lock_guard<std::mutex> lock(resultMutex_);
    AuthResponse resp = result_.value();
    result_.reset();
    return resp;
}

} // namespace fate
