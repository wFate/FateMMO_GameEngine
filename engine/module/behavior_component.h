#pragma once
// engine/module/behavior_component.h
//
// BehaviorComponent — host-owned bag of state that the reloadable game module
// provides BEHAVIOR for. The component itself is part of fate_engine; only the
// *implementation* of behaviors lives in FateGameRuntime.dll.
//
// Persistence shape (matches the user-facing JSON contract):
//   {
//     "behavior": "GuardPatrol",
//     "fields":   { ... behavior-specific scalars ... },
//     "enabled":  true
//   }
//
// Lifetime: every field that survives reload lives here. The module's
// `void* state` scratch hangs off `state` and is owned/freed by module code
// (see fateGameModuleBeginReload + onDestroy in fate_module_abi.h).

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <nlohmann/json.hpp>
#include <string>

namespace fate {

struct BehaviorComponent {
    FATE_COMPONENT(BehaviorComponent)

    // Stable behavior name. Resolved against BehaviorRegistry every frame so a
    // reload that changes which name is registered "swaps" the implementation
    // automatically without touching this string.
    std::string behavior;

    // ---- Authoring fields (SERIALIZED) ------------------------------------
    // Designer-edited per-behavior data. Survives reload and round-trips
    // through the scene .json. Module reads/writes via host->getFloat /
    // setFloat by string key.
    //
    // RULE: do NOT write per-frame runtime values here (theta accumulators,
    // captured starting positions, target handles, etc.) — those leak into
    // saved scenes. Use runtimeFields below for runtime scratch the module
    // wants the host to own (and clear on destroy).
    nlohmann::json fields = nlohmann::json::object();

    // ---- Runtime fields (NOT serialized) ----------------------------------
    // Same JSON storage shape as `fields`, but stripped from toJson and
    // cleared at every onDestroy by HotReloadManager. Module reads/writes
    // via host->getRuntimeFloat / setRuntimeFloat. Use this for orbit
    // accumulators, captured anchor positions, cached target IDs — anything
    // that should NEVER end up in a saved scene.
    nlohmann::json runtimeFields = nlohmann::json::object();

    // ---- Runtime-only (not serialized) ------------------------------------

    // Module-owned scratch pointer. Module sets via host->setState; host
    // stores it here verbatim. NEVER persisted. Host calls the module's
    // vtable->onDestroy and then nulls this pointer at every reload, scene
    // unload, entity destroy, component removal, and behavior-name change —
    // so the module is guaranteed a chance to free before the pointer is
    // cleared. (See HotReloadManager::tearDown / sweep.)
    void* state = nullptr;

    // Bumped by HotReloadManager whenever the active module generation
    // changes. Per-frame dispatch compares this to the registry generation;
    // when they diverge, dispatch calls onStart on the new vtable before the
    // first onUpdate.
    uint32_t boundGeneration = 0;
};

// Custom (de)serializer that writes "behavior" + "fields" + "enabled" only.
// `state` and `boundGeneration` are deliberately skipped: scratch pointers are
// invalid across reload, and generation is recomputed on first dispatch.
void registerBehaviorComponent();

} // namespace fate

// Empty reflection — the inspector reaches into the JSON via a custom drawer
// later; auto-reflect can't introspect nlohmann::json.
FATE_REFLECT_EMPTY(fate::BehaviorComponent)
