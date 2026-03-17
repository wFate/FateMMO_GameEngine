#include "engine/ecs/prefab.h"
#include "engine/ecs/component_meta.h"
#include "engine/ecs/component_traits.h"
#include "engine/core/logger.h"

#include "game/components/transform.h"  // needed by spawn() for position override

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace fate {

void PrefabLibrary::loadAll() {
    prefabs_.clear();

    if (!fs::exists(directory_)) {
        fs::create_directories(directory_);
        LOG_INFO("Prefab", "Created prefab directory: %s", directory_.c_str());
        return;
    }

    for (auto& entry : fs::directory_iterator(directory_)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        std::string name = entry.path().stem().string();
        std::ifstream file(entry.path());
        if (!file.is_open()) continue;

        try {
            nlohmann::json data = nlohmann::json::parse(file);
            prefabs_[name] = std::move(data);
        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR("Prefab", "Failed to parse %s: %s", name.c_str(), e.what());
        }
    }

    LOG_INFO("Prefab", "Loaded %zu prefabs from %s", prefabs_.size(), directory_.c_str());
}

bool PrefabLibrary::save(const std::string& name, Entity* entity) {
    if (!entity) return false;

    nlohmann::json data = entityToJson(entity);
    data["prefabName"] = name;

    std::string jsonStr = data.dump(2);

    // Write to runtime directory (where exe runs)
    if (!fs::exists(directory_)) fs::create_directories(directory_);
    std::string path = directory_ + "/" + name + ".json";
    {
        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("Prefab", "Cannot write to %s", path.c_str());
            return false;
        }
        file << jsonStr;
    }

    // Also write to source directory (persists across rebuilds)
    if (!sourceDirectory_.empty() && sourceDirectory_ != directory_) {
        if (!fs::exists(sourceDirectory_)) fs::create_directories(sourceDirectory_);
        std::string srcPath = sourceDirectory_ + "/" + name + ".json";
        std::ofstream srcFile(srcPath);
        if (srcFile.is_open()) {
            srcFile << jsonStr;
        }
    }

    // Cache it
    prefabs_[name] = std::move(data);

    LOG_INFO("Prefab", "Saved prefab '%s' to %s", name.c_str(), path.c_str());
    return true;
}

Entity* PrefabLibrary::spawn(const std::string& name, World& world, const Vec2& position) {
    auto it = prefabs_.find(name);
    if (it == prefabs_.end()) {
        LOG_ERROR("Prefab", "Prefab '%s' not found", name.c_str());
        return nullptr;
    }

    Entity* entity = jsonToEntity(it->second, world);
    if (!entity) return nullptr;

    // Override position
    auto* transform = entity->getComponent<Transform>();
    if (transform) {
        transform->position = position;
    }

    LOG_DEBUG("Prefab", "Spawned '%s' at (%.0f, %.0f)", name.c_str(), position.x, position.y);
    return entity;
}

bool PrefabLibrary::has(const std::string& name) const {
    return prefabs_.find(name) != prefabs_.end();
}

std::vector<std::string> PrefabLibrary::names() const {
    std::vector<std::string> result;
    result.reserve(prefabs_.size());
    for (auto& [name, _] : prefabs_) {
        result.push_back(name);
    }
    return result;
}

const nlohmann::json* PrefabLibrary::getJson(const std::string& name) const {
    auto it = prefabs_.find(name);
    return (it != prefabs_.end()) ? &it->second : nullptr;
}

// ============================================================================
// Serialization (shared format with editor scene save/load)
// ============================================================================

nlohmann::json PrefabLibrary::entityToJson(Entity* entity) {
    nlohmann::json data;
    data["name"] = entity->name();
    data["tag"] = entity->tag();
    data["active"] = entity->isActive();

    nlohmann::json comps;

    entity->forEachComponent([&](void* ptr, CompId id) {
        auto* meta = ComponentMetaRegistry::instance().findById(id);
        if (!meta || !meta->toJson) return;
        if (!hasFlag(meta->flags, ComponentFlags::Serializable)) return;

        nlohmann::json compJson;
        meta->toJson(ptr, compJson);
        comps[meta->name] = compJson;
    });

    // Preserve unknown components (types not registered at load time)
    for (auto& [name, blob] : entity->unknownComponents_) {
        comps[name] = blob;
    }

    data["components"] = comps;
    return data;
}

Entity* PrefabLibrary::jsonToEntity(const nlohmann::json& data, World& world) {
    std::string name = data.value("name", std::string("Entity"));
    Entity* entity = world.createEntity(name);
    entity->setTag(data.value("tag", std::string("")));
    entity->setActive(data.value("active", true));

    if (!data.contains("components")) return entity;

    for (auto& [typeName, compJson] : data["components"].items()) {
        auto* meta = ComponentMetaRegistry::instance().findByName(typeName);
        if (!meta) {
            // Unknown component -- preserve raw JSON for round-trip fidelity
            entity->unknownComponents_[typeName] = compJson;
            continue;
        }
        if (!meta->fromJson) continue;

        void* ptr = world.addComponentById(entity->handle(), meta->id, meta->size, meta->alignment);
        if (ptr) {
            if (meta->construct) meta->construct(ptr);
            meta->fromJson(compJson, ptr);
        }
    }

    return entity;
}

} // namespace fate
