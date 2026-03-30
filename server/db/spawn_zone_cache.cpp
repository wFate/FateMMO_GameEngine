#include "server/db/spawn_zone_cache.h"
#include "engine/core/logger.h"

namespace fate {

const std::vector<SpawnZoneRow> SpawnZoneCache::empty_;

bool SpawnZoneCache::initialize(pqxx::connection& conn) {
    try {
        pqxx::work txn(conn);
        auto result = txn.exec(
            "SELECT zone_id, scene_id, zone_name, mob_def_id, "
            "center_x, center_y, radius, zone_shape, target_count, respawn_override_seconds "
            "FROM spawn_zones ORDER BY scene_id, zone_id");
        txn.commit();

        zonesByScene_.clear();
        for (const auto& row : result) {
            SpawnZoneRow z;
            z.zoneId     = row["zone_id"].as<int>();
            z.sceneId    = row["scene_id"].as<std::string>();
            z.zoneName   = row["zone_name"].is_null() ? "" : row["zone_name"].as<std::string>();
            z.mobDefId   = row["mob_def_id"].as<std::string>();
            z.centerX    = row["center_x"].as<float>();
            z.centerY    = row["center_y"].as<float>();
            z.radius     = row["radius"].as<float>();
            z.zoneShape  = row["zone_shape"].as<std::string>("circle");
            z.targetCount = row["target_count"].is_null() ? 3 : row["target_count"].as<int>();
            z.respawnOverrideSeconds = row["respawn_override_seconds"].is_null()
                ? -1 : row["respawn_override_seconds"].as<int>();
            zonesByScene_[z.sceneId].push_back(std::move(z));
        }

        int total = 0;
        for (auto& [scene, zones] : zonesByScene_) total += (int)zones.size();
        LOG_INFO("SpawnZoneCache", "Loaded %d spawn zone rules across %d scenes",
                 total, (int)zonesByScene_.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SpawnZoneCache", "Failed to load: %s", e.what());
        return false;
    }
}

const std::vector<SpawnZoneRow>& SpawnZoneCache::getZonesForScene(const std::string& sceneId) const {
    auto it = zonesByScene_.find(sceneId);
    return it != zonesByScene_.end() ? it->second : empty_;
}

int SpawnZoneCache::count() const {
    int total = 0;
    for (auto& [s, zones] : zonesByScene_) total += (int)zones.size();
    return total;
}

} // namespace fate
