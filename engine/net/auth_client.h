#pragma once
#include "engine/net/auth_protocol.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>
#include <queue>
#include <condition_variable>

namespace fate {

// ---------------------------------------------------------------------------
// Command queue types — client pushes commands, worker thread processes them
// ---------------------------------------------------------------------------
enum class AuthCommandType { Login, Register, Create, Delete, Select, Disconnect };

struct AuthCommand {
    AuthCommandType type;
    std::string host;
    uint16_t port = 0;
    // Login/Register:
    std::string username, password, email;
    // Create:
    std::string characterName, className;
    uint8_t faction = 0, gender = 0, hairstyle = 0;
    // Delete/Select:
    std::string characterId;
};

// ---------------------------------------------------------------------------
// Result types — worker pushes results, main thread polls
// ---------------------------------------------------------------------------
enum class AuthResultType { Login, Create, Delete, Select };

struct AuthClientResult {
    AuthResultType type;
    bool success = false;
    std::string errorMessage;
    std::vector<CharacterPreview> characters; // for Login/Create/Delete
    SelectCharResponse selectData;            // for Select
};

// ---------------------------------------------------------------------------
// AuthClient — persistent TLS connection with command queue
// ---------------------------------------------------------------------------
class AuthClient {
public:
    ~AuthClient();

    // Initial connection — spawns worker thread with TLS setup
    void loginAsync(const std::string& host, uint16_t port,
                    const std::string& username, const std::string& password);
    void registerAsync(const std::string& host, uint16_t port,
                       const std::string& username, const std::string& password,
                       const std::string& email,
                       const std::string& characterName, const std::string& className,
                       uint8_t faction = 0, uint8_t gender = 0, uint8_t hairstyle = 0);

    // Commands on persistent connection (must be connected first)
    void createCharacterAsync(const std::string& name, const std::string& className,
                              uint8_t faction, uint8_t gender, uint8_t hairstyle);
    void deleteCharacterAsync(const std::string& characterId);
    void selectCharacterAsync(const std::string& characterId);
    void disconnectAuth();

    // Main-thread polling
    bool hasResult() const;
    AuthClientResult consumeResult();
    bool isBusy() const { return busy_; }

private:
    std::atomic<bool> busy_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> shouldStop_{false};

    mutable std::mutex resultMutex_;
    std::optional<AuthClientResult> result_;

    std::mutex cmdMutex_;
    std::condition_variable cmdCv_;
    std::queue<AuthCommand> cmdQueue_;

    std::thread worker_;

    void workerLoop(const std::string& host, uint16_t port,
                    const std::vector<uint8_t>& initialData);
    void pushResult(AuthClientResult result);
    void cleanup();
};

} // namespace fate
