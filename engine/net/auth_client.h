#pragma once
#include "engine/net/auth_protocol.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

namespace fate {

class AuthClient {
public:
    ~AuthClient();

    // Non-blocking: spawns background thread for TLS connection
    void loginAsync(const std::string& host, uint16_t port,
                    const std::string& username, const std::string& password);

    void registerAsync(const std::string& host, uint16_t port,
                       const std::string& username, const std::string& password,
                       const std::string& email,
                       const std::string& characterName, const std::string& className);

    // Call from main thread each frame to check for result
    bool hasResult() const;
    AuthResponse consumeResult();

    bool isBusy() const { return busy_; }

private:
    std::atomic<bool> busy_{false};
    mutable std::mutex resultMutex_;
    std::optional<AuthResponse> result_;
    std::thread worker_;

    void doAuth(const std::string& host, uint16_t port,
                const std::vector<uint8_t>& requestData);

    void cleanup();
};

} // namespace fate
