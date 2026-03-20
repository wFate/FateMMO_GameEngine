#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <pqxx/pqxx>

namespace fate {

struct SpawnZoneRow {
    int zoneId = 0;
    std::string sceneId;
    std::string zoneName;
    std::string mobDefId;
    float centerX = 0, centerY = 0;
    float radius = 100;
    int targetCount = 3;
    int respawnOverrideSeconds = -1;
};

class SpawnZoneCache {
public:
    bool initialize(pqxx::connection& conn);
    [[nodiscard]] const std::vector<SpawnZoneRow>& getZonesForScene(const std::string& sceneId) const;
    [[nodiscard]] int count() const;

private:
    std::unordered_map<std::string, std::vector<SpawnZoneRow>> zonesByScene_;
    static const std::vector<SpawnZoneRow> empty_;
};

} // namespace fate
