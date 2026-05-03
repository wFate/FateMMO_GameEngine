#pragma once
// engine/module/hot_reload_hook_decl.h
//
// Tiny forward-declared shim so engine/ecs/entity_inline.h (which is included
// by world.h, which is included everywhere) can fire bind/unbind hooks
// without dragging the full hot_reload_manager.h dependency into the world
// graph.
//
// The full implementations live in engine/module/hot_reload_manager.cpp;
// the linker resolves these to singleton calls. When FATE_ENABLE_HOT_RELOAD
// is off the functions are not declared, so callers must guard.
//
// P2 (S153): bind hooks were extended so the per-frame tick can iterate the
// dense active_ roster directly instead of scanning every entity in the
// World each frame. Add fires when a BehaviorComponent is constructed on
// an entity (Entity::addComponent<T> AND World::addComponentById — the
// deserialization path). Rebind is the optional "name/enabled changed,
// please rebind on next tick" hint. Removed already existed.

#include "engine/ecs/entity_handle.h"

namespace fate {

class World;

#if FATE_ENABLE_HOT_RELOAD
void hotReloadNotifyBehaviorComponentRemoved(World& world, EntityHandle handle);
void hotReloadNotifyBehaviorComponentAdded(World& world, EntityHandle handle);
void hotReloadNotifyBehaviorRebind(World& world, EntityHandle handle);
#endif

} // namespace fate
