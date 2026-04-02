#pragma once
#include <string>
#include <vector>
#include <unordered_map>
namespace pqxx { class connection; }

namespace fate {

struct SpawnZoneRow {
    int zoneId = 0;
    std::string sceneId;
    std::string zoneName;
    std::string mobDefId;
    float centerX = 0, centerY = 0;
    float radius = 100;
    std::string zoneShape = "circle";
    int targetCount = 3;
    int respawnOverrideSeconds = -1;
};

class SpawnZoneCache {
public:
    bool initialize(pqxx::connection& conn);
    [[nodiscard]] const std::vector<SpawnZoneRow>& getZonesForScene(const std::string& sceneId) const;
    [[nodiscard]] int count() const;
    [[nodiscard]] const auto& allZones() const { return zonesByScene_; }
    void reload(pqxx::connection& conn) { initialize(conn); }

private:
    std::unordered_map<std::string, std::vector<SpawnZoneRow>> zonesByScene_;
    static const std::vector<SpawnZoneRow> empty_;
};

} // namespace fate
