#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/entity_handle.h"
#include "engine/ecs/archetype.h"
#include "engine/core/types.h"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <functional>

namespace fate {

class World;

class Entity {
public:
    Entity(EntityId id, const std::string& name = "Entity");
    ~Entity();

    EntityId id() const { return id_; }
    const std::string& name() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }

    const std::string& tag() const { return tag_; }
    void setTag(const std::string& tag) { tag_ = tag; }

    EntityHandle handle() const { return handle_; }
    void setHandle(EntityHandle h) { handle_ = h; }

    // Marks the entity as runtime-spawned (replicated from server, pet, etc).
    // Editor::saveScene skips replicated entities so they don't get baked into
    // scene .json files. Not serialized -- a fresh load always produces
    // isReplicated()==false because authored entities aren't replicated.
    bool isReplicated() const { return isReplicated_; }
    void setReplicated(bool r) { isReplicated_ = r; }

    // Component access -- delegates to archetype storage via World
    // Implementations are in entity_inline.h (included after World is defined)
    template<typename T, typename... Args>
    T* addComponent(Args&&... args);

    template<typename T>
    T* getComponent() const;

    template<typename T>
    bool hasComponent() const;

    template<typename T>
    void removeComponent();

    size_t componentCount() const;

    // Iterate components as type-erased pointers (for editor inspector)
    // This is expensive -- iterates archetype columns
    void forEachComponent(const std::function<void(void*, CompId)>& fn);

    // World accessor — returns the ECS world this entity lives in. Used by
    // server-side helpers that need to spawn additional entities into the
    // same world as a given entity (e.g. boss-script summonAdds, which must
    // route to the dungeon-instance world when the boss is in a dungeon and
    // to the main server world otherwise).
    World* world() const { return world_; }

    // Preserves JSON for components whose types weren't registered at load time.
    // Key = component type name string, Value = raw JSON object for that component.
    std::unordered_map<std::string, nlohmann::json> unknownComponents_;

private:
    friend class World;
    EntityId id_;
    EntityHandle handle_;
    std::string name_;
    std::string tag_;
    bool active_ = true;
    bool isReplicated_ = false;

    // Archetype location -- updated by World on migration/swap
    World* world_ = nullptr;
    ArchetypeId archetypeId_ = UINT32_MAX;
    RowIndex row_ = UINT32_MAX;
};

} // namespace fate
