#pragma once
#include "engine/ecs/component.h"
#include "engine/ecs/component_registry.h"
#include "engine/ecs/entity_handle.h"
#include "engine/ecs/archetype.h"
#include "engine/core/types.h"
#include <string>
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

    // Legacy: iterate components (for editor inspector)
    // This is expensive -- iterates archetype columns
    void forEachComponent(const std::function<void(Component*)>& fn);

private:
    friend class World;
    EntityId id_;
    EntityHandle handle_;
    std::string name_;
    std::string tag_;
    bool active_ = true;

    // Archetype location -- updated by World on migration/swap
    World* world_ = nullptr;
    ArchetypeId archetypeId_ = UINT32_MAX;
    RowIndex row_ = UINT32_MAX;
};

} // namespace fate
