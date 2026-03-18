#include "server/db/db_pool.h"
#include "engine/core/logger.h"

namespace fate {

bool DbPool::initialize(const Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        LOG_WARN("DbPool", "Already initialized");
        return true;
    }

    config_ = config;
    idle_.reserve(config_.maxConnections);

    // Eagerly create min connections
    for (int i = 0; i < config_.minConnections; ++i) {
        auto conn = createConnection();
        if (!conn) {
            LOG_ERROR("DbPool", "Failed to create initial connection %d/%d",
                      i + 1, config_.minConnections);
            if (i == 0) return false;  // can't even create one
            break;
        }
        idle_.push_back(std::move(conn));
    }

    initialized_ = true;
    LOG_INFO("DbPool", "Initialized with %d connections (min=%d, max=%d)",
             static_cast<int>(idle_.size()), config_.minConnections, config_.maxConnections);
    return true;
}

void DbPool::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    idle_.clear();
    totalCreated_ = 0;
    initialized_ = false;
    LOG_INFO("DbPool", "Shutdown complete");
}

std::unique_ptr<pqxx::connection> DbPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Try to reuse an idle connection
    while (!idle_.empty()) {
        auto conn = std::move(idle_.back());
        idle_.pop_back();

        // Verify it's still alive
        try {
            if (conn && conn->is_open()) {
                return conn;
            }
        } catch (...) {
            totalCreated_--;  // dead connection
        }
    }

    // No idle connections — create a new one if under max
    if (totalCreated_ < config_.maxConnections) {
        auto conn = createConnection();
        if (conn) return conn;
    }

    // At max capacity and none idle — log error.
    // In production this should block/wait, but for now just create one more
    // as a safety valve and log a warning.
    LOG_WARN("DbPool", "Pool exhausted (%d/%d), creating overflow connection",
             totalCreated_, config_.maxConnections);
    return createConnection();
}

void DbPool::release(std::unique_ptr<pqxx::connection> conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Check if connection is still usable
    try {
        if (conn->is_open()) {
            idle_.push_back(std::move(conn));
            return;
        }
    } catch (...) {}

    // Dead connection — don't return to pool
    totalCreated_--;
    LOG_WARN("DbPool", "Released dead connection, total now %d", totalCreated_);
}

int DbPool::idleCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(idle_.size());
}

int DbPool::activeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totalCreated_ - static_cast<int>(idle_.size());
}

bool DbPool::testConnection() {
    try {
        auto conn = acquire();
        if (!conn) return false;
        pqxx::work txn(*conn);
        txn.exec("SELECT 1");
        txn.commit();
        release(std::move(conn));
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("DbPool", "Test connection failed: %s", e.what());
        return false;
    }
}

std::unique_ptr<pqxx::connection> DbPool::createConnection() {
    try {
        auto conn = std::make_unique<pqxx::connection>(config_.connectionString);
        if (conn->is_open()) {
            totalCreated_++;
            LOG_INFO("DbPool", "Created connection #%d to %s", totalCreated_, conn->dbname());
            return conn;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("DbPool", "Failed to create connection: %s", e.what());
    }
    return nullptr;
}

} // namespace fate
