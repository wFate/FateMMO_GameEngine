#include "engine/ecs/world.h"
#include "engine/core/logger.h"
#include <algorithm>

namespace fate {

World::World()
    : arena_(64 * 1024 * 1024),  // 64 MB arena for archetype columns
      archetypes_(arena_)
{
    // Reserve slot 0 as null sentinel
    slots_.resize(1);
    slots_[0].generation = 0;
    slots_[0].alive = false;
    slots_.reserve(1024);

    // Create the empty archetype (no components) for newly created entities
    emptyArchetypeId_ = archetypes_.findOrCreateArchetype({});
}

World::~World() {
    // Destroy all archetype data first (calls destructors on components)
    archetypes_.destroyAll();

    // Delete all heap-allocated Entity objects
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].entity) {
            delete slots_[i].entity;
            slots_[i].entity = nullptr;
        }
    }
}

// --- Handle-based API ---

EntityHandle World::createEntityH(const std::string& name) {
    uint32_t slotIdx;
    if (!freeSlots_.empty()) {
        slotIdx = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        slotIdx = nextSlotIndex_++;
        if (slotIdx >= static_cast<uint32_t>(slots_.size())) {
            slots_.resize(slotIdx + 1);
        }
    }

    auto& slot = slots_[slotIdx];
    Entity* entity = new Entity(slotIdx, name);
    slot.entity = entity;
    slot.alive = true;

    EntityHandle handle(slotIdx, slot.generation);
    entity->setHandle(handle);
    entity->world_ = this;

    // Add entity to the empty archetype
    RowIndex row = archetypes_.addEntity(emptyArchetypeId_, handle);
    entity->archetypeId_ = emptyArchetypeId_;
    entity->row_ = row;

    if (name != "Tile") {
        LOG_DEBUG("World", "Created entity '%s' (idx=%u, gen=%u)",
                  name.c_str(), handle.index(), handle.generation());
    }
    return handle;
}

Entity* World::createEntity(const std::string& name) {
    EntityHandle h = createEntityH(name);
    return getEntity(h);
}

void World::destroyEntity(EntityHandle handle) {
    destroyQueue_.push_back(handle);
}

void World::destroyEntity(EntityId id) {
    // Legacy: linear scan to find handle
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->id() == id) {
            destroyQueue_.push_back(EntityHandle(i, slots_[i].generation));
            return;
        }
    }
}

Entity* World::getEntity(EntityHandle handle) const {
    if (handle.isNull()) return nullptr;
    uint32_t idx = handle.index();
    if (idx >= static_cast<uint32_t>(slots_.size())) return nullptr;
    auto& slot = slots_[idx];
    if (!slot.alive || slot.generation != handle.generation()) return nullptr;
    return slot.entity;
}

bool World::isAlive(EntityHandle handle) const {
    if (handle.isNull()) return false;
    uint32_t idx = handle.index();
    if (idx >= static_cast<uint32_t>(slots_.size())) return false;
    return slots_[idx].alive && slots_[idx].generation == handle.generation();
}

Entity* World::getEntity(EntityId id) const {
    // Legacy O(n) fallback -- searches by entity id (slot index)
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->id() == id) {
            return slots_[i].entity;
        }
    }
    return nullptr;
}

Entity* World::findByName(const std::string& name) const {
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->name() == name)
            return slots_[i].entity;
    }
    return nullptr;
}

Entity* World::findByTag(const std::string& tag) const {
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->tag() == tag)
            return slots_[i].entity;
    }
    return nullptr;
}

// --- Frame updates ---

void World::update(float deltaTime) {
    for (auto& sys : systems_) {
        if (sys->enabled) sys->update(deltaTime);
    }
}

void World::fixedUpdate(float fixedDeltaTime) {
    for (auto& sys : systems_) {
        if (sys->enabled) sys->fixedUpdate(fixedDeltaTime);
    }
}

void World::lateUpdate(float deltaTime) {
    for (auto& sys : systems_) {
        if (sys->enabled) sys->lateUpdate(deltaTime);
    }
}

// --- Stats ---

size_t World::entityCount() const {
    size_t count = 0;
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].alive) ++count;
    }
    return count;
}

// --- Internal: update swapped entity after swap-and-pop ---

void World::updateSwappedEntity(ArchetypeId archId, RowIndex vacatedRow) {
    // After migrateEntity removes from the source archetype via swap-and-pop,
    // if the removed row wasn't the last, another entity was swapped into that row.
    // We need to find that entity and update its row_ field.
    const auto& arch = archetypes_.getArchetype(archId);
    if (vacatedRow < arch.count) {
        // An entity was swapped into vacatedRow
        EntityHandle* handles = archetypes_.getHandles(archId);
        EntityHandle swappedHandle = handles[vacatedRow];
        Entity* swappedEntity = getEntity(swappedHandle);
        if (swappedEntity) {
            swappedEntity->row_ = vacatedRow;
        }
    }
}

// --- forEachComponent (expensive, for editor inspector) ---

void World::forEachComponentOfEntity(Entity* entity, const std::function<void(void*, CompId)>& fn) {
    if (!entity || entity->archetypeId_ == UINT32_MAX) return;

    const auto& arch = archetypes_.getArchetype(entity->archetypeId_);
    for (size_t colIdx = 0; colIdx < arch.columns.size(); ++colIdx) {
        const auto& col = arch.columns[colIdx];
        // Get the component data at this entity's row as type-erased pointer
        void* data = col.at(entity->row_);
        fn(data, col.typeId);
    }
}

// --- Cleanup ---

void World::processDestroyQueue() {
    for (auto handle : destroyQueue_) {
        uint32_t idx = handle.index();
        if (idx >= static_cast<uint32_t>(slots_.size())) continue;
        auto& slot = slots_[idx];
        if (!slot.alive || slot.generation != handle.generation()) continue;

        Entity* entity = slot.entity;
        if (entity) {
            LOG_DEBUG("World", "Destroyed entity '%s' (idx=%u, gen=%u)",
                      entity->name().c_str(), idx, slot.generation);

            // Remove from archetype storage (swap-and-pop)
            if (entity->archetypeId_ != UINT32_MAX) {
                ArchetypeId archId = entity->archetypeId_;
                RowIndex row = entity->row_;

                // Destroy component data at this row before removing
                const auto& arch = archetypes_.getArchetype(archId);
                for (size_t colIdx = 0; colIdx < arch.columns.size(); ++colIdx) {
                    auto& col = const_cast<ArchetypeColumn&>(arch.columns[colIdx]);
                    col.destroyRange(row, 1);
                }

                EntityHandle swapped = archetypes_.removeEntity(archId, row);

                // Update the entity that was swapped into the vacated row
                if (!swapped.isNull()) {
                    Entity* swappedEntity = getEntity(swapped);
                    if (swappedEntity) {
                        swappedEntity->row_ = row;
                    }
                }
            }

            delete entity;
        }
        slot.entity = nullptr;
        slot.alive = false;
        // Bump generation for stale reference detection
        slot.generation = (slot.generation + 1) & EntityHandle::GEN_MASK;
        if (slot.generation == 0) slot.generation = 1; // skip 0
        freeSlots_.push_back(idx);
    }
    destroyQueue_.clear();
}

// --- Entity non-template helpers ---

size_t Entity::componentCount() const {
    return world_->componentCountForEntity(this);
}

void Entity::forEachComponent(const std::function<void(void*, CompId)>& fn) {
    world_->forEachComponentOfEntity(this, fn);
}

} // namespace fate
