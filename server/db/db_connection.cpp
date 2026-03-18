#include "server/db/db_connection.h"
#include "engine/core/logger.h"
#include <thread>
#include <chrono>

namespace fate {

bool DbConnection::connect(const std::string& connectionString) {
    connString_ = connectionString;
    try {
        conn_ = std::make_unique<pqxx::connection>(connString_);
        if (conn_->is_open()) {
            LOG_INFO("DB", "Connected to PostgreSQL: %s", conn_->dbname());
            reconnectAttempts_ = 0;
            return true;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("DB", "Connection failed: %s", e.what());
    }
    return false;
}

void DbConnection::disconnect() {
    if (conn_) {
        conn_->close();
        conn_.reset();
    }
}

bool DbConnection::isConnected() const {
    return conn_ && conn_->is_open();
}

pqxx::connection& DbConnection::connection() {
    return *conn_;
}

bool DbConnection::reconnect() {
    if (reconnectAttempts_ >= MAX_RECONNECT_ATTEMPTS) {
        LOG_ERROR("DB", "Max reconnect attempts (%d) reached", MAX_RECONNECT_ATTEMPTS);
        return false;
    }
    reconnectAttempts_++;
    int backoffMs = reconnectAttempts_ * 1000;
    LOG_WARN("DB", "Reconnecting in %dms (attempt %d/%d)",
             backoffMs, reconnectAttempts_, MAX_RECONNECT_ATTEMPTS);
    std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
    return connect(connString_);
}

} // namespace fate
