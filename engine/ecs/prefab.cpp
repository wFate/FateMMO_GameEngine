#include "engine/ecs/prefab.h"
#include "engine/ecs/component_meta.h"
#include "engine/ecs/component_traits.h"
#include "engine/core/logger.h"

#include "engine/components/transform.h"  // needed by spawn() for position override
#include "engine/components/tile_layer_component.h"
#include "engine/components/sprite_component.h"  // needed for collision layer stripping

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace fate {

// Write JSON string to path atomically: write to .tmp, then rename over target.
static bool atomicWriteJson(const std::string& path, const std::string& jsonStr) {
    fs::path target(path);
    fs::path parentDir = target.parent_path();
    if (!parentDir.empty() && !fs::exists(parentDir))
        fs::create_directories(parentDir);

    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream file(tmp);
        if (!file.is_open()) return false;
        file << jsonStr;
        if (!file.good()) {
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        // rename can fail cross-device on some OS; fall back to copy+remove
        fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec);
        fs::remove(tmp);
        if (ec) return false;
    }
    return true;
}

// Reject names that could escape the prefab directory.
static bool isValidPrefabName(const std::string& name) {
    if (name.empty()) return false;
    if (name.find("..") != std::string::npos) return false;
    if (name.front() == '/' || name.front() == '\\') return false;
    if (name.find(':') != std::string::npos) return false;
    return true;
}

void PrefabLibrary::loadAll() {
    prefabs_.clear();
    variants_.clear();
    composedVariantCache_.clear();

    if (!fs::exists(directory_)) {
        fs::create_directories(directory_);
        LOG_INFO("Prefab", "Created prefab directory: %s", directory_.c_str());
        return;
    }

    for (auto& entry : fs::recursive_directory_iterator(directory_)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        // Use path relative to directory_ as the prefab name (e.g. "npc/npc_shopkeeper")
        fs::path relPath = fs::relative(entry.path(), directory_);
        std::string name = relPath.replace_extension("").generic_string();

        std::ifstream file(entry.path());
        if (!file.is_open()) continue;

        try {
            nlohmann::json data = nlohmann::json::parse(file);

            // Check if this is a variant (has parent_prefab + patches)
            if (data.contains("parent_prefab") && data.contains("patches")) {
                PrefabVariant variant;
                variant.name = name;
                variant.parentName = data["parent_prefab"].get<std::string>();
                variant.patches = data["patches"];
                variants_[name] = std::move(variant);
            } else {
                prefabs_[name] = std::move(data);
            }
        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR("Prefab", "Failed to parse %s: %s", name.c_str(), e.what());
        }
    }

    LOG_INFO("Prefab", "Loaded %zu prefabs, %zu variants from %s",
             prefabs_.size(), variants_.size(), directory_.c_str());
}

bool PrefabLibrary::save(const std::string& name, Entity* entity) {
    if (!entity) return false;
    if (!isValidPrefabName(name)) {
        LOG_ERROR("Prefab", "Invalid prefab name: '%s'", name.c_str());
        return false;
    }

    nlohmann::json data = entityToJson(entity);
    data["prefabName"] = name;

    std::string jsonStr = data.dump(2);

    // Atomic write to runtime directory
    std::string path = directory_ + "/" + name + ".json";
    if (!atomicWriteJson(path, jsonStr)) {
        LOG_ERROR("Prefab", "Cannot write to %s", path.c_str());
        return false;
    }

    // Atomic write to source directory (persists across rebuilds). The source
    // copy is the authoritative one — a successful runtime write that's NOT
    // mirrored to source will be silently overwritten on the next rebuild,
    // making it indistinguishable from a lost edit. So a source failure is
    // a hard save failure, not a soft one. Callers (e.g. the editor's
    // playerPrefabDirty flush) rely on the bool return to decide whether
    // to clear their dirty bit; lying here re-creates the source-write bug
    // that the UI save path already had to fix.
    if (!sourceDirectory_.empty() && sourceDirectory_ != directory_) {
        std::string srcPath = sourceDirectory_ + "/" + name + ".json";
        if (!atomicWriteJson(srcPath, jsonStr)) {
            LOG_ERROR("Prefab", "Source write FAILED: %s (runtime ok, source stale)",
                      srcPath.c_str());
            return false;
        }
    }

    // Cache it
    prefabs_[name] = std::move(data);

    LOG_INFO("Prefab", "Saved prefab '%s' to %s", name.c_str(), path.c_str());
    return true;
}

Entity* PrefabLibrary::spawn(const std::string& name, World& world, const Vec2& position) {
    nlohmann::json composedJson;

    auto it = prefabs_.find(name);
    if (it != prefabs_.end()) {
        composedJson = it->second;
    } else {
        // Check variants
        auto vit = variants_.find(name);
        if (vit == variants_.end()) {
            LOG_ERROR("Prefab", "Prefab '%s' not found", name.c_str());
            return nullptr;
        }

        const auto& variant = vit->second;
        auto pit = prefabs_.find(variant.parentName);
        if (pit == prefabs_.end()) {
            LOG_ERROR("Prefab", "Parent prefab '%s' not found for variant '%s'",
                      variant.parentName.c_str(), name.c_str());
            return nullptr;
        }

        composedJson = applyPrefabPatches(pit->second, variant.patches);
    }

    Entity* entity = jsonToEntity(composedJson, world);
    if (!entity) return nullptr;

#ifdef FATE_HAS_GAME
    // Override position
    auto* transform = entity->getComponent<Transform>();
    if (transform) {
        transform->position = position;
    }
#endif // FATE_HAS_GAME

    LOG_DEBUG("Prefab", "Spawned '%s' at (%.0f, %.0f)", name.c_str(), position.x, position.y);
    return entity;
}

bool PrefabLibrary::has(const std::string& name) const {
    return prefabs_.find(name) != prefabs_.end() ||
           variants_.find(name) != variants_.end();
}

std::vector<std::string> PrefabLibrary::names() const {
    std::vector<std::string> result;
    result.reserve(prefabs_.size() + variants_.size());
    for (auto& [name, _] : prefabs_) {
        result.push_back(name);
    }
    for (auto& [name, _] : variants_) {
        result.push_back(name);
    }
    return result;
}

const nlohmann::json* PrefabLibrary::getJson(const std::string& name) const {
    auto it = prefabs_.find(name);
    if (it != prefabs_.end()) return &it->second;

    // For variants, compose on first request and cache the result
    auto vit = variants_.find(name);
    if (vit != variants_.end()) {
        auto pit = prefabs_.find(vit->second.parentName);
        if (pit != prefabs_.end()) {
            nlohmann::json composed = applyPrefabPatches(pit->second, vit->second.patches);
            auto& self = const_cast<PrefabLibrary&>(*this);
            auto [inserted, _] = self.composedVariantCache_.emplace(name, std::move(composed));
            return &inserted->second;
        }
    }
    return nullptr;
}

bool PrefabLibrary::saveVariant(const std::string& variantName,
                                 const std::string& parentName,
                                 Entity* entity) {
    if (!entity) return false;
    if (!isValidPrefabName(variantName)) {
        LOG_ERROR("Prefab", "Invalid variant name: '%s'", variantName.c_str());
        return false;
    }

    auto pit = prefabs_.find(parentName);
    if (pit == prefabs_.end()) {
        LOG_ERROR("Prefab", "Parent prefab '%s' not found for variant '%s'",
                  parentName.c_str(), variantName.c_str());
        return false;
    }

    nlohmann::json entityJson = entityToJson(entity);
    nlohmann::json patches = computePrefabDiff(pit->second, entityJson);

    nlohmann::json variantFile;
    variantFile["parent_prefab"] = parentName;
    variantFile["patches"] = patches;

    std::string jsonStr = variantFile.dump(2);

    // Atomic write to runtime directory
    std::string path = directory_ + "/" + variantName + ".json";
    if (!atomicWriteJson(path, jsonStr)) {
        LOG_ERROR("Prefab", "Cannot write variant to %s", path.c_str());
        return false;
    }

    // Atomic write to source directory (persists across rebuilds)
    if (!sourceDirectory_.empty() && sourceDirectory_ != directory_) {
        std::string srcPath = sourceDirectory_ + "/" + variantName + ".json";
        atomicWriteJson(srcPath, jsonStr);
    }

    // Cache it
    PrefabVariant variant;
    variant.name = variantName;
    variant.parentName = parentName;
    variant.patches = std::move(patches);
    variants_[variantName] = std::move(variant);

    LOG_INFO("Prefab", "Saved variant '%s' (parent: '%s') to %s",
             variantName.c_str(), parentName.c_str(), path.c_str());
    return true;
}

bool PrefabLibrary::isVariant(const std::string& name) const {
    return variants_.find(name) != variants_.end();
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
        // Persist the enabled flag (from FATE_COMPONENT macro, first non-static member
        // at offset 0). Only write when false to keep JSON clean (default is true).
        bool enabled = true;
        std::memcpy(&enabled, ptr, sizeof(bool));
        if (!enabled) compJson["_enabled"] = false;
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
    // Validate before constructing — if components is present but isn't an
    // object (and isn't null, which entityToJson emits for component-less
    // entities), reject with nullptr rather than leaving a half-constructed
    // entity in the world.
    const bool hasComponents = data.contains("components");
    const bool componentsValid = !hasComponents
                                 || data["components"].is_null()
                                 || data["components"].is_object();
    if (!componentsValid) {
        LOG_ERROR("Prefab", "jsonToEntity: 'components' must be an object (got %s)",
                  data["components"].type_name());
        return nullptr;
    }

    std::string name = data.value("name", std::string("Entity"));
    Entity* entity = world.createEntity(name);
    entity->setTag(data.value("tag", std::string("")));
    entity->setActive(data.value("active", true));

    if (!hasComponents || data["components"].is_null()) return entity;

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
            // Restore enabled flag (FATE_COMPONENT macro, offset 0)
            if (compJson.contains("_enabled")) {
                bool enabled = compJson["_enabled"].get<bool>();
                std::memcpy(ptr, &enabled, sizeof(bool));
            }
        }
    }

#ifdef FATE_HAS_GAME
    // Backwards-compat: ground-tagged tiles without TileLayerComponent get default "ground" layer
    if (entity->tag() == "ground" && !entity->getComponent<TileLayerComponent>()) {
        auto* tlc = entity->addComponent<TileLayerComponent>();
        tlc->layer = "ground";
    }

    // Runtime: strip sprite from collision-layer tiles (invisible at runtime, editor-only visual)
#ifndef EDITOR_BUILD
    {
        auto* tlc = entity->getComponent<TileLayerComponent>();
        if (tlc && tlc->layer == "collision") {
            entity->removeComponent<SpriteComponent>();
        }
    }
#endif
#endif // FATE_HAS_GAME

    return entity;
}

} // namespace fate
