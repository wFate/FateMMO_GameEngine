#pragma once
#include "engine/ecs/world.h"
#include "engine/core/types.h"
#include "engine/memory/arena.h"
#include <string>
#include <nlohmann/json.hpp>

namespace fate {

struct ZoneSnapshot;  // forward declare — defined in engine/memory/zone_snapshot.h

// A Scene represents a loaded game world with entities and systems
// Scenes can be loaded from JSON files or created programmatically
// Future: scenes will load from database instead of JSON
//
// Each Scene owns a zone arena (256 MB reserved) for zone-level allocations
// such as snapshot data. The World within the Scene has its own internal arena
// for archetype storage.
class Scene {
public:
    Scene(const std::string& name);
    ~Scene();

    const std::string& name() const { return name_; }
    World& world() { return world_; }

    // Zone arena — available for zone-level allocations (snapshot data, etc.)
    Arena& zoneArena() { return zoneArena_; }

    // Load scene definition from JSON file
    bool loadFromFile(const std::string& path);

    // Lifecycle
    void onEnter();   // called when scene becomes active
    void onExit();    // called when scene is being replaced

    // Snapshot — nullable pointer to a ZoneSnapshot in persistent storage.
    // The game layer is responsible for allocating the snapshot in a global
    // persistent arena and pointing this here before the scene exits.
    ZoneSnapshot* snapshot() const { return snapshot_; }
    void setSnapshot(ZoneSnapshot* snap) { snapshot_ = snap; }

    // Loading state queries
    bool isLoading() const { return isLoading_; }
    float loadProgress() const { return loadProgress_; }

    // Scene metadata (mirrors the 'scenes' database table)
    struct Metadata {
        std::string sceneId;
        std::string displayName;
        std::string sceneType = "zone";  // town, zone, dungeon, instance
        int minLevel = 1;
        int maxLevel = 99;
        bool pvpEnabled = false;
        bool isDungeon = false;
    };

    const Metadata& metadata() const { return metadata_; }

private:
    std::string name_;
    Arena zoneArena_;                       // 256 MB zone-level arena
    World world_;                           // ECS world (has its own 64 MB arena)
    Metadata metadata_;
    ZoneSnapshot* snapshot_ = nullptr;      // nullable; points to persistent storage
    bool isLoading_ = false;
    float loadProgress_ = 0.0f;

    // Entity factory: creates entity from JSON definition
    Entity* createEntityFromJson(const nlohmann::json& entityDef);
};

} // namespace fate
