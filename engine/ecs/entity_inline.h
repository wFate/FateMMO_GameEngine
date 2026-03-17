#pragma once
// Entity template method implementations
// Included at the bottom of world.h after World is fully defined

namespace fate {

template<typename T, typename... Args>
T* Entity::addComponent(Args&&... args) {
    return world_->addComponentToEntity<T>(this, std::forward<Args>(args)...);
}

template<typename T>
T* Entity::getComponent() const {
    return world_->getComponentFromArchetype<T>(this);
}

template<typename T>
bool Entity::hasComponent() const {
    return world_->hasComponentInArchetype<T>(this);
}

template<typename T>
void Entity::removeComponent() {
    world_->removeComponentFromEntity<T>(this);
}

} // namespace fate
