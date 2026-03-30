#pragma once

#include "server/server_spawn_manager.h"
#include "server/db/spawn_zone_cache.h"
#include "server/db/definition_caches.h"
#include "server/db/zone_mob_state_repository.h"
#include "engine/spatial/collision_grid.h"
#include "engine/ecs/world.h"
#include "engine/net/replication.h"
#include <unordered_map>
#include <memory>
#include <string>

namespace fate {

class SceneSpawnCoordinator {
public:
    struct Config {
        World* world = nullptr;
        ReplicationManager* replication = nullptr;
        const SpawnZoneCache* spawnZoneCache = nullptr;
        const MobDefCache* mobDefCache = nullptr;
        ZoneMobStateRepository* mobStateRepo = nullptr;
        const std::unordered_map<std::string, CollisionGrid>* collisionGrids = nullptr;
    };

    void initialize(const Config& config);

    void onPlayerEnterScene(const std::string& sceneId);
    void onPlayerLeaveScene(const std::string& sceneId);

    void tick(float dt, float gameTime);

    ServerSpawnManager* getManagerForScene(const std::string& sceneId);

    int totalActiveMobs() const;
    int activeSceneCount() const;

private:
    Config config_;
    std::unordered_map<std::string, std::unique_ptr<ServerSpawnManager>> activeScenes_;
    std::unordered_map<std::string, int> scenePlayerCounts_;
    float lastCleanupTime_ = 0.0f;

    void activateScene(const std::string& sceneId);
    void deactivateScene(const std::string& sceneId);
};

} // namespace fate
