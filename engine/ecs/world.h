#pragma once
#include "engine/ecs/entity.h"
#include "engine/ecs/entity_handle.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

namespace fate {

// System base class - systems contain the logic that operates on entities
class System {
public:
    virtual ~System() = default;
    virtual const char* name() const = 0;

    // Called once when added to world
    virtual void init(World* world) { world_ = world; }

    // Called every frame
    virtual void update(float deltaTime) {}

    // Called at fixed rate (for physics/networking)
    virtual void fixedUpdate(float fixedDeltaTime) {}

    // Called after all updates (for rendering prep)
    virtual void lateUpdate(float deltaTime) {}

    bool enabled = true;

protected:
    World* world_ = nullptr;
};

// The World manages all entities and systems
class World {
public:
    World();
    ~World();

    // --- Handle-based API (preferred) ---
    EntityHandle createEntityH(const std::string& name = "Entity");
    void destroyEntity(EntityHandle handle);
    Entity* getEntity(EntityHandle handle) const;
    bool isAlive(EntityHandle handle) const;

    // --- Legacy API (backward compat, delegates internally) ---
    Entity* createEntity(const std::string& name = "Entity");
    void destroyEntity(EntityId id);
    Entity* getEntity(EntityId id) const;
    Entity* findByName(const std::string& name) const;
    Entity* findByTag(const std::string& tag) const;

    // Query entities that have a specific component
    template<typename T>
    void forEach(const std::function<void(Entity*, T*)>& fn) {
        for (uint32_t i = 1; i < slots_.size(); ++i) {
            auto& slot = slots_[i];
            if (!slot.alive || !slot.entity || !slot.entity->isActive()) continue;
            T* comp = slot.entity->getComponent<T>();
            if (comp && comp->enabled) {
                fn(slot.entity.get(), comp);
            }
        }
    }

    // Query entities with two components
    template<typename T1, typename T2>
    void forEach(const std::function<void(Entity*, T1*, T2*)>& fn) {
        for (uint32_t i = 1; i < slots_.size(); ++i) {
            auto& slot = slots_[i];
            if (!slot.alive || !slot.entity || !slot.entity->isActive()) continue;
            T1* c1 = slot.entity->getComponent<T1>();
            T2* c2 = slot.entity->getComponent<T2>();
            if (c1 && c1->enabled && c2 && c2->enabled) {
                fn(slot.entity.get(), c1, c2);
            }
        }
    }

    // System management
    template<typename T, typename... Args>
    T* addSystem(Args&&... args) {
        auto sys = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = sys.get();
        sys->init(this);
        systems_.push_back(std::move(sys));
        return ptr;
    }

    template<typename T>
    T* getSystem() const {
        for (auto& sys : systems_) {
            T* cast = dynamic_cast<T*>(sys.get());
            if (cast) return cast;
        }
        return nullptr;
    }

    // Iterate ALL entities (for editor hierarchy)
    void forEachEntity(const std::function<void(Entity*)>& fn) {
        for (uint32_t i = 1; i < slots_.size(); ++i) {
            if (slots_[i].alive && slots_[i].entity) {
                fn(slots_[i].entity.get());
            }
        }
    }

    // Frame updates
    void update(float deltaTime);
    void fixedUpdate(float fixedDeltaTime);
    void lateUpdate(float deltaTime);

    // Stats
    size_t entityCount() const;
    size_t systemCount() const { return systems_.size(); }

    // Cleanup destroyed entities (called at end of frame)
    void processDestroyQueue();

private:
    struct EntitySlot {
        std::unique_ptr<Entity> entity;
        uint32_t generation = 1; // starts at 1 so handle (0,0) is always invalid
        bool alive = false;
    };

    std::vector<EntitySlot> slots_;
    std::vector<uint32_t> freeSlots_;
    std::vector<EntityHandle> destroyQueue_;
    std::vector<std::unique_ptr<System>> systems_;
    uint32_t nextSlotIndex_ = 1; // slot 0 is reserved (null handle)
};

} // namespace fate
