#pragma once
// Entity template method implementations
// Included at the bottom of world.h after World is fully defined

#if FATE_ENABLE_HOT_RELOAD
#include "engine/module/hot_reload_hook_decl.h"
#endif
#include <string_view>

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
#if FATE_ENABLE_HOT_RELOAD
    // Eager onDestroy hook for BehaviorComponent removal — the per-tick
    // sweep would catch this on the next tick, but only if tickBehaviors
    // runs (editor pause skips it). Notify BEFORE archetype migration so
    // the manager's destroyOne path can still re-resolve `bc->state`.
    //
    // Compile-time-resolved via constexpr name compare; folds to nothing for
    // any T other than BehaviorComponent. No header dependency on
    // behavior_component.h here — we only check the static name string the
    // FATE_COMPONENT macro stamps on every component.
    if constexpr (std::string_view(T::COMPONENT_NAME) == std::string_view("BehaviorComponent")) {
        if (world_) hotReloadNotifyBehaviorComponentRemoved(*world_, handle_);
    }
#endif
    world_->removeComponentFromEntity<T>(this);
}

} // namespace fate
