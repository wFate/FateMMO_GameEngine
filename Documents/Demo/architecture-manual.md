# Architecture Manual

Audience: Engine Contributor

This manual explains the major engine systems exposed by the demo. It is not a full source tour. It gives contributors enough context to make changes without violating the engine boundaries.

## Repository Boundaries

The monorepo is organized around clear layers:

- `engine/`: public engine core. Must compile standalone without `game/`.
- `examples/`: demo entry points, including `FateDemo`.
- `assets/`: runtime/editor assets.
- `game/`: proprietary gameplay layer in the full local tree.
- `server/`: proprietary authoritative server in the full local tree.
- `tests/`: local test coverage.
- `scripts/`: build wrappers and support scripts.

Engine code must not require `game/` unless all references are guarded by `FATE_HAS_GAME`.

## Demo App Lifecycle

The demo entry point is `examples/demo_app.cpp`.

Initialization does the following:

1. Creates a `DemoApp`.
2. Sets `AppConfig` title and window size.
3. Sets `assetsDir` from `FATE_SOURCE_DIR` when available.
4. Calls `app.init(config)`.
5. Runs the main loop.

`DemoApp::onInit` registers demo-safe components, creates a `demo` scene, sets editor roots, starts paused, and positions the camera.

`DemoApp::onRender` draws a large origin-centered grid. This gives users a visible editor workspace even before a scene is loaded.

## Scene System

Scenes are loaded from JSON and backed by a `World`.

Important concepts:

- Scene names are strings.
- Scene files live under `assets/scenes/`.
- The editor can load scenes from a dropdown.
- Scene saves write authored entities, not runtime-only replicated entities.
- Play mode snapshot/restore protects authoring data from runtime mutation.

Full game/server builds also use scene IDs for zone separation. The server is one flat world; scene separation is data on entities, not separate world instances.

## ECS Overview

The engine uses archetype ECS storage.

Core files:

- `engine/ecs/component_registry.h`
- `engine/ecs/component_meta.h`
- `engine/ecs/archetype.h`
- `engine/ecs/world.h`
- `engine/ecs/prefab.h`

Components use `FATE_COMPONENT`, `FATE_COMPONENT_HOT`, or `FATE_COMPONENT_COLD`. These macros provide:

- A compile-time component name.
- A runtime component type ID.
- A hot/warm/cold tier marker.
- An `enabled` flag at the front of the component.

## Archetype Memory Layout

An archetype is a unique sorted set of component type IDs. Every entity with the same component set is stored in the same archetype.

Each archetype contains:

- `typeIds`: sorted component IDs.
- `columns`: one type-erased component column per component type.
- `handles`: entity handles by row.
- `count`: live rows.
- `capacity`: allocated row capacity.
- `typeToColumn`: component ID to column lookup.

Each column stores a tightly packed array of one component type. This is structure-of-arrays storage. Iterating a system over a matching archetype reads contiguous component arrays rather than chasing per-entity heap allocations.

Rows are the entity's index inside its archetype. Adding or removing a component migrates the entity to another archetype. That migration can invalidate pointers to component data.

Contributor rule: never hold component pointers across operations that can add or remove components, destroy entities, or migrate archetypes. Re-fetch after structural changes.

## Component Registration

There are two registration layers:

- Demo-safe engine registration in `engine/components/register_engine_components.h`.
- Full-game registration in `game/register_components.h`.

The demo-safe list is:

- `Transform`
- `SpriteComponent`
- `TileLayerComponent`

Registration feeds `ComponentMetaRegistry`, which stores metadata, reflection fields, construction/destruction hooks, and JSON serialization functions.

If a component should serialize through prefabs or scenes, it must be registered explicitly.

## Prefabs

Prefabs serialize entities and registered components to JSON.

Important behavior:

- Unknown or unregistered components cannot round-trip correctly.
- Custom serializers are allowed when fields cannot be reflected directly.
- `SpriteComponent` uses a custom serializer because the live texture pointer is not JSON data.
- Prefab variants use JSON patch style data.

Prefer data-driven prefab edits over hard-coded entity creation when the entity should be authorable.

## Memory

The engine uses arena-backed storage in performance-sensitive systems.

ECS archetype columns allocate component arrays with alignment metadata. The storage reserves enough archetype slots to avoid vector reallocation invalidating references.

Contributor rules:

- Respect component alignment.
- Avoid long-lived raw pointers into archetype columns.
- Use engine allocation patterns already present in the system you are changing.
- Do not add heap churn to hot update loops without measuring it.

## Fiber Job System

Core files:

- `engine/job/job_system.h`
- `engine/job/job_system.cpp`
- `engine/job/fiber.h`
- `engine/job/fiber_win32.cpp`
- `engine/job/fiber_minicoro.cpp`

The job system provides:

- Worker threads.
- A bounded lock-free MPMC queue.
- A fixed counter pool.
- A 32-fiber pool.
- Counter-based waits.
- Fiber-local scratch arenas.

Default worker count is 4. Full server builds can tune worker count through `FATE_JOB_WORKERS`, capped by the implementation.

## Writing Thread-Safe Jobs

A job is a function pointer plus a `void*` parameter.

Safe job rules:

- Do CPU or IO decode work on worker fibers.
- Do not touch GPU resources from worker fibers.
- Do not mutate game-thread ECS state from worker fibers.
- Treat `AssetRegistry` main methods as main-thread-only unless documented otherwise.
- Use fiber scratch arenas for temporary worker-local memory.
- Use counters when the submitting thread needs completion.
- Use `tryPushFireAndForget` for producers that must not stall the game thread.

Main-thread finalization is required for GPU upload and most editor/world mutation.

## Asset And Render Flow

The asset pipeline separates decode from upload:

1. Main thread requests an asset.
2. Optional worker fiber decodes bytes.
3. Main thread finalizes upload.
4. Runtime uses a generational handle or cached texture pointer.

Renderer-facing texture cache behavior includes:

- Shared texture instances by path.
- VRAM estimate tracking.
- LRU eviction.
- Magenta placeholder texture for missing assets.

## Networking Boundary

Networking is engine-level code under `engine/net`, but much of the protocol is full-game/server-facing. Public demo docs should describe the architecture without promising that the demo ships a playable MMO server.

The authoritative source files are:

- `engine/net/packet.h`
- `engine/net/game_messages.h`
- `engine/net/reliability.h`
- `engine/net/net_client.h`
- `engine/net/net_server.h`
- `engine/net/packet_crypto.h`

## Build Boundaries

Use the wrapper for normal build verification:

```powershell
.\scripts\check_shipping.ps1
```

After engine/game boundary changes, verify the standalone engine/demo path still links. Any `engine/` include of `game/...` must be guarded with `FATE_HAS_GAME`.

