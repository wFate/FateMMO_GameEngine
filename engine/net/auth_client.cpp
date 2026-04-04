#include "engine/net/auth_client.h"
#include "engine/net/byte_stream.h"
#include "engine/core/logger.h"

#ifdef FATE_HAS_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

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

#ifndef FATE_HAS_OPENSSL
// Stub implementations when OpenSSL is not available (demo/open-source build)
AuthClient::~AuthClient() {}
void AuthClient::connect(const std::string&, uint16_t, const std::string&) {
    LOG_WARN("AuthClient", "TLS not available --OpenSSL not linked");
}
void AuthClient::disconnect() {}
void AuthClient::login(const std::string&, const std::string&) {}
void AuthClient::registerAccount(const std::string&, const std::string&, const std::string&) {}
void AuthClient::createCharacter(const std::string&, const std::string&) {}
void AuthClient::deleteCharacter(const std::string&) {}
void AuthClient::selectCharacter(const std::string&) {}
bool AuthClient::isConnected() const { return false; }
std::optional<AuthClientResult> AuthClient::poll() { return std::nullopt; }
#else

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
// SSL I/O helpers --length-prefixed (4-byte LE length + body)
// ---------------------------------------------------------------------------
static bool sslWriteAll(SSL* ssl, const void* data, int len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    int remaining = len;
    while (remaining > 0) {
        int written = SSL_write(ssl, p, remaining);
        if (written <= 0) return false;
        p += written;
        remaining -= written;
    }
    return true;
}

static bool sslReadAll(SSL* ssl, void* data, int len) {
    uint8_t* p = static_cast<uint8_t*>(data);
    int remaining = len;
    while (remaining > 0) {
        int r = SSL_read(ssl, p, remaining);
        if (r <= 0) return false;
        p += r;
        remaining -= r;
    }
    return true;
}

static bool sslSendMessage(SSL* ssl, const uint8_t* data, uint32_t len) {
    if (!sslWriteAll(ssl, &len, 4)) return false;
    if (!sslWriteAll(ssl, data, static_cast<int>(len))) return false;
    return true;
}

static constexpr uint32_t MAX_RESP_SIZE = 8192;

static bool sslRecvMessage(SSL* ssl, std::vector<uint8_t>& out) {
    uint8_t lenBuf[4];
    if (!sslReadAll(ssl, lenBuf, 4)) return false;

    uint32_t respLen = 0;
    std::memcpy(&respLen, lenBuf, 4);

    if (respLen == 0 || respLen > MAX_RESP_SIZE) return false;

    out.resize(respLen);
    if (!sslReadAll(ssl, out.data(), static_cast<int>(respLen))) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------
AuthClient::~AuthClient() {
    disconnectAuth();
}

void AuthClient::cleanup() {
    shouldStop_.store(true);
    cmdCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    shouldStop_.store(false);
    connected_.store(false);
    // Drain any stale commands (e.g. Disconnect left over from disconnectAuth)
    {
        std::lock_guard<std::mutex> lock(cmdMutex_);
        std::queue<AuthCommand> empty;
        cmdQueue_.swap(empty);
    }
}

void AuthClient::pushResult(AuthClientResult result) {
    std::lock_guard<std::mutex> lock(resultMutex_);
    result_ = std::move(result);
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
    req.clientVersion = 1;

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
    // Tear down any previous connection (its teardown may clear busy_)
    cleanup();

    busy_.store(true);
    worker_ = std::thread(&AuthClient::workerLoop, this, host, port, requestData);
}

// ---------------------------------------------------------------------------
// registerAsync
// ---------------------------------------------------------------------------
void AuthClient::registerAsync(const std::string& host, uint16_t port,
                               const std::string& username, const std::string& password,
                               const std::string& email,
                               const std::string& characterName, const std::string& className,
                               uint8_t faction, uint8_t gender, uint8_t hairstyle) {
    if (busy_.load()) return;

    // Serialize RegisterRequest (including the type byte from write())
    RegisterRequest req;
    req.username = username;
    req.password = password;
    req.email = email;
    req.characterName = characterName;
    req.className = className;
    req.faction = faction;
    req.gender = gender;
    req.hairstyle = hairstyle;
    req.clientVersion = 1;

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
    // Tear down any previous connection (its teardown may clear busy_)
    cleanup();

    busy_.store(true);
    worker_ = std::thread(&AuthClient::workerLoop, this, host, port, requestData);
}

// ---------------------------------------------------------------------------
// Command queue methods (post-login operations on persistent connection)
// ---------------------------------------------------------------------------
void AuthClient::createCharacterAsync(const std::string& name, const std::string& className,
                                      uint8_t faction, uint8_t gender, uint8_t hairstyle) {
    if (!connected_.load()) {
        LOG_WARN("AuthClient", "createCharacterAsync called but not connected");
        return;
    }

    AuthCommand cmd;
    cmd.type = AuthCommandType::Create;
    cmd.characterName = name;
    cmd.className = className;
    cmd.faction = faction;
    cmd.gender = gender;
    cmd.hairstyle = hairstyle;

    {
        std::lock_guard<std::mutex> lock(cmdMutex_);
        cmdQueue_.push(std::move(cmd));
    }
    cmdCv_.notify_one();
}

void AuthClient::deleteCharacterAsync(const std::string& characterId) {
    if (!connected_.load()) {
        LOG_WARN("AuthClient", "deleteCharacterAsync called but not connected");
        return;
    }

    AuthCommand cmd;
    cmd.type = AuthCommandType::Delete;
    cmd.characterId = characterId;

    {
        std::lock_guard<std::mutex> lock(cmdMutex_);
        cmdQueue_.push(std::move(cmd));
    }
    cmdCv_.notify_one();
}

void AuthClient::selectCharacterAsync(const std::string& characterId) {
    if (!connected_.load()) {
        LOG_WARN("AuthClient", "selectCharacterAsync called but not connected");
        return;
    }

    AuthCommand cmd;
    cmd.type = AuthCommandType::Select;
    cmd.characterId = characterId;

    {
        std::lock_guard<std::mutex> lock(cmdMutex_);
        cmdQueue_.push(std::move(cmd));
    }
    cmdCv_.notify_one();
}

void AuthClient::disconnectAuth() {
    if (!worker_.joinable()) return;

    {
        std::lock_guard<std::mutex> lock(cmdMutex_);
        AuthCommand cmd;
        cmd.type = AuthCommandType::Disconnect;
        cmdQueue_.push(std::move(cmd));
    }
    shouldStop_.store(true);
    cmdCv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
    shouldStop_.store(false);
    connected_.store(false);
    busy_.store(false);
}

// ---------------------------------------------------------------------------
// workerLoop --runs on background thread, maintains persistent TLS connection
// ---------------------------------------------------------------------------
void AuthClient::workerLoop(const std::string& host, uint16_t port,
                            const std::vector<uint8_t>& initialData) {
    auto fail = [this](const std::string& reason) {
        AuthClientResult res;
        res.type = AuthResultType::Login;
        res.success = false;
        res.errorMessage = reason;
        pushResult(std::move(res));
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

    // --- Send initial login/register message ---
    if (!sslSendMessage(ssl, initialData.data(), static_cast<uint32_t>(initialData.size()))) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("Failed to send initial auth message");
        return;
    }

    // --- Read initial AuthResponse ---
    std::vector<uint8_t> respBuf;
    if (!sslRecvMessage(ssl, respBuf)) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("Failed to read auth response");
        return;
    }

    ByteReader reader(respBuf.data(), respBuf.size());
    uint8_t typeByte = reader.readU8(); // consume the type byte
    (void)typeByte; // AuthResponse type

    AuthResponse authResp = AuthResponse::read(reader);
    if (reader.overflowed()) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        fail("Malformed auth response from server");
        return;
    }

    // Push the login result
    {
        AuthClientResult res;
        res.type = AuthResultType::Login;
        res.success = authResp.success;
        res.errorMessage = authResp.errorReason;
        res.characters = std::move(authResp.characters);
        pushResult(std::move(res));
    }

    if (!authResp.success) {
        LOG_WARN("AuthClient", "Auth failed: %s", authResp.errorReason.c_str());
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(sockFd);
        SSL_CTX_free(ctx);
        busy_.store(false);
        return;
    }

    LOG_INFO("AuthClient", "Auth succeeded, persistent connection established");
    connected_.store(true);
    busy_.store(false);

    // -----------------------------------------------------------------------
    // Command processing loop --waits for commands, sends/receives on the
    // persistent TLS connection
    // -----------------------------------------------------------------------
    while (!shouldStop_.load()) {
        AuthCommand cmd;
        {
            std::unique_lock<std::mutex> lock(cmdMutex_);
            cmdCv_.wait(lock, [this]() {
                return !cmdQueue_.empty() || shouldStop_.load();
            });

            if (shouldStop_.load()) break;
            if (cmdQueue_.empty()) continue;

            cmd = std::move(cmdQueue_.front());
            cmdQueue_.pop();
        }

        if (cmd.type == AuthCommandType::Disconnect) {
            break;
        }

        // Serialize and send the command
        uint8_t sendBuf[2048];
        ByteWriter w(sendBuf, sizeof(sendBuf));

        switch (cmd.type) {
            case AuthCommandType::Create: {
                CharCreateRequest req;
                req.characterName = cmd.characterName;
                req.className = cmd.className;
                req.faction = cmd.faction;
                req.gender = cmd.gender;
                req.hairstyle = cmd.hairstyle;
                req.write(w);
                break;
            }
            case AuthCommandType::Delete: {
                CharDeleteRequest req;
                req.characterId = cmd.characterId;
                req.write(w);
                break;
            }
            case AuthCommandType::Select: {
                SelectCharRequest req;
                req.characterId = cmd.characterId;
                req.write(w);
                break;
            }
            default:
                continue;
        }

        if (w.overflowed()) {
            LOG_ERROR("AuthClient", "Command serialization overflow");
            continue;
        }

        // Send the message
        if (!sslSendMessage(ssl, w.data(), static_cast<uint32_t>(w.size()))) {
            LOG_ERROR("AuthClient", "Failed to send command --connection lost");
            break;
        }

        // Read the response
        respBuf.clear();
        if (!sslRecvMessage(ssl, respBuf)) {
            LOG_ERROR("AuthClient", "Failed to read response --connection lost");
            break;
        }

        // Parse based on command type
        ByteReader rdr(respBuf.data(), respBuf.size());
        uint8_t respType = rdr.readU8(); // type byte
        (void)respType;

        switch (cmd.type) {
            case AuthCommandType::Create: {
                CharCreateResponse resp = CharCreateResponse::read(rdr);
                if (!rdr.overflowed()) {
                    AuthClientResult res;
                    res.type = AuthResultType::Create;
                    res.success = resp.success;
                    res.errorMessage = resp.errorMessage;
                    res.characters = std::move(resp.characters);
                    pushResult(std::move(res));
                } else {
                    LOG_ERROR("AuthClient", "Malformed CharCreateResponse");
                }
                break;
            }
            case AuthCommandType::Delete: {
                CharDeleteResponse resp = CharDeleteResponse::read(rdr);
                if (!rdr.overflowed()) {
                    AuthClientResult res;
                    res.type = AuthResultType::Delete;
                    res.success = resp.success;
                    res.errorMessage = resp.errorMessage;
                    res.characters = std::move(resp.characters);
                    pushResult(std::move(res));
                } else {
                    LOG_ERROR("AuthClient", "Malformed CharDeleteResponse");
                }
                break;
            }
            case AuthCommandType::Select: {
                SelectCharResponse resp = SelectCharResponse::read(rdr);
                if (!rdr.overflowed()) {
                    AuthClientResult res;
                    res.type = AuthResultType::Select;
                    res.success = resp.success;
                    res.errorMessage = resp.errorMessage;
                    res.selectData = std::move(resp);
                    pushResult(std::move(res));
                } else {
                    LOG_ERROR("AuthClient", "Malformed SelectCharResponse");
                }
                break;
            }
            default:
                break;
        }
    }

    // -----------------------------------------------------------------------
    // Cleanup --tear down TLS and socket
    // -----------------------------------------------------------------------
    SSL_shutdown(ssl);
    SSL_free(ssl);
    closeSocket(sockFd);
    SSL_CTX_free(ctx);
    connected_.store(false);
    busy_.store(false);

    LOG_INFO("AuthClient", "Persistent auth connection closed");
}

// ---------------------------------------------------------------------------
// Main-thread polling
// ---------------------------------------------------------------------------
bool AuthClient::hasResult() const {
    std::lock_guard<std::mutex> lock(resultMutex_);
    return result_.has_value();
}

AuthClientResult AuthClient::consumeResult() {
    std::lock_guard<std::mutex> lock(resultMutex_);
    AuthClientResult res = std::move(result_.value());
    result_.reset();
    return res;
}

#endif // FATE_HAS_OPENSSL

} // namespace fate
