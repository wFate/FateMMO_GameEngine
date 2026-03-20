#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity_handle.h"
#include "engine/net/replication.h"
#include "server/db/spawn_zone_cache.h"
#include "server/db/definition_caches.h"
#include <vector>
#include <unordered_map>

namespace fate {

class ServerSpawnManager {
public:
    // Initialize: create mob entities for the given scene
    void initialize(const std::string& sceneId,
                    World& world,
                    ReplicationManager& replication,
                    const SpawnZoneCache& spawnZoneCache,
                    const MobDefCache& mobDefCache);

    // Tick: check for dead mobs, process respawn timers
    void tick(float dt, float gameTime, World& world, ReplicationManager& replication);

    // Called when a mob entity dies (from MobAISystem combat callback)
    void onMobDeath(EntityHandle handle, float gameTime);

    int totalMobs() const { return totalMobs_; }

private:
    struct TrackedMob {
        EntityHandle handle;
        int zoneRowIndex = -1;   // index into zoneRows_
        bool alive = true;
        float respawnAt = 0.0f;  // game time when respawn should occur
    };

    struct ZoneRowState {
        SpawnZoneRow config;
        const CachedMobDef* def = nullptr;
        int respawnSeconds = 30;
    };

    std::vector<ZoneRowState> zoneRows_;
    std::vector<TrackedMob> mobs_;
    int totalMobs_ = 0;

    // Creates a mob entity, registers it with replication, and returns its handle.
    // NOTE: also pushes a new TrackedMob onto mobs_ — callers that reuse an
    // existing slot (respawn path) must pop the duplicate and decrement totalMobs_.
    EntityHandle createMob(World& world, ReplicationManager& replication,
                           int zoneRowIndex, float gameTime);

    Vec2 randomPositionInZone(const SpawnZoneRow& zone);
};

} // namespace fate
