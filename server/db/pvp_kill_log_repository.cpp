#include "server/db/pvp_kill_log_repository.h"
#include "engine/core/logger.h"

namespace fate {

void PvPKillLogRepository::recordKill(const std::string& attackerId,
                                       const std::string& victimId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "INSERT INTO pvp_kill_log (attacker_id, victim_id) VALUES ($1, $2)",
            attackerId, victimId);
        txn.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("PvPKillLog", "recordKill failed: %s", e.what());
    }
}

int PvPKillLogRepository::countRecentKills(const std::string& playerA,
                                            const std::string& playerB) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT COUNT(*) FROM pvp_kill_log "
            "WHERE ((attacker_id = $1 AND victim_id = $2) "
            "    OR (attacker_id = $2 AND victim_id = $1)) "
            "  AND killed_at > NOW() - INTERVAL '1 hour'",
            playerA, playerB);
        txn.commit();
        if (!result.empty()) {
            return result[0][0].as<int>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("PvPKillLog", "countRecentKills failed: %s", e.what());
    }
    return 0;
}

int PvPKillLogRepository::pruneOldEntries() {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec(
            "DELETE FROM pvp_kill_log WHERE killed_at < NOW() - INTERVAL '2 hours'");
        txn.commit();
        return static_cast<int>(result.affected_rows());
    } catch (const std::exception& e) {
        LOG_ERROR("PvPKillLog", "pruneOldEntries failed: %s", e.what());
    }
    return 0;
}

} // namespace fate
