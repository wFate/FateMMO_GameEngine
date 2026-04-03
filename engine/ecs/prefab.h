#pragma once
#include "engine/ecs/entity.h"
#include "engine/ecs/world.h"
#include "engine/ecs/prefab_variant.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

namespace fate {

// A Prefab is a reusable entity template stored as JSON.
// Create an entity in the editor, save it as a prefab, then stamp copies.
//
// Usage:
//   PrefabLibrary::instance().save("goblin", entity);      // save from editor
//   Entity* mob = PrefabLibrary::instance().spawn("goblin", world, position);
//
// Prefabs are stored in assets/prefabs/ as .json files.
// Future: prefab definitions can come from the database instead of JSON.

class PrefabLibrary {
public:
    static PrefabLibrary& instance() {
        static PrefabLibrary s_instance;
        return s_instance;
    }

    // Set the directory where prefab files live
    // sourceDir is the project source (persists across builds)
    void setDirectory(const std::string& dir) { directory_ = dir; }
    void setSourceDirectory(const std::string& dir) { sourceDirectory_ = dir; }

    // Scan the prefab directory and load all .json files
    void loadAll();

    // Save an entity as a prefab (serializes all components to JSON)
    bool save(const std::string& name, Entity* entity);

    // Spawn a copy of a prefab into the world at the given position
    Entity* spawn(const std::string& name, World& world, const Vec2& position);

    // Check if a prefab exists
    bool has(const std::string& name) const;

    // Get all prefab names
    std::vector<std::string> names() const;

    // Get the raw JSON for a prefab (for editor preview)
    const nlohmann::json* getJson(const std::string& name) const;

    // Save an entity as a variant of an existing prefab (stores only the diff)
    bool saveVariant(const std::string& variantName, const std::string& parentName,
                     Entity* entity);

    // Check if a name refers to a variant
    bool isVariant(const std::string& name) const;

    // Serialize/deserialize (used by editor for duplicate, scene save/load)
    static nlohmann::json entityToJson(Entity* entity);
    static Entity* jsonToEntity(const nlohmann::json& data, World& world);

private:
    PrefabLibrary() = default;

    std::string directory_ = "assets/prefabs";
    std::string sourceDirectory_; // project source path for persistent saves
    std::unordered_map<std::string, nlohmann::json> prefabs_;
    std::unordered_map<std::string, PrefabVariant> variants_;
    mutable std::unordered_map<std::string, nlohmann::json> composedVariantCache_;
};

} // namespace fate
