#include "engine/ecs/entity.h"

namespace fate {

Entity::Entity(EntityId id, const std::string& name)
    : id_(id), name_(name) {}

Entity::~Entity() = default;

} // namespace fate
