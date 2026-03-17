#include "engine/scene/scene.h"
#include "engine/memory/zone_snapshot.h"
#include "engine/core/logger.h"
#include <fstream>

namespace fate {

Scene::Scene(const std::string& name)
    : name_(name),
      zoneArena_(256 * 1024 * 1024)   // 256 MB reserved for zone data
{
    metadata_.sceneId = name;
    metadata_.displayName = name;
}

Scene::~Scene() = default;

bool Scene::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Scene", "Cannot open scene file: %s", path.c_str());
        return false;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(file);

        // Load metadata
        if (root.contains("metadata")) {
            auto& meta = root["metadata"];
            if (meta.contains("sceneId"))     metadata_.sceneId = meta["sceneId"];
            if (meta.contains("displayName")) metadata_.displayName = meta["displayName"];
            if (meta.contains("sceneType"))   metadata_.sceneType = meta["sceneType"];
            if (meta.contains("minLevel"))    metadata_.minLevel = meta["minLevel"];
            if (meta.contains("maxLevel"))    metadata_.maxLevel = meta["maxLevel"];
            if (meta.contains("pvpEnabled"))  metadata_.pvpEnabled = meta["pvpEnabled"];
            if (meta.contains("isDungeon"))   metadata_.isDungeon = meta["isDungeon"];
        }

        // Load entities
        if (root.contains("entities") && root["entities"].is_array()) {
            for (auto& entityDef : root["entities"]) {
                createEntityFromJson(entityDef);
            }
        }

        LOG_INFO("Scene", "Loaded scene '%s' from %s (%zu entities)",
                 name_.c_str(), path.c_str(), world_.entityCount());
        return true;

    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Scene", "JSON parse error in %s: %s", path.c_str(), e.what());
        return false;
    }
}

Entity* Scene::createEntityFromJson(const nlohmann::json& def) {
    std::string name = def.value("name", "Entity");
    std::string tag = def.value("tag", "");

    Entity* entity = world_.createEntity(name);
    if (!tag.empty()) entity->setTag(tag);

    // Components are added by the game layer's component factory
    // The scene stores the raw JSON so the factory can interpret it
    // This keeps the engine layer generic and the game layer handles
    // specific component types (Transform, Sprite, PlayerController, etc.)

    // For now, store component data as a simple marker that game code reads
    // Game-specific scene loading happens in GameApp::onSceneLoaded()

    return entity;
}

void Scene::onEnter() {
    isLoading_ = true;
    loadProgress_ = 0.0f;

    LOG_INFO("Scene", "Entering scene: %s (%s)", metadata_.displayName.c_str(), name_.c_str());

    // Restore from snapshot if one exists
    if (snapshot_ && snapshot_->hasData()) {
        LOG_INFO("Scene", "Restoring zone snapshot for '%s' (%zu entities, %zu spawn zones)",
                 name_.c_str(), snapshot_->entities.size(), snapshot_->spawnZones.size());
        // TODO: actual deserialization — for now just acknowledge the snapshot exists
    }

    loadProgress_ = 1.0f;
    isLoading_ = false;
}

void Scene::onExit() {
    LOG_INFO("Scene", "Exiting scene: %s", name_.c_str());

    // Placeholder: save zone state to snapshot.
    // Full implementation will serialize spawn/mob state into snapshot_.
    // For now, just clear the world to release ECS resources.
    // (World destructor handles cleanup when Scene is destroyed.)

    // Reset the zone arena — all zone-level allocations are freed in O(1)
    zoneArena_.reset();
}

} // namespace fate
