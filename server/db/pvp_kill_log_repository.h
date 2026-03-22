#pragma once
#include <string>
#include <cstdint>
#include <pqxx/pqxx>
#include "server/db/db_pool.h"

namespace fate {

class PvPKillLogRepository {
public:
    explicit PvPKillLogRepository(pqxx::connection& conn) : connRef_(&conn), pool_(nullptr) {}
    explicit PvPKillLogRepository(DbPool& pool) : connRef_(nullptr), pool_(&pool) {}

    /// Record a PvP kill to the database.
    void recordKill(const std::string& attackerId, const std::string& victimId);

    /// Count recent kills between two players (bidirectional) within the last hour.
    int countRecentKills(const std::string& playerA, const std::string& playerB);

    /// Delete entries older than 2 hours (only recent kills matter for tracking).
    int pruneOldEntries();

private:
    pqxx::connection* connRef_ = nullptr;
    DbPool* pool_ = nullptr;

    DbPool::Guard acquireConn() {
        if (pool_) return pool_->acquire_guard();
        return DbPool::Guard::wrap(*connRef_);
    }
};

} // namespace fate
