#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity_handle.h"
#include "engine/core/types.h"
#include "engine/net/replication.h"
#include "server/db/spawn_zone_cache.h"
#include "server/db/definition_caches.h"
#include "server/db/zone_mob_state_repository.h"
#include "engine/spatial/collision_grid.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace fate {

struct MobCreateParams {
    const CachedMobDef* def = nullptr;
    Vec2 position;
    int level = 1;
    std::string sceneId;
    float zoneRadius = 100.0f;  // for roamRadius calculation
};

class ServerSpawnManager {
public:
    // Initialize: create mob entities for the given scene
    void initialize(const std::string& sceneId,
                    World& world,
                    ReplicationManager& replication,
                    const SpawnZoneCache& spawnZoneCache,
                    const MobDefCache& mobDefCache,
                    const CollisionGrid* collisionGrid = nullptr,
                    ZoneMobStateRepository* mobStateRepo = nullptr);

    // Tick: check for dead mobs, process respawn timers
    void tick(float dt, float gameTime, World& world, ReplicationManager& replication);

    // Called when a mob entity dies (from MobAISystem combat callback)
    void onMobDeath(EntityHandle handle, float gameTime);

    // Persist all currently-dead mobs to DB before server shutdown
    void shutdown();

    int totalMobs() const { return totalMobs_; }

    // Create a mob entity from params — single source of truth for server-side mob creation.
    // Adds Transform, EnemyStatsComponent, MobAIComponent, MobNameplateComponent,
    // registers with ReplicationManager, and returns the EntityHandle.
    static EntityHandle createMobEntity(World& world,
                                        ReplicationManager& replication,
                                        const MobCreateParams& params);

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

    ZoneMobStateRepository* mobStateRepo_ = nullptr;
    std::string sceneId_;
    bool initializedTime_ = false;

    // Creates a mob entity, registers it with replication, and returns its handle.
    // NOTE: also pushes a new TrackedMob onto mobs_ — callers that reuse an
    // existing slot (respawn path) must pop the duplicate and decrement totalMobs_.
    EntityHandle createMob(World& world, ReplicationManager& replication,
                           int zoneRowIndex, float gameTime);

    const CollisionGrid* collisionGrid_ = nullptr;

    Vec2 randomPositionInZone(const SpawnZoneRow& zone, World& world);
};

} // namespace fate
