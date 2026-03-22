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
    std::unique_lock<std::mutex> lock(mutex_);

    // L29: Wait up to 5 seconds for active connections to be returned
    if (activeOut_ > 0) {
        LOG_INFO("DbPool", "Shutdown waiting for %d active connection(s)...", activeOut_);
        bool drained = shutdownCv_.wait_for(lock, std::chrono::seconds(5),
                                             [this]{ return activeOut_ <= 0; });
        if (!drained) {
            LOG_WARN("DbPool", "Shutdown timed out with %d connection(s) still active", activeOut_);
        }
    }

    idle_.clear();
    totalCreated_ = 0;
    activeOut_ = 0;
    overflowCreated_ = 0;
    initialized_ = false;
    LOG_INFO("DbPool", "Shutdown complete");
}

std::unique_ptr<pqxx::connection> DbPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Circuit breaker check — reject immediately if the DB is known to be down
    if (!breaker_.allowRequest()) {
        LOG_WARN("DbPool", "Circuit breaker OPEN — rejecting acquire request");
        return nullptr;
    }

    // Try to reuse an idle connection
    while (!idle_.empty()) {
        auto conn = std::move(idle_.back());
        idle_.pop_back();

        // Verify it's still alive
        try {
            if (conn && conn->is_open()) {
                activeOut_++;
                return conn;
            }
        } catch (const pqxx::broken_connection& e) {
            LOG_WARN("DbPool", "Idle connection broken during acquire: %s", e.what());
            breaker_.recordFailure(currentBreakerTime_);
            totalCreated_--;
        } catch (...) {
            totalCreated_--;  // dead connection
        }
    }

    // No idle connections — create a new one if under max
    if (totalCreated_ < config_.maxConnections) {
        auto conn = createConnection();
        if (conn) {
            activeOut_++;
            return conn;
        }
        // createConnection failed — record the failure
        breaker_.recordFailure(currentBreakerTime_);
    }

    // L26: At max capacity — allow limited overflow with hard cap
    if (overflowCreated_ < kMaxOverflow) {
        LOG_WARN("DbPool", "Pool exhausted (%d/%d), creating overflow connection (%d/%d)",
                 totalCreated_, config_.maxConnections, overflowCreated_ + 1, kMaxOverflow);
        auto conn = createConnection();
        if (conn) {
            overflowCreated_++;
            activeOut_++;
            return conn;
        }
    }

    LOG_ERROR("DbPool", "Pool fully exhausted including overflow (%d+%d connections), "
              "rejecting acquire", config_.maxConnections, overflowCreated_);
    return nullptr;
}

void DbPool::release(std::unique_ptr<pqxx::connection> conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(mutex_);
    activeOut_--;

    // Check if connection is still usable
    try {
        if (conn->is_open()) {
            breaker_.recordSuccess();
            idle_.push_back(std::move(conn));
            shutdownCv_.notify_one();  // L29: wake shutdown if waiting
            return;
        }
    } catch (const pqxx::broken_connection& e) {
        LOG_WARN("DbPool", "Broken connection on release: %s", e.what());
        breaker_.recordFailure(currentBreakerTime_);
    } catch (...) {}

    // Dead connection — don't return to pool
    totalCreated_--;
    LOG_WARN("DbPool", "Released dead connection, total now %d", totalCreated_);
    shutdownCv_.notify_one();  // L29: wake shutdown if waiting
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
            // L27: Set query timeout (PostgreSQL uses milliseconds)
            if (config_.commandTimeoutSeconds > 0) {
                conn->set_variable("statement_timeout",
                    std::to_string(config_.commandTimeoutSeconds * 1000));
            }
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
