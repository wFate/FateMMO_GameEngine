#include "server/auth/auth_server.h"
#include "engine/core/logger.h"
#include "engine/core/types.h"
#include "engine/net/byte_stream.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

#include <bcrypt_hashpw.h>

#include <chrono>
#include <cstring>

// Winsock (must come before Windows.h)
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
#include <fcntl.h>
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
AuthServer::~AuthServer() {
    stop();
}

// ---------------------------------------------------------------------------
// start()
// ---------------------------------------------------------------------------
bool AuthServer::start(uint16_t port, const std::string& certPath, const std::string& keyPath,
                       const std::string& dbConnectionString) {
    if (running_.load()) {
        LOG_ERROR("AuthServer", "Already running");
        return false;
    }

    // --- OpenSSL init ---
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    sslCtx_ = SSL_CTX_new(TLS_server_method());
    if (!sslCtx_) {
        LOG_ERROR("AuthServer", "SSL_CTX_new failed");
        return false;
    }

    if (SSL_CTX_use_certificate_file(sslCtx_, certPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("AuthServer", "Failed to load certificate: %s", certPath.c_str());
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }

    if (SSL_CTX_use_PrivateKey_file(sslCtx_, keyPath.c_str(), SSL_FILETYPE_PEM) <= 0) {
        LOG_ERROR("AuthServer", "Failed to load private key: %s", keyPath.c_str());
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }

    if (!SSL_CTX_check_private_key(sslCtx_)) {
        LOG_ERROR("AuthServer", "Certificate / private key mismatch");
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }

    // --- Database ---
    if (!dbConn_.connect(dbConnectionString)) {
        LOG_ERROR("AuthServer", "Failed to connect to database");
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }
    accountRepo_ = std::make_unique<AccountRepository>(dbConn_.connection());
    characterRepo_ = std::make_unique<CharacterRepository>(dbConn_.connection());

    // --- TCP listen socket ---
#ifdef _WIN32
    // WSAStartup is expected to be called already by NetSocket::initPlatform()
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("AuthServer", "socket() failed: %d", WSAGetLastError());
        dbConn_.disconnect();
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }
    listenFd_ = static_cast<uintptr_t>(sock);
#else
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR("AuthServer", "socket() failed");
        dbConn_.disconnect();
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }
    listenFd_ = static_cast<uintptr_t>(sock);
#endif

    // Allow address reuse
    int reuseVal = 1;
#ifdef _WIN32
    ::setsockopt(static_cast<SOCKET>(listenFd_), SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuseVal), sizeof(reuseVal));
#else
    ::setsockopt(static_cast<int>(listenFd_), SOL_SOCKET, SO_REUSEADDR,
                 &reuseVal, sizeof(reuseVal));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

#ifdef _WIN32
    if (::bind(static_cast<SOCKET>(listenFd_), reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("AuthServer", "bind() failed on port %d: %d", port, WSAGetLastError());
        closeSocket(listenFd_);
        listenFd_ = INVALID_SOCK;
        dbConn_.disconnect();
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }

    if (::listen(static_cast<SOCKET>(listenFd_), SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("AuthServer", "listen() failed: %d", WSAGetLastError());
        closeSocket(listenFd_);
        listenFd_ = INVALID_SOCK;
        dbConn_.disconnect();
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }
#else
    if (::bind(static_cast<int>(listenFd_), reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) < 0) {
        LOG_ERROR("AuthServer", "bind() failed on port %d", port);
        closeSocket(listenFd_);
        listenFd_ = INVALID_SOCK;
        dbConn_.disconnect();
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }

    if (::listen(static_cast<int>(listenFd_), SOMAXCONN) < 0) {
        LOG_ERROR("AuthServer", "listen() failed");
        closeSocket(listenFd_);
        listenFd_ = INVALID_SOCK;
        dbConn_.disconnect();
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
        return false;
    }
#endif

    port_ = port;
    running_.store(true);
    listenThread_ = std::thread(&AuthServer::listenLoop, this);

    LOG_INFO("AuthServer", "TLS auth server listening on port %d", port);
    return true;
}

// ---------------------------------------------------------------------------
// stop()
// ---------------------------------------------------------------------------
void AuthServer::stop() {
    if (!running_.load()) return;
    running_.store(false);

    if (listenThread_.joinable()) {
        listenThread_.join();
    }

    if (listenFd_ != INVALID_SOCK) {
        closeSocket(listenFd_);
        listenFd_ = INVALID_SOCK;
    }

    if (sslCtx_) {
        SSL_CTX_free(sslCtx_);
        sslCtx_ = nullptr;
    }

    dbConn_.disconnect();
    accountRepo_.reset();
    characterRepo_.reset();

    LOG_INFO("AuthServer", "Stopped");
}

// ---------------------------------------------------------------------------
// listenLoop() — runs on auth thread
// ---------------------------------------------------------------------------
void AuthServer::listenLoop() {
#ifdef _WIN32
    SOCKET sock = static_cast<SOCKET>(listenFd_);
#else
    int sock = static_cast<int>(listenFd_);
#endif

    while (running_.load()) {
        // Use select() with 500ms timeout so we can check running_ periodically
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500ms

#ifdef _WIN32
        // On Windows, first arg to select() is ignored
        int sel = ::select(0, &readSet, nullptr, nullptr, &tv);
#else
        int sel = ::select(sock + 1, &readSet, nullptr, nullptr, &tv);
#endif

        if (sel <= 0) continue; // timeout or error, loop around to check running_
        if (!FD_ISSET(sock, &readSet)) continue;

        sockaddr_in clientAddr{};
#ifdef _WIN32
        int addrLen = sizeof(clientAddr);
        SOCKET clientSock = ::accept(sock, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientSock == INVALID_SOCKET) {
            if (running_.load()) {
                LOG_ERROR("AuthServer", "accept() failed: %d", WSAGetLastError());
            }
            continue;
        }
        uintptr_t clientFd = static_cast<uintptr_t>(clientSock);
#else
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = ::accept(sock, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientSock < 0) {
            if (running_.load()) {
                LOG_ERROR("AuthServer", "accept() failed");
            }
            continue;
        }
        uintptr_t clientFd = static_cast<uintptr_t>(clientSock);
#endif

        handleClient(clientFd);
    }
}

// ---------------------------------------------------------------------------
// handleClient() — TLS handshake, read request, process, write response
// ---------------------------------------------------------------------------
void AuthServer::handleClient(uintptr_t clientFd) {
    SSL* ssl = SSL_new(sslCtx_);
    if (!ssl) {
        LOG_ERROR("AuthServer", "SSL_new failed");
        closeSocket(clientFd);
        return;
    }

    SSL_set_fd(ssl, static_cast<int>(clientFd));

    if (SSL_accept(ssl) <= 0) {
        LOG_ERROR("AuthServer", "SSL_accept (TLS handshake) failed");
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        closeSocket(clientFd);
        return;
    }

    // --- Read length-prefixed message ---
    // Protocol: 4 bytes (uint32_t little-endian) message length, then message bytes
    uint8_t lenBuf[4];
    int bytesRead = 0;
    while (bytesRead < 4) {
        int r = SSL_read(ssl, lenBuf + bytesRead, 4 - bytesRead);
        if (r <= 0) {
            LOG_ERROR("AuthServer", "SSL_read (length prefix) failed");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(clientFd);
            return;
        }
        bytesRead += r;
    }

    uint32_t msgLen = 0;
    std::memcpy(&msgLen, lenBuf, 4);

    static constexpr uint32_t MAX_MSG_SIZE = 4096;
    if (msgLen == 0 || msgLen > MAX_MSG_SIZE) {
        LOG_ERROR("AuthServer", "Invalid message length: %u", msgLen);
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(clientFd);
        return;
    }

    std::vector<uint8_t> msgBuf(msgLen);
    int totalRead = 0;
    while (static_cast<uint32_t>(totalRead) < msgLen) {
        int r = SSL_read(ssl, msgBuf.data() + totalRead,
                         static_cast<int>(msgLen) - totalRead);
        if (r <= 0) {
            LOG_ERROR("AuthServer", "SSL_read (message body) failed");
            SSL_shutdown(ssl);
            SSL_free(ssl);
            closeSocket(clientFd);
            return;
        }
        totalRead += r;
    }

    // --- Parse message type ---
    ByteReader reader(msgBuf.data(), msgBuf.size());
    uint8_t typeByte = reader.readU8();
    auto msgType = static_cast<AuthMessageType>(typeByte);

    AuthResponse resp;

    switch (msgType) {
        case AuthMessageType::RegisterRequest: {
            auto req = RegisterRequest::read(reader);
            if (reader.overflowed()) {
                resp.success = false;
                resp.errorReason = "Malformed register request";
            } else {
                processRegister(req, resp);
            }
            break;
        }
        case AuthMessageType::LoginRequest: {
            auto req = LoginRequest::read(reader);
            if (reader.overflowed()) {
                resp.success = false;
                resp.errorReason = "Malformed login request";
            } else {
                processLogin(req, resp);
            }
            break;
        }
        default:
            resp.success = false;
            resp.errorReason = "Unknown message type";
            break;
    }

    // --- Write length-prefixed response ---
    uint8_t respBuf[1024];
    ByteWriter writer(respBuf, sizeof(respBuf));
    resp.write(writer);

    if (writer.overflowed()) {
        LOG_ERROR("AuthServer", "Response buffer overflow");
        SSL_shutdown(ssl);
        SSL_free(ssl);
        closeSocket(clientFd);
        return;
    }

    uint32_t respLen = static_cast<uint32_t>(writer.size());
    uint8_t respLenBuf[4];
    std::memcpy(respLenBuf, &respLen, 4);

    SSL_write(ssl, respLenBuf, 4);
    SSL_write(ssl, respBuf, static_cast<int>(respLen));

    // --- Cleanup ---
    SSL_shutdown(ssl);
    SSL_free(ssl);
    closeSocket(clientFd);
}

// ---------------------------------------------------------------------------
// processRegister()
// ---------------------------------------------------------------------------
void AuthServer::processRegister(const RegisterRequest& req, AuthResponse& resp) {
    // Validate inputs
    if (!AuthValidation::isValidUsername(req.username)) {
        resp.success = false;
        resp.errorReason = "Invalid username (3-20 alphanumeric/underscore characters)";
        return;
    }
    if (!AuthValidation::isValidPassword(req.password)) {
        resp.success = false;
        resp.errorReason = "Invalid password (8-128 printable ASCII characters)";
        return;
    }
    if (!AuthValidation::isValidEmail(req.email)) {
        resp.success = false;
        resp.errorReason = "Invalid email address";
        return;
    }
    if (!AuthValidation::isValidCharacterName(req.characterName)) {
        resp.success = false;
        resp.errorReason = "Invalid character name (2-16 alphanumeric characters)";
        return;
    }

    // Hash password
    std::string hashedPw = hashPassword(req.password);
    if (hashedPw.empty()) {
        resp.success = false;
        resp.errorReason = "Internal error: password hashing failed";
        LOG_ERROR("AuthServer", "bcrypt hash failed for registration of '%s'", req.username.c_str());
        return;
    }

    // Create account
    int accountId = accountRepo_->createAccount(req.username, hashedPw, req.email);
    if (accountId < 0) {
        resp.success = false;
        resp.errorReason = "Username or email already exists";
        return;
    }

    // Create character
    std::string charId = characterRepo_->createDefaultCharacter(accountId, req.characterName, req.className);
    if (charId.empty()) {
        resp.success = false;
        resp.errorReason = "Character name already taken";
        return;
    }

    // Grant starter equipment based on class
    try {
        pqxx::work txn(dbConn_.connection());

        // Class-specific weapon
        std::string weaponId;
        if (req.className == "Warrior")    weaponId = "item_rusty_dagger";
        else if (req.className == "Mage")  weaponId = "item_gnarled_stick";
        else if (req.className == "Archer") weaponId = "item_makeshift_bow";

        const char* insertSql =
            "INSERT INTO character_inventory (character_id, item_id, is_equipped, equipped_slot, quantity) "
            "VALUES ($1, $2, TRUE, $3, 1)";

        if (!weaponId.empty()) {
            txn.exec_params(insertSql, charId, weaponId, "weapon");
        }
        txn.exec_params(insertSql, charId, "item_quilted_vest", "armor");
        txn.exec_params(insertSql, charId, "item_worn_sandals", "boots");
        txn.exec_params(insertSql, charId, "item_tattered_gloves", "gloves");

        txn.commit();
        LOG_INFO("AuthServer", "Granted starter equipment to character '%s' (%s)",
                 req.characterName.c_str(), req.className.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("AuthServer", "Failed to grant starter equipment: %s", e.what());
        // Non-fatal — character still created, just no gear
    }

    // Generate auth token and pending session
    AuthToken token = generateAuthToken();

    auto now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    PendingSession session;
    session.account_id = accountId;
    session.character_id = charId;
    session.created_at = now;
    session.expires_at = now + 30.0; // 30 second window to connect via UDP

    pushResult(AuthResult{token, session});

    resp.success = true;
    resp.authToken = token;
    resp.characterName = req.characterName;
    resp.className = req.className;
    resp.level = 1;
    resp.sceneName = "WhisperingWoods";  // New characters start here

    LOG_INFO("AuthServer", "Registered account '%s' (id=%d), character '%s'",
             req.username.c_str(), accountId, req.characterName.c_str());
}

// ---------------------------------------------------------------------------
// processLogin()
// ---------------------------------------------------------------------------
void AuthServer::processLogin(const LoginRequest& req, AuthResponse& resp) {
    // Look up account
    auto account = accountRepo_->findByUsername(req.username);
    if (!account) {
        resp.success = false;
        resp.errorReason = "Invalid username or password";
        return;
    }

    // Verify password
    if (!verifyPassword(req.password, account->password_hash)) {
        resp.success = false;
        resp.errorReason = "Invalid username or password";
        return;
    }

    // Check ban
    if (account->is_banned) {
        resp.success = false;
        resp.errorReason = "Account is banned: " + account->ban_reason;
        return;
    }

    // Check active
    if (!account->is_active) {
        resp.success = false;
        resp.errorReason = "Account is inactive";
        return;
    }

    // Load character
    auto character = characterRepo_->loadCharacterByAccount(account->account_id);
    if (!character) {
        resp.success = false;
        resp.errorReason = "No character found for this account";
        return;
    }

    // Update last login
    accountRepo_->updateLastLogin(account->account_id);

    // Generate auth token and pending session
    AuthToken token = generateAuthToken();

    auto now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    PendingSession session;
    session.account_id = account->account_id;
    session.character_id = character->character_id;
    session.created_at = now;
    session.expires_at = now + 30.0; // 30 second window to connect via UDP
    session.admin_role = account->admin_role;

    pushResult(AuthResult{token, session});

    resp.success = true;
    resp.authToken = token;
    resp.characterName = character->character_name;
    resp.className = character->class_name;
    // Fix legacy "Scene2" default, then send the scene name
    resp.sceneName = (character->current_scene == "Scene2" || character->current_scene.empty())
        ? "WhisperingWoods" : character->current_scene;
    // Send saved position in pixel coords so client spawns at the right place
    resp.spawnX = character->position_x * Coords::TILE_SIZE;
    resp.spawnY = character->position_y * Coords::TILE_SIZE;
    // Full character state
    resp.level = character->level;
    resp.currentXP = character->current_xp;
    resp.gold = character->gold;
    resp.currentHP = character->current_hp;
    resp.maxHP = character->max_hp;
    resp.currentMP = character->current_mp;
    resp.maxMP = character->max_mp;
    resp.currentFury = character->current_fury;
    resp.honor = character->honor;
    resp.pvpKills = character->pvp_kills;
    resp.pvpDeaths = character->pvp_deaths;
    resp.isDead = character->is_dead ? 1 : 0;
    resp.faction = 0; // TODO: load faction from DB when faction column exists

    LOG_INFO("AuthServer", "Login successful: '%s' (id=%d), character '%s' level %d",
             req.username.c_str(), account->account_id,
             character->character_name.c_str(), character->level);
}

// ---------------------------------------------------------------------------
// bcrypt helpers
// ---------------------------------------------------------------------------
std::string AuthServer::hashPassword(const std::string& password) {
    char salt[BCRYPT_HASHSIZE];
    char hash[BCRYPT_HASHSIZE];

    if (bcrypt_gensalt(12, salt) != 0) {
        LOG_ERROR("AuthServer", "bcrypt_gensalt failed");
        return {};
    }

    if (bcrypt_hashpw(password.c_str(), salt, hash) != 0) {
        LOG_ERROR("AuthServer", "bcrypt_hashpw failed");
        return {};
    }

    return std::string(hash);
}

bool AuthServer::verifyPassword(const std::string& password, const std::string& hash) {
    int ret = bcrypt_checkpw(password.c_str(), hash.c_str());
    if (ret == -1) {
        LOG_ERROR("AuthServer", "bcrypt_checkpw returned error");
        return false;
    }
    return ret == 0;
}

// ---------------------------------------------------------------------------
// Thread-safe queue
// ---------------------------------------------------------------------------
void AuthServer::pushResult(const AuthResult& result) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    resultQueue_.push(result);
}

bool AuthServer::popAuthResult(AuthResult& out) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (resultQueue_.empty()) return false;
    out = resultQueue_.front();
    resultQueue_.pop();
    return true;
}

} // namespace fate
