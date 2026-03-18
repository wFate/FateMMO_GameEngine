#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <pqxx/pqxx>

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
            : pool_(pool), conn_(std::move(conn)) {}
        ~Guard() { if (conn_) pool_.release(std::move(conn_)); }

        Guard(Guard&&) = default;
        Guard& operator=(Guard&&) = default;
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        pqxx::connection& connection() { return *conn_; }

    private:
        DbPool& pool_;
        std::unique_ptr<pqxx::connection> conn_;
    };

    /// Acquire with RAII auto-release.
    Guard acquire_guard() { return Guard(*this, acquire()); }

private:
    std::unique_ptr<pqxx::connection> createConnection();

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<pqxx::connection>> idle_;
    Config config_;
    int totalCreated_ = 0;
    bool initialized_ = false;
};

} // namespace fate
