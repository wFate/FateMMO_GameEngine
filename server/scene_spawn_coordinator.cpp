#include "server/scene_spawn_coordinator.h"
#include "engine/core/logger.h"

namespace fate {

void SceneSpawnCoordinator::initialize(const Config& config) {
    config_ = config;
    activeScenes_.clear();
    scenePlayerCounts_.clear();
    lastCleanupTime_ = 0.0f;
}

void SceneSpawnCoordinator::onPlayerEnterScene(const std::string& sceneId) {
    if (sceneId.empty()) return;
    int& count = scenePlayerCounts_[sceneId];
    ++count;
    if (count == 1) {
        activateScene(sceneId);
    }
}

void SceneSpawnCoordinator::onPlayerLeaveScene(const std::string& sceneId) {
    if (sceneId.empty()) return;
    auto it = scenePlayerCounts_.find(sceneId);
    if (it == scenePlayerCounts_.end() || it->second <= 0) return;
    --it->second;
    if (it->second <= 0) {
        deactivateScene(sceneId);
        scenePlayerCounts_.erase(it);
    }
}

void SceneSpawnCoordinator::tick(float dt, float gameTime) {
    for (auto& [sceneId, manager] : activeScenes_) {
        manager->tick(dt, gameTime, *config_.world, *config_.replication);
    }

    // Periodic cleanup of expired death records (every 5 minutes)
    if (config_.mobStateRepo && gameTime - lastCleanupTime_ > 300.0f) {
        int cleaned = config_.mobStateRepo->cleanupExpiredDeaths();
        if (cleaned > 0) {
            LOG_INFO("SpawnCoordinator", "Cleaned %d expired death records", cleaned);
        }
        lastCleanupTime_ = gameTime;
    }
}

ServerSpawnManager* SceneSpawnCoordinator::getManagerForScene(const std::string& sceneId) {
    auto it = activeScenes_.find(sceneId);
    return (it != activeScenes_.end()) ? it->second.get() : nullptr;
}

int SceneSpawnCoordinator::totalActiveMobs() const {
    int total = 0;
    for (const auto& [id, mgr] : activeScenes_) {
        total += mgr->totalMobs();
    }
    return total;
}

int SceneSpawnCoordinator::activeSceneCount() const {
    return static_cast<int>(activeScenes_.size());
}

void SceneSpawnCoordinator::activateScene(const std::string& sceneId) {
    if (activeScenes_.count(sceneId)) return;

    auto manager = std::make_unique<ServerSpawnManager>();

    const CollisionGrid* grid = nullptr;
    if (config_.collisionGrids) {
        auto it = config_.collisionGrids->find(sceneId);
        if (it != config_.collisionGrids->end()) {
            grid = &it->second;
        }
    }

    manager->initialize(sceneId, *config_.world, *config_.replication,
                        *config_.spawnZoneCache, *config_.mobDefCache,
                        grid, config_.mobStateRepo);

    LOG_INFO("SpawnCoordinator", "Activated scene '%s'", sceneId.c_str());
    activeScenes_[sceneId] = std::move(manager);
}

void SceneSpawnCoordinator::deactivateScene(const std::string& sceneId) {
    auto it = activeScenes_.find(sceneId);
    if (it == activeScenes_.end()) return;

    it->second->shutdown();
    activeScenes_.erase(it);

    LOG_INFO("SpawnCoordinator", "Deactivated scene '%s'", sceneId.c_str());
}

} // namespace fate
