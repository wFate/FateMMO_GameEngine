#include "engine/scene/scene.h"
#include "engine/core/logger.h"
#include <fstream>

namespace fate {

Scene::Scene(const std::string& name) : name_(name) {
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
    LOG_INFO("Scene", "Entering scene: %s (%s)", metadata_.displayName.c_str(), name_.c_str());
}

void Scene::onExit() {
    LOG_INFO("Scene", "Exiting scene: %s", name_.c_str());
}

} // namespace fate
