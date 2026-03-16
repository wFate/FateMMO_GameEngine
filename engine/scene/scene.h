#pragma once
#include "engine/ecs/world.h"
#include "engine/core/types.h"
#include <string>
#include <nlohmann/json.hpp>

namespace fate {

// A Scene represents a loaded game world with entities and systems
// Scenes can be loaded from JSON files or created programmatically
// Future: scenes will load from database instead of JSON
class Scene {
public:
    Scene(const std::string& name);
    ~Scene();

    const std::string& name() const { return name_; }
    World& world() { return world_; }

    // Load scene definition from JSON file
    bool loadFromFile(const std::string& path);

    // Lifecycle
    void onEnter();   // called when scene becomes active
    void onExit();    // called when scene is being replaced

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
    World world_;
    Metadata metadata_;

    // Entity factory: creates entity from JSON definition
    Entity* createEntityFromJson(const nlohmann::json& entityDef);
};

} // namespace fate
