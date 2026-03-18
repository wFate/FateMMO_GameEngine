#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace pqxx { class connection; }

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
    explicit ZoneMobStateRepository(pqxx::connection& conn);

    bool saveZoneDeaths(const std::string& sceneName, const std::string& zoneName,
                        const std::vector<DeadMobRecord>& deadMobs);
    std::vector<DeadMobRecord> loadZoneDeaths(const std::string& sceneName,
                                               const std::string& zoneName);
    bool clearZoneDeaths(const std::string& sceneName, const std::string& zoneName);
    int cleanupExpiredDeaths();

private:
    pqxx::connection& conn_;
};

} // namespace fate
