#include "server/db/zone_mob_state_repository.h"
#include "engine/core/logger.h"
#include <pqxx/pqxx>

namespace fate {

ZoneMobStateRepository::ZoneMobStateRepository(pqxx::connection& conn)
    : conn_(conn) {}

bool ZoneMobStateRepository::saveZoneDeaths(
    const std::string& sceneName, const std::string& zoneName,
    const std::vector<DeadMobRecord>& deadMobs) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM zone_mob_deaths WHERE scene_name = $1 AND zone_name = $2",
            sceneName, zoneName);
        for (const auto& rec : deadMobs) {
            txn.exec_params(
                "INSERT INTO zone_mob_deaths (scene_name, zone_name, enemy_id, mob_index, died_at_unix, respawn_seconds) "
                "VALUES ($1, $2, $3, $4, $5, $6)",
                sceneName, zoneName, rec.enemyId, rec.mobIndex,
                rec.diedAtUnix, rec.respawnSeconds);
        }
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ZoneMobState", "Failed to save zone deaths: %s", e.what());
        return false;
    }
}

std::vector<DeadMobRecord> ZoneMobStateRepository::loadZoneDeaths(
    const std::string& sceneName, const std::string& zoneName) {
    std::vector<DeadMobRecord> records;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT enemy_id, mob_index, died_at_unix, respawn_seconds "
            "FROM zone_mob_deaths WHERE scene_name = $1 AND zone_name = $2",
            sceneName, zoneName);
        txn.commit();
        for (const auto& row : result) {
            DeadMobRecord rec;
            rec.enemyId        = row["enemy_id"].as<std::string>();
            rec.mobIndex       = row["mob_index"].as<int>(0);
            rec.diedAtUnix     = row["died_at_unix"].as<int64_t>(0);
            rec.respawnSeconds = row["respawn_seconds"].as<int>(0);
            records.push_back(rec);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("ZoneMobState", "Failed to load zone deaths: %s", e.what());
    }
    return records;
}

bool ZoneMobStateRepository::clearZoneDeaths(
    const std::string& sceneName, const std::string& zoneName) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM zone_mob_deaths WHERE scene_name = $1 AND zone_name = $2",
            sceneName, zoneName);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ZoneMobState", "Failed to clear zone deaths: %s", e.what());
        return false;
    }
}

int ZoneMobStateRepository::cleanupExpiredDeaths() {
    try {
        int64_t now = static_cast<int64_t>(std::time(nullptr));
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "DELETE FROM zone_mob_deaths WHERE (died_at_unix + respawn_seconds) < $1",
            now);
        txn.commit();
        return static_cast<int>(result.affected_rows());
    } catch (const std::exception& e) {
        LOG_ERROR("ZoneMobState", "Failed to cleanup expired deaths: %s", e.what());
        return 0;
    }
}

} // namespace fate
