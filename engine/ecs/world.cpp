#include "engine/ecs/world.h"
#include "engine/core/logger.h"
#include <algorithm>

namespace fate {

World::World() {
    entities_.reserve(1000);
}

World::~World() = default;

Entity* World::createEntity(const std::string& name) {
    EntityId id = nextId_++;
    auto entity = std::make_unique<Entity>(id, name);
    Entity* ptr = entity.get();
    entities_.push_back(std::move(entity));
    // Only log non-tile entities to avoid flooding console with 640 tile messages
    if (name != "Tile") {
        LOG_DEBUG("World", "Created entity '%s' (id=%u)", name.c_str(), id);
    }
    return ptr;
}

void World::destroyEntity(EntityId id) {
    destroyQueue_.push_back(id);
}

Entity* World::getEntity(EntityId id) const {
    for (auto& e : entities_) {
        if (e && e->id() == id) return e.get();
    }
    return nullptr;
}

Entity* World::findByName(const std::string& name) const {
    for (auto& e : entities_) {
        if (e && e->name() == name) return e.get();
    }
    return nullptr;
}

Entity* World::findByTag(const std::string& tag) const {
    for (auto& e : entities_) {
        if (e && e->tag() == tag) return e.get();
    }
    return nullptr;
}

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

size_t World::entityCount() const {
    size_t count = 0;
    for (auto& e : entities_) {
        if (e) count++;
    }
    return count;
}

void World::processDestroyQueue() {
    for (EntityId id : destroyQueue_) {
        auto it = std::find_if(entities_.begin(), entities_.end(),
            [id](const std::unique_ptr<Entity>& e) { return e && e->id() == id; });
        if (it != entities_.end()) {
            LOG_DEBUG("World", "Destroyed entity '%s' (id=%u)", (*it)->name().c_str(), id);
            it->reset();
        }
    }
    destroyQueue_.clear();

    // Compact: remove null slots periodically
    entities_.erase(
        std::remove_if(entities_.begin(), entities_.end(),
            [](const std::unique_ptr<Entity>& e) { return !e; }),
        entities_.end()
    );
}

} // namespace fate
