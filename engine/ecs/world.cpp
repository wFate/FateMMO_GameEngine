#include "engine/ecs/world.h"
#include "engine/core/logger.h"
#include <algorithm>

namespace fate {

World::World() {
    // Reserve slot 0 as null sentinel
    slots_.resize(1);
    slots_[0].generation = 0;
    slots_[0].alive = false;
    slots_.reserve(1024);
}

World::~World() = default;

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
    slot.entity = std::make_unique<Entity>(slotIdx, name);
    slot.alive = true;

    EntityHandle handle(slotIdx, slot.generation);
    slot.entity->setHandle(handle);

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
    return slot.entity.get();
}

bool World::isAlive(EntityHandle handle) const {
    if (handle.isNull()) return false;
    uint32_t idx = handle.index();
    if (idx >= static_cast<uint32_t>(slots_.size())) return false;
    return slots_[idx].alive && slots_[idx].generation == handle.generation();
}

Entity* World::getEntity(EntityId id) const {
    // Legacy O(n) fallback — searches by entity id (slot index)
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->id() == id) {
            return slots_[i].entity.get();
        }
    }
    return nullptr;
}

Entity* World::findByName(const std::string& name) const {
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->name() == name)
            return slots_[i].entity.get();
    }
    return nullptr;
}

Entity* World::findByTag(const std::string& tag) const {
    for (uint32_t i = 1; i < static_cast<uint32_t>(slots_.size()); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->tag() == tag)
            return slots_[i].entity.get();
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

// --- Cleanup ---

void World::processDestroyQueue() {
    for (auto handle : destroyQueue_) {
        uint32_t idx = handle.index();
        if (idx >= static_cast<uint32_t>(slots_.size())) continue;
        auto& slot = slots_[idx];
        if (!slot.alive || slot.generation != handle.generation()) continue;

        if (slot.entity) {
            LOG_DEBUG("World", "Destroyed entity '%s' (idx=%u, gen=%u)",
                      slot.entity->name().c_str(), idx, slot.generation);
        }
        slot.entity.reset();
        slot.alive = false;
        // Bump generation for stale reference detection
        slot.generation = (slot.generation + 1) & EntityHandle::GEN_MASK;
        if (slot.generation == 0) slot.generation = 1; // skip 0
        freeSlots_.push_back(idx);
    }
    destroyQueue_.clear();
}

} // namespace fate
