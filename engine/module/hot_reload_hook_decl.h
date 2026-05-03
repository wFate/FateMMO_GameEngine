#pragma once
// engine/module/hot_reload_hook_decl.h
//
// Tiny forward-declared shim so engine/ecs/entity_inline.h (which is included
// by world.h, which is included everywhere) can fire the
// onBehaviorComponentRemoved hook without dragging the full
// hot_reload_manager.h dependency into the world graph.
//
// The full implementation lives in engine/module/hot_reload_manager.cpp;
// linker resolves it to the singleton call. When FATE_ENABLE_HOT_RELOAD is
// off the function is not declared, so callers must guard.

#include "engine/ecs/entity_handle.h"

namespace fate {

class World;

#if FATE_ENABLE_HOT_RELOAD
void hotReloadNotifyBehaviorComponentRemoved(World& world, EntityHandle handle);
#endif

} // namespace fate
