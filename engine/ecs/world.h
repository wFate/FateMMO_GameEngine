#pragma once
#include "engine/ecs/entity.h"
#include "engine/ecs/entity_handle.h"
#include "engine/ecs/archetype.h"
#include "engine/ecs/command_buffer.h"
#include "engine/memory/arena.h"
#if defined(ENGINE_MEMORY_DEBUG)
#include "engine/memory/allocator_registry.h"
#endif
#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <cassert>

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
// Backed by archetype storage for cache-friendly component iteration
class World {
public:
    // Reserve sizes control initial std::vector capacity for slots_ and
    // archetypes_. The client (AOI-bounded, a few thousand entities at peak)
    // is fine with the defaults. The server (flat world, all players + mobs
    // across all scenes, cumulative index growth over days) should pass
    // larger values so slots_ doesn't resize-copy repeatedly and
    // archetypes_ never reallocates mid-migration. Memory cost is ~16 B per
    // slot reserved and ~sizeof(Archetype) per archetype slot, so even 65K
    // slots is only ~1 MB.
    explicit World(size_t slotReserve = 2048, size_t archetypeReserve = 4096);
    ~World();

    // Non-copyable, non-movable (owns Arena + ArchetypeStorage)
    World(const World&) = delete;
    World& operator=(const World&) = delete;

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

    // --- Archetype-backed component operations (called by Entity) ---

    // Type-erased component addition for serialization.
    // Returns pointer to zero-initialized memory. Caller constructs the component.
    void* addComponentById(EntityHandle handle, CompId id, size_t size, size_t alignment);

    // Add a component to an entity, migrating to a new archetype
    template<typename T, typename... Args>
    T* addComponentToEntity(Entity* entity, Args&&... args) {
        assert(iteratingDepth_ == 0 && "Structural changes during forEach are not allowed — use CommandBuffer");
        CompId cid = componentId<T>();
        archetypes_.registerType<T>();

        ArchetypeId oldArchId = entity->archetypeId_;
        RowIndex oldRow = entity->row_;

        // Migrate to new archetype with this component added
        ArchetypeId newArchId = archetypes_.migrateEntity(
            oldArchId, oldRow, entity->handle(), cid, true);

        // The migration may have caused swap-and-pop in the old archetype.
        // We need to update the entity that was swapped into oldRow.
        updateSwappedEntity(oldArchId, oldRow);

        // Find entity's new row (it's the last added in the new archetype)
        RowIndex newRow = archetypes_.entityCount(newArchId) - 1;
        entity->archetypeId_ = newArchId;
        entity->row_ = newRow;

        // Construct the new component with placement new
        T* column = archetypes_.getColumn<T>(newArchId);
        T* comp = &column[newRow];
        new (comp) T(std::forward<Args>(args)...);
        return comp;
    }

    // Get a component from an entity's archetype
    template<typename T>
    T* getComponentFromArchetype(const Entity* entity) const {
        if (entity->archetypeId_ == UINT32_MAX) return nullptr;
        CompId cid = componentId<T>();
        const auto& arch = archetypes_.getArchetype(entity->archetypeId_);
        if (!arch.hasType(cid)) return nullptr;
        T* column = const_cast<ArchetypeStorage&>(archetypes_).getColumn<T>(entity->archetypeId_);
        if (!column) return nullptr;
        return &column[entity->row_];
    }

    // Check if an entity has a component
    template<typename T>
    bool hasComponentInArchetype(const Entity* entity) const {
        if (entity->archetypeId_ == UINT32_MAX) return false;
        CompId cid = componentId<T>();
        return archetypes_.getArchetype(entity->archetypeId_).hasType(cid);
    }

    // Remove a component from an entity, migrating to a new archetype
    template<typename T>
    void removeComponentFromEntity(Entity* entity) {
        assert(iteratingDepth_ == 0 && "Structural changes during forEach are not allowed — use CommandBuffer");
        if (entity->archetypeId_ == UINT32_MAX) return;
        CompId cid = componentId<T>();
        const auto& arch = archetypes_.getArchetype(entity->archetypeId_);
        if (!arch.hasType(cid)) return;

        ArchetypeId oldArchId = entity->archetypeId_;
        RowIndex oldRow = entity->row_;

        ArchetypeId newArchId = archetypes_.migrateEntity(
            oldArchId, oldRow, entity->handle(), cid, false);

        updateSwappedEntity(oldArchId, oldRow);

        RowIndex newRow = archetypes_.entityCount(newArchId) - 1;
        entity->archetypeId_ = newArchId;
        entity->row_ = newRow;
    }

    // Get count of components for an entity
    size_t componentCountForEntity(const Entity* entity) const {
        if (entity->archetypeId_ == UINT32_MAX) return 0;
        return archetypes_.getArchetype(entity->archetypeId_).typeIds.size();
    }

    // Iterate components of an entity as type-erased pointers (expensive, for editor)
    void forEachComponentOfEntity(Entity* entity, const std::function<void(void*, CompId)>& fn);

    // --- Query entities with specific components (archetype iteration) ---
    template<typename T, typename Fn>
    void forEach(Fn&& fn) {
        ++iteratingDepth_;
        struct Guard { int& depth; ~Guard() { --depth; } } guard{iteratingDepth_};
        CompId typeId = componentId<T>();
        for (size_t i = 0; i < archetypes_.archetypeCount(); ++i) {
            ArchetypeId aid = static_cast<ArchetypeId>(i);
            const auto& arch = archetypes_.getArchetype(aid);
            if (arch.count == 0 || !arch.hasType(typeId)) continue;

            T* column = archetypes_.getColumn<T>(aid);
            EntityHandle* handles = archetypes_.getHandles(aid);

            for (uint32_t row = 0; row < arch.count; ++row) {
                Entity* entity = getEntity(handles[row]);
                if (!entity || !entity->isActive()) continue;
                T* comp = &column[row];
                if (comp->enabled) {
                    fn(entity, comp);
                }
            }
        }
    }

    // Query entities with two components
    template<typename T1, typename T2, typename Fn>
    void forEach(Fn&& fn) {
        ++iteratingDepth_;
        struct Guard { int& depth; ~Guard() { --depth; } } guard{iteratingDepth_};
        CompId cid1 = componentId<T1>();
        CompId cid2 = componentId<T2>();
        for (size_t i = 0; i < archetypes_.archetypeCount(); ++i) {
            ArchetypeId aid = static_cast<ArchetypeId>(i);
            const auto& arch = archetypes_.getArchetype(aid);
            if (arch.count == 0 || !arch.hasType(cid1) || !arch.hasType(cid2)) continue;

            T1* col1 = archetypes_.getColumn<T1>(aid);
            T2* col2 = archetypes_.getColumn<T2>(aid);
            EntityHandle* handles = archetypes_.getHandles(aid);

            for (uint32_t row = 0; row < arch.count; ++row) {
                Entity* entity = getEntity(handles[row]);
                if (!entity || !entity->isActive()) continue;
                T1* c1 = &col1[row];
                T2* c2 = &col2[row];
                if (c1->enabled && c2->enabled) {
                    fn(entity, c1, c2);
                }
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
        for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
            if (slots_[i].alive && slots_[i].entity) {
                fn(slots_[i].entity);
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

    // Cleanup destroyed entities (called at end of frame).
    // scope is an optional context tag emitted into the destroy log so we can
    // tell at a glance whether a batch came from the main tick, a disconnect
    // cleanup, a dungeon flush, etc. Must be a string literal or other
    // pointer with process-lifetime storage (not copied / not owned).
    void processDestroyQueue(const char* scope = nullptr);

    // Access to archetype storage (for advanced iteration)
    ArchetypeStorage& archetypes() { return archetypes_; }
    const ArchetypeStorage& archetypes() const { return archetypes_; }

private:
    struct EntitySlot {
        Entity* entity = nullptr;       // heap-allocated, stable pointer
        uint32_t generation = 1;        // starts at 1 so handle (0,0) is always invalid
        bool alive = false;
    };

    // After a swap-and-pop in an archetype, update the swapped entity's row_
    void updateSwappedEntity(ArchetypeId archId, RowIndex vacatedRow);

    Arena arena_;                                   // backing memory for archetype columns
    ArchetypeStorage archetypes_;                    // archetype storage
    ArchetypeId emptyArchetypeId_ = UINT32_MAX;     // archetype with no components (for new entities)
    std::vector<EntitySlot> slots_;
    std::vector<uint32_t> freeSlots_;
    std::vector<EntityHandle> destroyQueue_;
    std::vector<std::unique_ptr<System>> systems_;
    uint32_t nextSlotIndex_ = 1; // slot 0 is reserved (null handle)
    int iteratingDepth_ = 0;     // re-entrancy depth counter for forEach
};

} // namespace fate

// Include template implementations that need World to be fully defined
#include "engine/ecs/entity_inline.h"
