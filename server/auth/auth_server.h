#pragma once
#include "engine/net/auth_protocol.h"
#include "server/db/db_connection.h"
#include "server/db/account_repository.h"
#include "server/db/character_repository.h"
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <string>
#include <cstdint>

// Forward declare OpenSSL types to avoid header pollution
typedef struct ssl_ctx_st SSL_CTX;

namespace fate {

struct AuthResult {
    AuthToken token;
    PendingSession session;
};

class AuthServer {
public:
    ~AuthServer();

    bool start(uint16_t port, const std::string& certPath, const std::string& keyPath,
               const std::string& dbConnectionString);
    void stop();

    // Thread-safe: called from game thread to drain completed auths
    bool popAuthResult(AuthResult& out);

private:
    static constexpr uintptr_t INVALID_SOCK = ~uintptr_t(0);

    std::atomic<bool> running_{false};
    std::thread listenThread_;

    // TLS
    SSL_CTX* sslCtx_ = nullptr;
    uintptr_t listenFd_ = INVALID_SOCK;
    uint16_t port_ = 0;

    // DB (auth thread only -- separate from game thread connection)
    DbConnection dbConn_;
    std::unique_ptr<AccountRepository> accountRepo_;
    std::unique_ptr<CharacterRepository> characterRepo_;

    // Thread-safe queue: auth thread pushes, game thread pops
    std::mutex queueMutex_;
    std::queue<AuthResult> resultQueue_;

    void listenLoop();
    void handleClient(uintptr_t clientFd);
    void processRegister(const RegisterRequest& req, AuthResponse& resp);
    void processLogin(const LoginRequest& req, AuthResponse& resp);
    void pushResult(const AuthResult& result);

    // bcrypt helpers
    std::string hashPassword(const std::string& password);
    bool verifyPassword(const std::string& password, const std::string& hash);
};

} // namespace fate
