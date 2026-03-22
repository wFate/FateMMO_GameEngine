#pragma once
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <pqxx/pqxx>
#include "server/db/circuit_breaker.h"

namespace fate {

// ============================================================================
// DbPool — connection pool for PostgreSQL (libpqxx)
//
// Ported from Unity PostgresConnectionFactory.cs pool settings:
//   MinPoolSize=5, MaxPoolSize=50, CommandTimeout=30
//
// Usage:
//   auto conn = pool.acquire();   // blocks until available
//   pqxx::work txn(*conn);
//   txn.exec_params(...);
//   txn.commit();
//   pool.release(std::move(conn));
//
// Or with the RAII guard:
//   auto guard = pool.acquire_guard();
//   pqxx::work txn(guard.connection());
// ============================================================================

class DbPool {
public:
    struct Config {
        std::string connectionString;
        int minConnections = 5;
        int maxConnections = 50;
        int commandTimeoutSeconds = 30;
    };

    /// Initialize pool. Creates minConnections eagerly.
    /// Returns false if initial connections fail.
    bool initialize(const Config& config);

    /// Shut down pool. Closes all connections.
    void shutdown();

    /// Acquire a connection from the pool. Blocks if none available
    /// and pool is at max capacity. Creates a new one if below max.
    std::unique_ptr<pqxx::connection> acquire();

    /// Return a connection to the pool for reuse.
    void release(std::unique_ptr<pqxx::connection> conn);

    /// Current pool size (idle connections).
    int idleCount() const;

    /// Total connections currently checked out.
    int activeCount() const;

    /// Test connectivity of one pooled connection.
    bool testConnection();

    // RAII guard that auto-releases on destruction
    class Guard {
    public:
        Guard(DbPool& pool, std::unique_ptr<pqxx::connection> conn)
            : pool_(&pool), conn_(std::move(conn)) {}
        ~Guard() { if (conn_ && pool_) pool_->release(std::move(conn_)); }

        Guard(Guard&&) = default;
        Guard& operator=(Guard&&) = default;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        /// Returns true if a valid connection was acquired.
        explicit operator bool() const { return conn_ != nullptr || raw_ != nullptr; }

        pqxx::connection& connection() { return conn_ ? *conn_ : *raw_; }

        /// Wrap a raw connection reference (no pool release on destruction).
        /// Used by repos that already have a connection (e.g., temp repos in fibers).
        static Guard wrap(pqxx::connection& conn) {
            Guard g;
            g.raw_ = &conn;
            return g;
        }

    private:
        Guard() : pool_(nullptr), raw_(nullptr) {}
        DbPool* pool_ = nullptr;
        std::unique_ptr<pqxx::connection> conn_;
        pqxx::connection* raw_ = nullptr;
    };

    /// Acquire with RAII auto-release. Check guard with `if (guard)` before use —
    /// returns an empty guard (null connection) when the circuit breaker is open.
    Guard acquire_guard() { return Guard(*this, acquire()); }

    /// Advance the circuit breaker's notion of time (call from server tick).
    void updateBreakerTime(double t) {
        currentBreakerTime_ = t;
        breaker_.updateTime(t);
    }

    /// Expose breaker state for monitoring/logging.
    DbCircuitBreaker::State breakerState() const { return breaker_.state(); }

private:
    std::unique_ptr<pqxx::connection> createConnection();

    mutable std::mutex mutex_;
    std::condition_variable shutdownCv_;  // L29: signaled when active connections return
    std::vector<std::unique_ptr<pqxx::connection>> idle_;
    Config config_;
    int totalCreated_ = 0;
    int activeOut_ = 0;          // L29: connections currently checked out
    int overflowCreated_ = 0;    // L26: connections created beyond maxConnections
    static constexpr int kMaxOverflow = 10;  // L26: hard cap on overflow connections
    bool initialized_ = false;
    DbCircuitBreaker breaker_{5, 30.0}; // open after 5 consecutive failures, 30s cooldown
    double currentBreakerTime_ = 0.0;  // mirrors breaker_.currentTime_ for use in acquire/release
};

} // namespace fate
