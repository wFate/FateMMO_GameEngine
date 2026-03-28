#include "server/auth/auth_server.h"
#include "engine/core/logger.h"
#include "engine/core/types.h"
#include "engine/net/byte_stream.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

#include <bcrypt_hashpw.h>
#include "game/shared/profanity_filter.h"

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

    // Enforce TLS 1.2+ and restrict to AEAD cipher suites
    SSL_CTX_set_min_proto_version(sslCtx_, TLS1_2_VERSION);
    SSL_CTX_set_options(sslCtx_, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_cipher_list(sslCtx_,
        "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS");
    SSL_CTX_set_ciphersuites(sslCtx_,
        "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256");

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
    itemDefCache_.initialize(dbConn_.connection());

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

    // Wait briefly for active client handlers to finish on shutdown.
    // With persistent connections, we can't wait CLIENT_TIMEOUT_SECONDS (5 min);
    // close the listen socket first, then give handlers 5s to notice running_==false.
    static constexpr int SHUTDOWN_GRACE_SECONDS = 5;
    for (int i = 0; i < SHUTDOWN_GRACE_SECONDS * 10 && activeClients_.load() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (activeClients_.load() > 0) {
        LOG_WARN("AuthServer", "Shutdown with %d client handler(s) still active", activeClients_.load());
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

        if (activeClients_.load() >= MAX_CONCURRENT_CLIENTS) {
            LOG_WARN("AuthServer", "Max concurrent auth clients (%d), rejecting", MAX_CONCURRENT_CLIENTS);
            closeSocket(clientFd);
            continue;
        }
        activeClients_++;
        std::thread([this, clientFd]() {
            handleClient(clientFd);
            activeClients_--;
        }).detach();
    }
}

// ---------------------------------------------------------------------------
// SSL helper: write a length-prefixed response buffer via SSL
// Returns false on write failure.
// ---------------------------------------------------------------------------
static bool sslWriteResponse(SSL* ssl, const uint8_t* data, uint32_t len) {
    uint8_t lenBuf[4];
    std::memcpy(lenBuf, &len, 4);
    if (SSL_write(ssl, lenBuf, 4) <= 0) return false;
    if (SSL_write(ssl, data, static_cast<int>(len)) <= 0) return false;
    return true;
}

// ---------------------------------------------------------------------------
// SSL helper: read a length-prefixed message via SSL
// Returns false on read failure or invalid length (client disconnected / timeout).
// ---------------------------------------------------------------------------
static bool sslReadMessage(SSL* ssl, std::vector<uint8_t>& outBuf) {
    static constexpr uint32_t MAX_MSG_SIZE = 4096;

    // Read 4-byte length prefix
    uint8_t lenBuf[4];
    int bytesRead = 0;
    while (bytesRead < 4) {
        int r = SSL_read(ssl, lenBuf + bytesRead, 4 - bytesRead);
        if (r <= 0) return false;
        bytesRead += r;
    }

    uint32_t msgLen = 0;
    std::memcpy(&msgLen, lenBuf, 4);
    if (msgLen == 0 || msgLen > MAX_MSG_SIZE) return false;

    outBuf.resize(msgLen);
    int totalRead = 0;
    while (static_cast<uint32_t>(totalRead) < msgLen) {
        int r = SSL_read(ssl, outBuf.data() + totalRead,
                         static_cast<int>(msgLen) - totalRead);
        if (r <= 0) return false;
        totalRead += r;
    }
    return true;
}

// ---------------------------------------------------------------------------
// handleClient() — TLS handshake, persistent message loop
// ---------------------------------------------------------------------------
void AuthServer::handleClient(uintptr_t clientFd) {
    // Set per-connection recv timeout (5 min for char-select browsing)
#ifdef _WIN32
    DWORD timeout = CLIENT_TIMEOUT_SECONDS * 1000;
    setsockopt(static_cast<SOCKET>(clientFd), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval tv{CLIENT_TIMEOUT_SECONDS, 0};
    setsockopt(static_cast<int>(clientFd), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

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

    // Per-connection auth state (set after successful login/register)
    int accountId = 0;
    AdminRole adminRole = AdminRole::Player;

    // --- Persistent message loop ---
    while (running_.load()) {
        std::vector<uint8_t> msgBuf;
        if (!sslReadMessage(ssl, msgBuf)) {
            break; // Client disconnected or timeout
        }

        ByteReader reader(msgBuf.data(), msgBuf.size());
        uint8_t typeByte = reader.readU8();
        auto msgType = static_cast<AuthMessageType>(typeByte);

        // Response buffer (4 KB — enough for char list + snapshot)
        uint8_t respBuf[4096];
        ByteWriter writer(respBuf, sizeof(respBuf));
        bool wrote = false;

        switch (msgType) {
            // ----------------------------------------------------------
            // Login / Register — only allowed before authentication
            // ----------------------------------------------------------
            case AuthMessageType::RegisterRequest: {
                AuthResponse resp;
                if (accountId != 0) {
                    resp.success = false;
                    resp.errorReason = "Already authenticated";
                } else {
                    auto req = RegisterRequest::read(reader);
                    if (reader.overflowed()) {
                        resp.success = false;
                        resp.errorReason = "Malformed register request";
                    } else {
                        processRegister(req, resp, accountId, adminRole);
                    }
                }
                resp.write(writer);
                wrote = true;
                break;
            }
            case AuthMessageType::LoginRequest: {
                AuthResponse resp;
                if (accountId != 0) {
                    resp.success = false;
                    resp.errorReason = "Already authenticated";
                } else {
                    auto req = LoginRequest::read(reader);
                    if (reader.overflowed()) {
                        resp.success = false;
                        resp.errorReason = "Malformed login request";
                    } else {
                        processLogin(req, resp, accountId, adminRole);
                    }
                }
                resp.write(writer);
                wrote = true;
                break;
            }

            // ----------------------------------------------------------
            // Character management — require authenticated connection
            // ----------------------------------------------------------
            case AuthMessageType::CharCreateRequest: {
                CharCreateResponse resp;
                if (accountId == 0) {
                    resp.success = false;
                    resp.errorMessage = "Not authenticated";
                } else {
                    auto req = CharCreateRequest::read(reader);
                    if (reader.overflowed()) {
                        resp.success = false;
                        resp.errorMessage = "Malformed create request";
                    } else {
                        resp = processCharacterCreate(req, accountId);
                    }
                }
                resp.write(writer);
                wrote = true;
                break;
            }
            case AuthMessageType::CharDeleteRequest: {
                CharDeleteResponse resp;
                if (accountId == 0) {
                    resp.success = false;
                    resp.errorMessage = "Not authenticated";
                } else {
                    auto req = CharDeleteRequest::read(reader);
                    if (reader.overflowed()) {
                        resp.success = false;
                        resp.errorMessage = "Malformed delete request";
                    } else {
                        resp = processCharacterDelete(req, accountId);
                    }
                }
                resp.write(writer);
                wrote = true;
                break;
            }
            case AuthMessageType::SelectCharRequest: {
                SelectCharResponse resp;
                if (accountId == 0) {
                    resp.success = false;
                    resp.errorMessage = "Not authenticated";
                } else {
                    auto req = SelectCharRequest::read(reader);
                    if (reader.overflowed()) {
                        resp.success = false;
                        resp.errorMessage = "Malformed select request";
                    } else {
                        resp = processSelectCharacter(req, accountId, adminRole);
                    }
                }
                resp.write(writer);
                wrote = true;
                break;
            }

            default: {
                // Unknown message type — send error AuthResponse
                AuthResponse resp;
                resp.success = false;
                resp.errorReason = "Unknown message type";
                resp.write(writer);
                wrote = true;
                break;
            }
        }

        if (wrote) {
            if (writer.overflowed()) {
                LOG_ERROR("AuthServer", "Response buffer overflow for msg type %d", typeByte);
                break;
            }
            if (!sslWriteResponse(ssl, respBuf, static_cast<uint32_t>(writer.size()))) {
                LOG_ERROR("AuthServer", "SSL_write failed");
                break;
            }
        }
    }

    // --- Cleanup ---
    SSL_shutdown(ssl);
    SSL_free(ssl);
    closeSocket(clientFd);
}

// ---------------------------------------------------------------------------
// grantStarterEquipment() — insert starter gear for a new character
// ---------------------------------------------------------------------------
void AuthServer::grantStarterEquipment(const std::string& charId, const std::string& className) {
    try {
        std::lock_guard<std::mutex> lock(dbMutex_);
        pqxx::work txn(dbConn_.connection());

        std::string weaponId;
        if (className == "Warrior")       weaponId = "item_rusty_dagger";
        else if (className == "Mage")     weaponId = "item_gnarled_stick";
        else if (className == "Archer")   weaponId = "item_makeshift_bow";

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
                 charId.c_str(), className.c_str());
    } catch (const std::exception& e) {
        LOG_ERROR("AuthServer", "Failed to grant starter equipment: %s", e.what());
    }
}

// ---------------------------------------------------------------------------
// buildCharacterList() — load all characters for an account as previews
// ---------------------------------------------------------------------------
std::vector<CharacterPreview> AuthServer::buildCharacterList(int accountId) {
    std::vector<CharacterPreview> list;
    // characterRepo_ uses acquireConn() which wraps the single dbConn_ reference,
    // so we must hold dbMutex_ to serialize access to that connection.
    std::lock_guard<std::mutex> lock(dbMutex_);
    auto records = characterRepo_->loadCharactersByAccount(accountId);
    for (const auto& rec : records) {
        CharacterPreview p;
        p.characterId = rec.character_id;
        p.characterName = rec.character_name;
        p.className = rec.class_name;
        p.level = rec.level;
        p.faction = static_cast<uint8_t>(rec.faction);
        p.gender = static_cast<uint8_t>(rec.gender);
        p.hairstyle = static_cast<uint8_t>(rec.hairstyle);

        // Query equipped weapon/armor/hat visual indices
        try {
            pqxx::work txn(dbConn_.connection());
            auto rows = txn.exec_params(
                "SELECT item_id, equipped_slot FROM character_inventory "
                "WHERE character_id = $1 AND is_equipped = true "
                "AND equipped_slot IN ('Weapon', 'Armor', 'Hat')",
                rec.character_id);
            txn.commit();
            for (const auto& row : rows) {
                std::string itemId = row["item_id"].as<std::string>();
                std::string slot   = row["equipped_slot"].as<std::string>();
                uint16_t vi = itemDefCache_.getVisualIndex(itemId);
                if (slot == "Weapon")    p.weaponVisualIdx = vi;
                else if (slot == "Armor") p.armorVisualIdx = vi;
                else if (slot == "Hat")   p.hatVisualIdx = vi;
            }
        } catch (const std::exception& e) {
            LOG_ERROR("AuthServer", "Failed to query equip visuals for %s: %s",
                      rec.character_id.c_str(), e.what());
        }

        list.push_back(p);
    }
    return list;
}

// ---------------------------------------------------------------------------
// processRegister()
// ---------------------------------------------------------------------------
void AuthServer::processRegister(const RegisterRequest& req, AuthResponse& resp,
                                  int& outAccountId, AdminRole& outAdminRole) {
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
        resp.errorReason = "Invalid character name (1-10 letters and numbers only)";
        return;
    }
    if (ProfanityFilter::containsProfanity(req.characterName)) {
        resp.success = false;
        resp.errorReason = "Character name contains inappropriate language";
        return;
    }
    if (req.className != "Warrior" && req.className != "Mage" && req.className != "Archer") {
        resp.success = false;
        resp.errorReason = "Invalid class (must be Warrior, Mage, or Archer)";
        return;
    }

    // Hash password (slow ~250ms, NO lock)
    std::string hashedPw = hashPassword(req.password);
    if (hashedPw.empty()) {
        resp.success = false;
        resp.errorReason = "Internal error: password hashing failed";
        LOG_ERROR("AuthServer", "bcrypt hash failed for registration of '%s'", req.username.c_str());
        return;
    }

    // DB operations under lock
    int accountId;
    std::string charId;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);

        accountId = accountRepo_->createAccount(req.username, hashedPw, req.email);
        if (accountId < 0) {
            resp.success = false;
            resp.errorReason = "Username or email already exists";
            return;
        }

        charId = characterRepo_->createDefaultCharacter(accountId, req.characterName, req.className);
        if (charId.empty()) {
            resp.success = false;
            resp.errorReason = "Character name already taken";
            return;
        }
    }

    // Grant starter equipment (acquires its own lock)
    grantStarterEquipment(charId, req.className);

    // Store auth state for this connection
    outAccountId = accountId;
    outAdminRole = AdminRole::Player; // New accounts are always Player

    // Return character list (no token/session — that comes from SelectCharacter)
    resp.success = true;
    resp.characters = buildCharacterList(accountId);

    LOG_INFO("AuthServer", "Registered account '%s' (id=%d), character '%s'",
             req.username.c_str(), accountId, req.characterName.c_str());
}

// ---------------------------------------------------------------------------
// processLogin()
// ---------------------------------------------------------------------------
void AuthServer::processLogin(const LoginRequest& req, AuthResponse& resp,
                               int& outAccountId, AdminRole& outAdminRole) {
    // Phase 1: DB lookup (fast, under lock)
    decltype(accountRepo_->findByUsername(req.username)) account;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        account = accountRepo_->findByUsername(req.username);
    }
    if (!account) {
        resp.success = false;
        resp.errorReason = "Invalid username or password";
        return;
    }

    // Phase 2: bcrypt verify (slow ~250ms, NO lock — runs concurrently)
    if (!verifyPassword(req.password, account->password_hash)) {
        resp.success = false;
        resp.errorReason = "Invalid username or password";
        return;
    }

    // Check ban (with expiry support)
    if (account->is_banned) {
        if (!account->ban_expires_at.empty()) {
            // Timed ban -- check if expired
            try {
                std::lock_guard<std::mutex> lock(dbMutex_);
                pqxx::work txn(accountRepo_->connection());
                auto result = txn.exec_params(
                    "SELECT ban_expires_at < NOW() AS expired FROM accounts WHERE username = $1",
                    req.username);
                txn.commit();
                if (!result.empty() && result[0]["expired"].as<bool>()) {
                    accountRepo_->clearBanByUsername(req.username);
                    account->is_banned = false;
                    account->ban_expires_at.clear();
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Auth", "Ban expiry check failed: %s", e.what());
            }
        }
        if (account->is_banned) {
            resp.success = false;
            if (account->ban_expires_at.empty()) {
                resp.errorReason = "Account is permanently banned: " + account->ban_reason;
            } else {
                resp.errorReason = "Account is banned: " + account->ban_reason +
                                   " (expires: " + account->ban_expires_at + " UTC)";
            }
            return;
        }
    }

    // Check active
    if (!account->is_active) {
        resp.success = false;
        resp.errorReason = "Account is inactive";
        return;
    }

    // Update last login
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        accountRepo_->updateLastLogin(account->account_id);
    }

    // Store auth state for this connection
    outAccountId = account->account_id;
    outAdminRole = account->admin_role;

    // Return character list (no token/session — that comes from SelectCharacter)
    resp.success = true;
    resp.characters = buildCharacterList(account->account_id);

    LOG_INFO("AuthServer", "Login successful: '%s' (id=%d), %zu character(s)",
             req.username.c_str(), account->account_id, resp.characters.size());
}

// ---------------------------------------------------------------------------
// processCharacterCreate()
// ---------------------------------------------------------------------------
CharCreateResponse AuthServer::processCharacterCreate(const CharCreateRequest& req, int accountId) {
    CharCreateResponse resp;

    // Validate name
    if (!AuthValidation::isValidCharacterName(req.characterName)) {
        resp.errorMessage = "Invalid character name (1-10 alphanumeric)";
        return resp;
    }
    if (ProfanityFilter::containsProfanity(req.characterName)) {
        resp.errorMessage = "Character name contains inappropriate language";
        return resp;
    }

    // Validate class
    if (req.className != "Warrior" && req.className != "Mage" && req.className != "Archer") {
        resp.errorMessage = "Invalid class";
        return resp;
    }

    // Check slot count (max 3 characters)
    auto existing = buildCharacterList(accountId);
    if (existing.size() >= 3) {
        resp.errorMessage = "Maximum 3 characters per account";
        return resp;
    }

    // Create character (acquires lock internally via acquireConn)
    std::string charId;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        charId = characterRepo_->createDefaultCharacter(accountId, req.characterName, req.className);
    }
    if (charId.empty()) {
        resp.errorMessage = "Character name already taken";
        return resp;
    }

    // Set appearance (gender, hairstyle, faction)
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        pqxx::work txn(dbConn_.connection());
        txn.exec_params(
            "UPDATE characters SET gender = $1, hairstyle = $2, faction = $3 WHERE character_id = $4",
            static_cast<int>(req.gender), static_cast<int>(req.hairstyle),
            static_cast<int>(req.faction), charId);
        txn.commit();
    }

    // Grant starter equipment (acquires its own lock)
    grantStarterEquipment(charId, req.className);

    resp.success = true;
    resp.characters = buildCharacterList(accountId);

    LOG_INFO("AuthServer", "Created character '%s' (%s) for account %d — gender=%d hairstyle=%d faction=%d",
             req.characterName.c_str(), req.className.c_str(), accountId,
             req.gender, req.hairstyle, req.faction);
    return resp;
}

// ---------------------------------------------------------------------------
// processCharacterDelete()
// ---------------------------------------------------------------------------
CharDeleteResponse AuthServer::processCharacterDelete(const CharDeleteRequest& req, int accountId) {
    CharDeleteResponse resp;

    bool deleted;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        deleted = characterRepo_->deleteCharacter(req.characterId, accountId);
    }

    if (!deleted) {
        resp.errorMessage = "Character not found or does not belong to this account";
        return resp;
    }

    resp.success = true;
    resp.characters = buildCharacterList(accountId);

    LOG_INFO("AuthServer", "Deleted character '%s' for account %d",
             req.characterId.c_str(), accountId);
    return resp;
}

// ---------------------------------------------------------------------------
// processSelectCharacter()
// ---------------------------------------------------------------------------
SelectCharResponse AuthServer::processSelectCharacter(const SelectCharRequest& req,
                                                       int accountId, AdminRole adminRole) {
    SelectCharResponse resp;

    // Load full character data
    std::optional<CharacterRecord> charOpt;
    {
        std::lock_guard<std::mutex> lock(dbMutex_);
        charOpt = characterRepo_->loadCharacter(req.characterId);
    }

    if (!charOpt || charOpt->account_id != accountId) {
        resp.errorMessage = "Character not found";
        return resp;
    }

    const auto& rec = *charOpt;

    // Generate auth token + pending session (bridges TCP auth to UDP game connect)
    AuthToken token = generateAuthToken();

    auto now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    PendingSession session;
    session.account_id = accountId;
    session.character_id = rec.character_id;
    session.created_at = now;
    session.expires_at = now + 30.0; // 30 second window to connect via UDP
    session.admin_role = adminRole;

    pushResult(AuthResult{token, session});

    // Fill response with full character snapshot
    resp.success = true;
    resp.authToken = token;
    resp.characterName = rec.character_name;
    resp.className = rec.class_name;
    resp.sceneName = rec.current_scene.empty() ? "WhisperingWoods" : rec.current_scene;
    resp.spawnX = rec.position_x;
    resp.spawnY = rec.position_y;
    resp.level = rec.level;
    resp.currentXP = rec.current_xp;
    resp.gold = rec.gold;
    resp.currentHP = rec.current_hp;
    resp.maxHP = rec.max_hp;
    resp.currentMP = rec.current_mp;
    resp.maxMP = rec.max_mp;
    resp.currentFury = rec.current_fury;
    resp.honor = rec.honor;
    resp.pvpKills = rec.pvp_kills;
    resp.pvpDeaths = rec.pvp_deaths;
    resp.isDead = rec.is_dead ? 1 : 0;
    resp.faction = static_cast<uint8_t>(rec.faction);

    LOG_INFO("AuthServer", "Character selected: '%s' (id=%s) for account %d — gender=%d hairstyle=%d class=%s scene=%s",
             rec.character_name.c_str(), rec.character_id.c_str(), accountId,
             rec.gender, rec.hairstyle, rec.class_name.c_str(), rec.current_scene.c_str());
    return resp;
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
