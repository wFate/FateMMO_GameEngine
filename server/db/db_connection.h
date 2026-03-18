#pragma once
#include <string>
#include <memory>
#include <pqxx/pqxx>

namespace fate {

class DbConnection {
public:
    bool connect(const std::string& connectionString);
    void disconnect();
    bool isConnected() const;

    // Returns the raw pqxx connection for repository use.
    // Caller must not store this across threads.
    pqxx::connection& connection();

    // Reconnect with backoff (call on failure).
    bool reconnect();

private:
    std::unique_ptr<pqxx::connection> conn_;
    std::string connString_;
    int reconnectAttempts_ = 0;
    static constexpr int MAX_RECONNECT_ATTEMPTS = 3;
};

} // namespace fate
