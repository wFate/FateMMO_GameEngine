#include "engine/ecs/prefab.h"
#include "engine/core/logger.h"
#include "engine/render/texture.h"

#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/player_controller.h"
#include "game/components/box_collider.h"
#include "game/components/polygon_collider.h"
#include "game/components/animator.h"

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
    nlohmann::json ej;
    ej["name"] = entity->name();
    ej["tag"] = entity->tag();

    nlohmann::json comps;

    if (auto* t = entity->getComponent<Transform>()) {
        comps["Transform"] = {
            {"position", {t->position.x, t->position.y}},
            {"scale", {t->scale.x, t->scale.y}},
            {"rotation", t->rotation},
            {"depth", t->depth}
        };
    }

    if (auto* s = entity->getComponent<SpriteComponent>()) {
        comps["Sprite"] = {
            {"texture", s->texturePath},
            {"size", {s->size.x, s->size.y}},
            {"tint", {s->tint.r, s->tint.g, s->tint.b, s->tint.a}},
            {"flipX", s->flipX},
            {"flipY", s->flipY}
        };
    }

    if (auto* c = entity->getComponent<BoxCollider>()) {
        comps["BoxCollider"] = {
            {"size", {c->size.x, c->size.y}},
            {"offset", {c->offset.x, c->offset.y}},
            {"isTrigger", c->isTrigger},
            {"isStatic", c->isStatic}
        };
    }

    if (auto* pc = entity->getComponent<PolygonCollider>()) {
        nlohmann::json pts = nlohmann::json::array();
        for (auto& p : pc->points) {
            pts.push_back({p.x, p.y});
        }
        comps["PolygonCollider"] = {
            {"points", pts},
            {"isTrigger", pc->isTrigger},
            {"isStatic", pc->isStatic}
        };
    }

    if (auto* p = entity->getComponent<PlayerController>()) {
        comps["PlayerController"] = {
            {"moveSpeed", p->moveSpeed},
            {"isLocalPlayer", p->isLocalPlayer}
        };
    }

    if (auto* a = entity->getComponent<Animator>()) {
        nlohmann::json anims;
        for (auto& [name, def] : a->animations) {
            anims[name] = {
                {"startFrame", def.startFrame},
                {"frameCount", def.frameCount},
                {"frameRate", def.frameRate},
                {"loop", def.loop}
            };
        }
        comps["Animator"] = {
            {"animations", anims},
            {"currentAnimation", a->currentAnimation}
        };
    }

    ej["components"] = comps;
    return ej;
}

Entity* PrefabLibrary::jsonToEntity(const nlohmann::json& data, World& world) {
    std::string name = data.value("name", "Entity");
    Entity* entity = world.createEntity(name);
    entity->setTag(data.value("tag", ""));

    if (!data.contains("components")) return entity;
    auto& comps = data["components"];

    if (comps.contains("Transform")) {
        auto& tj = comps["Transform"];
        auto* t = entity->addComponent<Transform>();
        if (tj.contains("position")) {
            auto p = tj["position"];
            t->position = {p[0].get<float>(), p[1].get<float>()};
        }
        if (tj.contains("scale")) {
            auto s = tj["scale"];
            t->scale = {s[0].get<float>(), s[1].get<float>()};
        }
        t->rotation = tj.value("rotation", 0.0f);
        t->depth = tj.value("depth", 0.0f);
    }

    if (comps.contains("Sprite")) {
        auto& sj = comps["Sprite"];
        auto* s = entity->addComponent<SpriteComponent>();
        s->texturePath = sj.value("texture", "");
        if (!s->texturePath.empty()) {
            s->texture = TextureCache::instance().load(s->texturePath);
        }
        // If texture failed to load, generate a magenta placeholder so it's visible
        if (!s->texture) {
            unsigned char px[] = {255, 0, 255, 255, 200, 0, 200, 255,
                                  200, 0, 200, 255, 255, 0, 255, 255};
            auto tex = std::make_shared<Texture>();
            tex->loadFromMemory(px, 2, 2, 4);
            s->texture = tex;
        }
        if (sj.contains("size")) {
            auto sz = sj["size"];
            s->size = {sz[0].get<float>(), sz[1].get<float>()};
        }
        if (sj.contains("tint")) {
            auto tn = sj["tint"];
            s->tint = {tn[0].get<float>(), tn[1].get<float>(),
                       tn[2].get<float>(), tn[3].get<float>()};
        }
        s->flipX = sj.value("flipX", false);
        s->flipY = sj.value("flipY", false);
    }

    if (comps.contains("BoxCollider")) {
        auto& cj = comps["BoxCollider"];
        auto* c = entity->addComponent<BoxCollider>();
        if (cj.contains("size")) {
            auto sz = cj["size"];
            c->size = {sz[0].get<float>(), sz[1].get<float>()};
        }
        if (cj.contains("offset")) {
            auto off = cj["offset"];
            c->offset = {off[0].get<float>(), off[1].get<float>()};
        }
        c->isTrigger = cj.value("isTrigger", false);
        c->isStatic = cj.value("isStatic", true);
    }

    if (comps.contains("PolygonCollider")) {
        auto& pj = comps["PolygonCollider"];
        auto* pc = entity->addComponent<PolygonCollider>();
        if (pj.contains("points")) {
            for (auto& pt : pj["points"]) {
                pc->points.push_back({pt[0].get<float>(), pt[1].get<float>()});
            }
        }
        pc->isTrigger = pj.value("isTrigger", false);
        pc->isStatic = pj.value("isStatic", true);
    }

    if (comps.contains("PlayerController")) {
        auto& pj = comps["PlayerController"];
        auto* p = entity->addComponent<PlayerController>();
        p->moveSpeed = pj.value("moveSpeed", 96.0f);
        p->isLocalPlayer = pj.value("isLocalPlayer", false);
    }

    if (comps.contains("Animator")) {
        auto& aj = comps["Animator"];
        auto* a = entity->addComponent<Animator>();
        if (aj.contains("animations")) {
            for (auto& [name, def] : aj["animations"].items()) {
                a->addAnimation(name,
                    def.value("startFrame", 0),
                    def.value("frameCount", 1),
                    def.value("frameRate", 8.0f),
                    def.value("loop", true));
            }
        }
        if (aj.contains("currentAnimation")) {
            a->play(aj["currentAnimation"].get<std::string>());
        }
    }

    return entity;
}

} // namespace fate
