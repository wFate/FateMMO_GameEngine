#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include <pqxx/pqxx>
#include "server/db/db_pool.h"

namespace fate {

struct DeadMobRecord {
    std::string enemyId;
    int mobIndex = 0;
    int64_t diedAtUnix = 0;
    int respawnSeconds = 0;

    bool hasRespawned() const {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        return now >= (diedAtUnix + respawnSeconds);
    }

    float getRemainingRespawnTime() const {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        int64_t respawnAt = diedAtUnix + respawnSeconds;
        if (now >= respawnAt) return 0.0f;
        return static_cast<float>(respawnAt - now);
    }
};

class ZoneMobStateRepository {
public:
    // Legacy: direct connection (for temp repos in async fibers)
    explicit ZoneMobStateRepository(pqxx::connection& conn) : connRef_(&conn), pool_(nullptr) {}
    // Pool-based: acquires connection per operation
    explicit ZoneMobStateRepository(DbPool& pool) : connRef_(nullptr), pool_(&pool) {}

    bool saveZoneDeaths(const std::string& sceneName, const std::string& zoneName,
                        const std::vector<DeadMobRecord>& deadMobs);
    std::vector<DeadMobRecord> loadZoneDeaths(const std::string& sceneName,
                                               const std::string& zoneName);
    bool clearZoneDeaths(const std::string& sceneName, const std::string& zoneName);
    int cleanupExpiredDeaths();

private:
    pqxx::connection* connRef_ = nullptr;
    DbPool* pool_ = nullptr;

    DbPool::Guard acquireConn() {
        if (pool_) return pool_->acquire_guard();
        return DbPool::Guard::wrap(*connRef_);
    }
};

} // namespace fate
