#include "engine/scene/scene.h"
#include "engine/ecs/prefab.h"
#include "engine/memory/zone_snapshot.h"
#include "engine/core/logger.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace fate {

Scene::Scene(const std::string& name)
    : name_(name),
      zoneArena_(256 * 1024 * 1024)   // 256 MB reserved for zone data
{
    metadata_.sceneId = name;
    metadata_.displayName = name;
}

Scene::~Scene() = default;

// ============================================================================
// Metadata helpers
// ============================================================================

nlohmann::json Scene::metadataToJson() const {
    nlohmann::json meta;
    meta["sceneId"]     = metadata_.sceneId;
    meta["displayName"] = metadata_.displayName;
    meta["sceneType"]   = metadata_.sceneType;
    meta["minLevel"]    = metadata_.minLevel;
    meta["maxLevel"]    = metadata_.maxLevel;
    meta["pvpEnabled"]  = metadata_.pvpEnabled;
    meta["isDungeon"]   = metadata_.isDungeon;
    return meta;
}

void Scene::metadataFromJson(const nlohmann::json& meta) {
    if (meta.contains("sceneId"))     metadata_.sceneId     = meta["sceneId"];
    if (meta.contains("displayName")) metadata_.displayName = meta["displayName"];
    if (meta.contains("sceneType"))   metadata_.sceneType   = meta["sceneType"];
    if (meta.contains("minLevel"))    metadata_.minLevel    = meta["minLevel"];
    if (meta.contains("maxLevel"))    metadata_.maxLevel    = meta["maxLevel"];
    if (meta.contains("pvpEnabled"))  metadata_.pvpEnabled  = meta["pvpEnabled"];
    if (meta.contains("isDungeon"))   metadata_.isDungeon   = meta["isDungeon"];
}

// ============================================================================
// Load — registry-based deserialization with version header
// ============================================================================

bool Scene::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Scene", "Cannot open scene file: %s", path.c_str());
        return false;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(file);

        // Read version (default to 1 for backward compat with pre-header files)
        int version = root.value("version", 1);
        if (version > SCENE_FORMAT_VERSION) {
            LOG_ERROR("Scene", "Scene file version %d is newer than supported (%d): %s",
                      version, SCENE_FORMAT_VERSION, path.c_str());
            return false;
        }

        // Scene name (optional override)
        if (root.contains("name")) {
            name_ = root["name"].get<std::string>();
        }

        // Load metadata
        if (root.contains("metadata")) {
            metadataFromJson(root["metadata"]);
        }

        // Load entities using registry-based deserialization
        if (root.contains("entities") && root["entities"].is_array()) {
            for (auto& entityDef : root["entities"]) {
                PrefabLibrary::jsonToEntity(entityDef, world_);
            }
        }

        LOG_INFO("Scene", "Loaded scene '%s' v%d from %s (%zu entities)",
                 name_.c_str(), version, path.c_str(), world_.entityCount());
        return true;

    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("Scene", "JSON parse error in %s: %s", path.c_str(), e.what());
        return false;
    }
}

// ============================================================================
// Save — registry-based serialization with version header
// ============================================================================

bool Scene::saveToFile(const std::string& path) const {
    nlohmann::json root;
    root["version"]  = SCENE_FORMAT_VERSION;
    root["name"]     = name_;
    root["metadata"] = metadataToJson();

    nlohmann::json entitiesJson = nlohmann::json::array();

    // Use a const_cast here because forEachEntity and entityToJson need
    // non-const access, but the save itself is logically const.
    World& w = const_cast<World&>(world_);
    w.forEachEntity([&](Entity* entity) {
        // Skip transient runtime entities — same filter as Editor::saveScene
        std::string tag = entity->tag();
        if (tag == "mob" || tag == "boss" || tag == "player" ||
            tag == "ghost" || tag == "dropped_item") return;

        entitiesJson.push_back(PrefabLibrary::entityToJson(entity));
    });

    root["entities"] = entitiesJson;

    // Ensure parent directory exists
    auto parentDir = fs::path(path).parent_path();
    if (!parentDir.empty() && !fs::exists(parentDir)) {
        fs::create_directories(parentDir);
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        LOG_ERROR("Scene", "Cannot write scene file: %s", path.c_str());
        return false;
    }

    out << root.dump(2);
    LOG_INFO("Scene", "Saved scene '%s' v%d to %s (%zu entities)",
             name_.c_str(), SCENE_FORMAT_VERSION, path.c_str(), world_.entityCount());
    return true;
}

// ============================================================================
// Lifecycle
// ============================================================================

void Scene::onEnter() {
    isLoading_ = true;
    loadProgress_ = 0.0f;

    LOG_INFO("Scene", "Entering scene: %s (%s)", metadata_.displayName.c_str(), name_.c_str());

    // Restore from snapshot if one exists
    if (snapshot_ && snapshot_->hasData()) {
        LOG_INFO("Scene", "Restoring zone snapshot for '%s' (%zu entities, %zu spawn zones)",
                 name_.c_str(), snapshot_->entities.size(), snapshot_->spawnZones.size());

        size_t restored = 0;
        for (const auto& entitySnap : snapshot_->entities) {
            if (entitySnap.entityJson.empty()) continue;

            // Skip player entities — they are created by the auth/connect flow
            if (entitySnap.entityTag == "player") continue;

            Entity* entity = PrefabLibrary::jsonToEntity(entitySnap.entityJson, world_);
            if (entity) {
                ++restored;
            }
        }
        LOG_INFO("Scene", "Restored %zu entities from snapshot", restored);
    }

    loadProgress_ = 1.0f;
    isLoading_ = false;
}

void Scene::onExit() {
    LOG_INFO("Scene", "Exiting scene: %s", name_.c_str());

    // Serialize non-player entities into snapshot for zone reload
    if (snapshot_) {
        snapshot_->clear();
        snapshot_->zoneName = name_;

        world_.forEachEntity([&](Entity* entity) {
            if (!entity || !entity->isActive()) return;
            // Skip player entities — they're managed by auth/connect flow
            if (entity->tag() == "player") return;

            EntitySnapshot snap;
            snap.entityName = entity->name();
            snap.entityTag = entity->tag();
            snap.entityJson = PrefabLibrary::entityToJson(entity);
            snapshot_->entities.push_back(std::move(snap));
        });

        LOG_INFO("Scene", "Saved %zu entities to snapshot for '%s'",
                 snapshot_->entities.size(), name_.c_str());
    }

    // Reset the zone arena — all zone-level allocations are freed in O(1)
    zoneArena_.reset();
}

} // namespace fate
