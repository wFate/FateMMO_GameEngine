#include "engine/ecs/world.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <cassert>
#include <string>

#ifdef FATE_HAS_GAME
#include "game/components/game_components.h"        // EnemyStatsComponent, NPCComponent
#include "game/components/dropped_item_component.h" // DroppedItemComponent
#include "game/components/player_controller.h"      // PlayerController
#endif // FATE_HAS_GAME

namespace fate {

World::World(size_t slotReserve, size_t archetypeReserve)
    // 256 MB arena. As of the Session 67 fix, archetype columns are heap-
    // allocated via operator new[] rather than bumped from this arena, so
    // it's currently unused. Kept at a generous reserve so any future arena
    // consumer (e.g. per-world scratch, command buffers) has room without
    // another round of OOM debugging. Virtual reserve on Win/Linux is free —
    // only pages we actually touch get physically committed.
    : arena_(256 * 1024 * 1024),
      archetypes_(arena_, archetypeReserve)
{
    // Reserve slot 0 as null sentinel
    slots_.resize(1);
    slots_[0].generation = 0;
    slots_[0].alive = false;
    slots_.reserve(slotReserve);

    // Create the empty archetype (no components) for newly created entities
    emptyArchetypeId_ = archetypes_.findOrCreateArchetype({});

#if defined(ENGINE_MEMORY_DEBUG)
    AllocatorRegistry::instance().add({
        .name = "WorldArena",
        .type = AllocatorType::Arena,
        .getUsed = [this]() -> size_t { return arena_.position(); },
        .getCommitted = [this]() -> size_t { return arena_.committed(); },
        .getReserved = [this]() -> size_t { return arena_.reserved(); },
    });
#endif
}

World::~World() {
#if defined(ENGINE_MEMORY_DEBUG)
    AllocatorRegistry::instance().remove("WorldArena");
#endif

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

// --- Type-erased component addition ---

void* World::addComponentById(EntityHandle handle, CompId id, size_t size, size_t alignment) {
    assert(iteratingDepth_ == 0 && "Structural changes during forEach are not allowed — use CommandBuffer");
    Entity* entity = getEntity(handle);
    if (!entity) return nullptr;

    // Register type metadata if not already known
    archetypes_.registerTypeById(id, size, alignment);

    ArchetypeId oldArchId = entity->archetypeId_;
    RowIndex oldRow = entity->row_;

    // Migrate to new archetype with this component added
    ArchetypeId newArchId = archetypes_.migrateEntity(
        oldArchId, oldRow, entity->handle(), id, true);

    // Update entity that was swapped into the old row
    updateSwappedEntity(oldArchId, oldRow);

    // Find entity's new row (it's the last added in the new archetype)
    RowIndex newRow = archetypes_.entityCount(newArchId) - 1;
    entity->archetypeId_ = newArchId;
    entity->row_ = newRow;

    // Return pointer to the zero-initialized component data
    // (addEntity already zero-inits all columns for the new row)
    void* columnBase = archetypes_.getColumnRaw(newArchId, id);
    if (!columnBase) return nullptr;
    size_t elemSize = archetypes_.getColumnElemSize(newArchId, id);
    return static_cast<uint8_t*>(columnBase) + static_cast<size_t>(newRow) * elemSize;
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

void World::processDestroyQueue(const char* scope) {
    // First pass: classify pending destroys by component type so the log line
    // can show a breakdown (e.g. "30 mobs, 5 loot, 2 other"). Without this,
    // batched destroys after a zone transition look like a scene teardown
    // when they are really a per-tick flush of dead mobs + expired loot.
    // We must tally BEFORE destroying — component storage is invalidated once
    // an entity is removed from its archetype below.
#ifdef FATE_HAS_GAME
    int mobCount = 0;
    int lootCount = 0;
    int playerCount = 0;
    int npcCount = 0;
    int otherCount = 0;
    for (auto handle : destroyQueue_) {
        uint32_t idx = handle.index();
        if (idx >= static_cast<uint32_t>(slots_.size())) { otherCount++; continue; }
        auto& slot = slots_[idx];
        if (!slot.alive || slot.generation != handle.generation()) continue;
        Entity* entity = slot.entity;
        if (!entity) { otherCount++; continue; }
        // Mutually-exclusive classification: pick the most specific role.
        // Mob check first — mobs are the noisiest category after zone transitions.
        if (entity->hasComponent<EnemyStatsComponent>()) {
            mobCount++;
        } else if (entity->hasComponent<DroppedItemComponent>()) {
            lootCount++;
        } else if (entity->hasComponent<PlayerController>()) {
            playerCount++;
        } else if (entity->hasComponent<NPCComponent>()) {
            npcCount++;
        } else {
            otherCount++;
        }
    }
#endif // FATE_HAS_GAME

    size_t destroyedCount = 0;
    for (auto handle : destroyQueue_) {
        uint32_t idx = handle.index();
        if (idx >= static_cast<uint32_t>(slots_.size())) continue;
        auto& slot = slots_[idx];
        if (!slot.alive || slot.generation != handle.generation()) continue;

        Entity* entity = slot.entity;
        if (entity) {
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
            destroyedCount++;
        }
        slot.entity = nullptr;
        slot.alive = false;
        // Bump generation for stale reference detection
        slot.generation = (slot.generation + 1) & EntityHandle::GEN_MASK;
        if (slot.generation == 0) slot.generation = 1; // skip 0
        freeSlots_.push_back(idx);
    }
    if (destroyedCount > 0) {
        // scope tag lets the reader know the intent of the destroy batch
        // (e.g. "tick_shared_world", "client_disconnect", "dungeon_tick",
        // "editor", "shutdown"). When no scope is supplied we log "unknown"
        // so any forgotten call site sticks out in the logs.
#ifdef FATE_HAS_GAME
        // Build "N mobs, N loot, N players, N npcs, N other" omitting zero
        // categories so the reader only sees relevant types.
        std::string breakdown;
        auto append = [&](const char* label, int n) {
            if (n <= 0) return;
            if (!breakdown.empty()) breakdown += ", ";
            breakdown += std::to_string(n);
            breakdown += ' ';
            breakdown += label;
        };
        append("mobs", mobCount);
        append("loot", lootCount);
        append("players", playerCount);
        append("npcs", npcCount);
        append("other", otherCount);
        LOG_DEBUG("World", "Destroyed %zu entities (scope=%s: %s)",
                  destroyedCount,
                  scope ? scope : "unknown",
                  breakdown.empty() ? "empty" : breakdown.c_str());
#else
        // Editor-only build (no game components linked) — plain count.
        LOG_DEBUG("World", "Destroyed %zu entities (scope=%s)",
                  destroyedCount, scope ? scope : "unknown");
#endif // FATE_HAS_GAME
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
