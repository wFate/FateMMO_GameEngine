#pragma once
#include "engine/net/auth_protocol.h"
#include "server/db/db_connection.h"
#include "server/db/account_repository.h"
#include "server/db/character_repository.h"
#include "server/cache/item_definition_cache.h"
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
    static constexpr int MAX_CONCURRENT_CLIENTS = 8;
    static constexpr int CLIENT_TIMEOUT_SECONDS = 300; // 5 minutes for persistent connections

    std::atomic<bool> running_{false};
    std::atomic<int> activeClients_{0};
    std::thread listenThread_;

    // TLS
    SSL_CTX* sslCtx_ = nullptr;
    uintptr_t listenFd_ = INVALID_SOCK;
    uint16_t port_ = 0;

    // DB (shared across handler threads — protected by dbMutex_)
    DbConnection dbConn_;
    std::mutex dbMutex_;
    std::unique_ptr<AccountRepository> accountRepo_;
    std::unique_ptr<CharacterRepository> characterRepo_;
    ItemDefinitionCache itemDefCache_;

    // Thread-safe queue: auth thread pushes, game thread pops
    std::mutex queueMutex_;
    std::queue<AuthResult> resultQueue_;

    void listenLoop();
    void handleClient(uintptr_t clientFd);
    void processRegister(const RegisterRequest& req, AuthResponse& resp,
                         int& outAccountId, AdminRole& outAdminRole);
    void processLogin(const LoginRequest& req, AuthResponse& resp,
                      int& outAccountId, AdminRole& outAdminRole);
    void pushResult(const AuthResult& result);

    // Character management helpers
    void grantStarterEquipment(const std::string& charId, const std::string& className);
    std::vector<CharacterPreview> buildCharacterList(int accountId);

    // Character management handlers (require authenticated connection)
    CharCreateResponse processCharacterCreate(const CharCreateRequest& req, int accountId);
    CharDeleteResponse processCharacterDelete(const CharDeleteRequest& req, int accountId);
    SelectCharResponse processSelectCharacter(const SelectCharRequest& req, int accountId, AdminRole adminRole);

    // bcrypt helpers
    std::string hashPassword(const std::string& password);
    bool verifyPassword(const std::string& password, const std::string& hash);
};

} // namespace fate
