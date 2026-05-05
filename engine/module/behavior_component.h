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
//     "protocol": 1,
//     "fields":   { ... behavior-specific scalars ... },
//     "enabled":  true
//   }
//
// `protocol` is the FateModule protocol version that wrote the payload. It
// is HOST metadata — kept as a sibling of `fields`, NOT inside `fields`,
// because `fields` belongs to the behavior schema and must not collide
// with reserved keys. On scene load, missing `protocol` defaults to 1
// (the version that wrote today's existing payload shape — pre-stamp
// scenes); future protocol bumps then trigger the module's migrate
// callback via FateBehaviorVTable::migrate(ctx, fromVersion).
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

    // Protocol version that wrote bc->fields. Compared against the module's
    // FATE_MODULE_PROTOCOL_VERSION at swap time: when they differ, the
    // host calls vtable->migrate(ctx, fromVersion) to let the module
    // bridge older payloads. Pre-stamp scenes default to 1 on load (NOT
    // 0) — the JSON shape in those scenes IS protocol 1; only the stamp
    // is missing. Bumped to the current protocol after migration runs.
    uint32_t payloadProtocolVersion = 1;
};

// Reserved authored-field keys. Behavior schemas may NOT declare fields
// with these names — the host owns this namespace.
//
// `__fate_migrated` is a nested object under bc->fields populated when
// the schema diff at reload time finds an authored field that no longer
// exists in the new schema (or has changed type). The original value is
// moved under this object keyed by its old field name, so the designer
// can recover or manually drop it from the inspector. Survives save/
// reload cycles; persists in the scene .json. After designer cleanup
// the key disappears naturally.
inline constexpr const char* kBehaviorReservedKey_FateMigrated = "__fate_migrated";

// Custom (de)serializer that writes "behavior" + "fields" + "enabled" only.
// `state` and `boundGeneration` are deliberately skipped: scratch pointers are
// invalid across reload, and generation is recomputed on first dispatch.
void registerBehaviorComponent();

} // namespace fate

// Empty reflection — the inspector reaches into the JSON via a custom drawer
// later; auto-reflect can't introspect nlohmann::json.
FATE_REFLECT_EMPTY(fate::BehaviorComponent)
