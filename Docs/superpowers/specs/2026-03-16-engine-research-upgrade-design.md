# Engine Research Upgrade Design Spec

**Date:** 2026-03-16
**Scope:** Full research stack implementation — archetype ECS, zone arenas, spatial grid, chunk lifecycle, profiling, multiplayer groundwork
**Strategy:** Inside-out (ECS core, then radiate outward)
**Target:** Bleeding-edge 2D MMORPG engine for TWOM-style game

---

## Overview

This upgrade transforms the FateMMO engine from a prototype-grade ECS with per-entity heap allocations into a production-grade data-oriented engine. The architecture follows the research document's recommended stack: archetype ECS with SoA storage → zone-lifetime arenas → fixed power-of-two spatial grid → 7-state chunk lifecycle → Tracy profiling → multiplayer groundwork.

Every design decision prioritizes the TWOM model: discrete zone maps with loading-screen transitions, persistent mob/boss state across zone visits, and structural readiness for server-authoritative multiplayer.

---

## Section 1: Memory System Upgrade

### Arena Stack

Four-tier allocator hierarchy replacing `new`/`delete` for all performance-critical paths:

**Zone Arena (4 GB virtual reserve each)**
- One per active zone/scene, allocated on zone load, reset O(1) on zone unload
- Backs all archetype component arrays, spatial grid buckets, chunk tile data, and zone-local state
- Uses existing `Arena` class (VirtualAlloc/mmap reserve, page-on-demand commit)
- Pool allocators compose on top for fixed-size entity slot recycling

**Frame Arena (double-buffered, 64 MB each)**
- Upgraded from current 16 MB to accommodate spatial query result buffers backed by `std::span` (which can reference thousands of EntityHandles per query), render command lists, and physics scratch. The 4x increase provides headroom as entity counts scale toward the 5,000-10,000 range.
- All per-frame temporaries: spatial query results (`std::span` backed), render command lists, physics scratch
- `swap()` at frame start; previous frame's data remains valid for cross-frame references

**Thread-Local Scratch Arenas (2 per thread, 256 MB reserved each)**
- Fleury's conflict-avoidance pattern: `GetScratch(Arena** conflicts, int count)` returns whichever scratch arena doesn't alias a persistent arena in scope
- Used for function-local temporaries: decompression buffers, string building, pathfinding scratch
- Foundation for future multithreaded spatial rebuild and chunk loading
- Note: `thread_local` with non-trivial constructors is safe in the static-link configuration (per CMakeLists.txt). If the engine is ever split into DLLs, scratch arena init ordering must be revisited.

**Global Persistent Arena (256 MB reserve)**
- Outlives any single zone; holds zone snapshots and cross-zone state
- Never reset during gameplay, only on full application shutdown

### Arena Exhaustion Policy

Zone arenas reserve 4 GB virtual (not physical — only committed pages consume RAM). If a zone exhausts its 4 GB reserve (extremely unlikely for 2D), `Arena::push()` returns `nullptr` and the caller must handle the failure. Arenas are NOT chained — exhaustion is a fatal assert in debug builds and a graceful error log + zone unload in release. The 4 GB reserve is chosen to be effectively unlimited for any realistic 2D zone while staying within 64-bit address space constraints.

### Zone Snapshots

When a zone unloads (player leaves, last ticket released), persistent state is serialized to a `ZoneSnapshot` allocated from the global persistent arena:

**Serialized (survives zone unload):**
- Boss/mob spawn timers as absolute timestamps (world-time, not relative countdowns)
- Mob positions, health, and AI state for alive mobs
- Spawn zone tracked mob lists with respawn schedules
- Loot/chest states, environmental changes (doors, switches)
- Chunk dirty flags

**Not serialized (recreated on zone load):**
- Spatial grid (rebuilt from entity positions)
- Render state, particle effects, projectiles in flight
- Archetype component arrays (repopulated from snapshot)

When the zone reloads, the snapshot is read and entities are restored to their saved state. Respawn timers use absolute timestamps, so time passes correctly while unloaded — if a boss respawn was 30 minutes and 20 minutes have passed, it spawns in 10 more minutes after reload.

### Zone Snapshot Serialization Format

Snapshots use a component-type-driven binary format:

- Each serializable component type registers a `serialize(BinaryWriter&)` and `deserialize(BinaryReader&)` method at compile time via a `FATE_SERIALIZABLE_COMPONENT` macro
- The snapshot iterates all entities with a `Persistent` marker component, writing: `[PersistentId][componentCount][typeId, size, data]...` per entity
- Spawn zone state (tracked mob lists, respawn timers) is serialized as a special snapshot section keyed by zone name
- The format is versioned (uint32 header) for forward compatibility
- Only entities with the `Persistent` marker are snapshot-eligible — transient entities (particles, projectiles, VFX) are skipped

---

## Section 2: Archetype ECS

### Component Type System Migration

The current `Component` is a polymorphic base class with virtual destructor and virtual methods (`typeName()`, `typeId()`). Components are stored as `unique_ptr<Component>` in an `unordered_map`. Archetype SoA storage requires plain value types in contiguous arrays — no vtables, no heap pointers per component.

**Migration path:**

1. **Strip the virtual base class.** The `Component` base struct becomes a non-virtual tag type with only `bool enabled = true`. No virtual destructor, no virtual methods.

2. **Replace `FATE_COMPONENT` macro** with a compile-time registration system:
```cpp
// Old: virtual dispatch
#define FATE_COMPONENT(ClassName) \
    const char* typeName() const override { return #ClassName; } \
    ComponentTypeId typeId() const override { return fate::getComponentTypeId<ClassName>(); }

// New: compile-time type traits
#define FATE_COMPONENT(ClassName) \
    static constexpr const char* COMPONENT_NAME = #ClassName; \
    static constexpr ComponentTypeId COMPONENT_ID = fate::componentId<ClassName>(); \
    static constexpr ComponentTier COMPONENT_TIER = ComponentTier::Warm;
```

3. **Component tier classification** via `ComponentTier` enum:
   - `Hot` — SoA field-split into individual float arrays (Transform position, velocity, depth)
   - `Warm` — contiguous typed array, not field-split (SpriteComponent, MobAIComponent)
   - `Cold` — contiguous typed array (InventoryComponent, GuildComponent)

   Game code overrides the default tier: `static constexpr ComponentTier COMPONENT_TIER = ComponentTier::Hot;`

4. **Hot component SoA registration.** Hot components additionally define which fields to split:
```cpp
// In game/components/transform.h
struct Transform {
    FATE_COMPONENT(Transform)
    static constexpr ComponentTier COMPONENT_TIER = ComponentTier::Hot;

    // These fields get split into individual arrays in archetype storage
    float pos_x = 0.0f;
    float pos_y = 0.0f;
    float rotation = 0.0f;
    float depth = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    bool enabled = true;
};
```
   The archetype system reflects on hot component fields via a `SoADescriptor` trait specialization:
```cpp
template<> struct SoADescriptor<Transform> {
    static constexpr auto fields = std::make_tuple(
        SoAField{"pos_x",    &Transform::pos_x},
        SoAField{"pos_y",    &Transform::pos_y},
        SoAField{"rotation", &Transform::rotation},
        SoAField{"depth",    &Transform::depth},
        SoAField{"scale_x",  &Transform::scale_x},
        SoAField{"scale_y",  &Transform::scale_y}
    );
};
```
   Each `SoAField` captures the member pointer, from which `offsetof`, `sizeof`, and type are deduced at compile time. The archetype allocator uses this to create one contiguous array per field. Game code provides one `SoADescriptor` specialization per hot component. Non-hot components are stored as-is (single contiguous typed array per archetype).

5. **Transform stays in game/.** The engine archetype system has no hardcoded knowledge of Transform. Game-level code registers Transform as a hot component via the `FATE_COMPONENT` macro with `ComponentTier::Hot`. The engine provides the SoA splitting mechanism; game code declares which components use it.

6. **Non-trivially-destructible members.** Components that contain `std::string`, `std::vector`, or other heap-owning types (e.g., entity name, tracked mob lists) must either:
   - Use arena-backed alternatives (`ArenaString`, `ArenaVector<T>` — small wrappers that allocate from a provided arena instead of the heap), or
   - Be classified as Cold tier and stored in a separate allocation path that runs destructors before zone arena reset via a registered cleanup callback list

   The cleanup is **per-archetype-column**, not per-entity. Each archetype column that holds a non-trivially-destructible type registers a single type-erased destructor loop: `void destroyColumn(void* data, size_t count)` which iterates the column array calling destructors. This keeps the callback count bounded to the number of non-trivial component types per archetype (typically 2-5), not the number of entities. The `World` invokes all registered column destructors during zone unload BEFORE arena reset. This handles non-trivial members without per-entity heap allocations for the cleanup mechanism itself.

**Components that need changes:**
- `Transform` — remove `Component` base, add `FATE_COMPONENT` with Hot tier
- `SpriteComponent` — remove `Component` base, add `FATE_COMPONENT` with Warm tier
- `PlayerController` — remove `Component` base, Warm tier
- `BoxCollider` — remove `Component` base, Warm tier
- `MobAIComponent` — remove `Component` base, Warm tier (contains non-trivial MobAI struct — cleanup callback)
- `SpawnZoneComponent` — remove `Component` base, Cold tier (contains vectors — cleanup callback)
- `CharacterStatsComponent` — remove `Component` base, Cold tier
- `InventoryComponent` — remove `Component` base, Cold tier
- `EnemyStatsComponent` — remove `Component` base, Warm tier
- All social components (Guild, Party, Friends, Trade, Market, Chat) — Cold tier, cleanup callbacks for string/vector members

### Core Concept

An archetype is a unique combination of component types. All entities sharing the same component set are stored together in contiguous arrays. This replaces the current per-entity `unordered_map<ComponentTypeId, unique_ptr<Component>>`.

### Data Layout

Hot components (Transform fields) are stored as individual float arrays (true SoA). Cold components are stored as contiguous typed arrays without field splitting:

```
Archetype [Transform, SpriteComponent, MobAIComponent]
  float pos_x[N]           // Transform.position.x — SoA for SIMD
  float pos_y[N]           // Transform.position.y — SoA for SIMD
  float rotation[N]        // Transform.rotation
  float depth[N]           // Transform.depth
  SpriteComponent sprites[N]   // contiguous typed array
  MobAIComponent ais[N]        // contiguous typed array
  EntityHandle handles[N]      // entity-to-slot mapping
```

### Hot/Cold Splitting

Components are classified at registration time:

- **Hot** (SoA, fields split into individual arrays): Transform position (x, y), velocity, depth — read every frame by spatial, physics, rendering
- **Warm** (contiguous typed array, not field-split): SpriteComponent, MobAIComponent, EnemyStatsComponent — read frequently but not SIMD-critical
- **Cold** (contiguous typed array): InventoryComponent, GuildComponent, FriendsComponent, display names — read rarely, on interaction only

Hot components in the same archetype share cache lines during spatial/physics iteration. Cold components don't pollute those cache lines.

### Compatibility Bridge

The existing `Entity` class becomes a thin facade:

- `entity->getComponent<T>()` looks up the archetype table internally via the entity's handle
- `entity->addComponent<T>()` moves the entity to a new archetype (component set changed)
- Existing game systems (combat, inventory, skills) work unchanged through the facade
- High-performance systems (spatial rebuild, rendering, AI) migrate to direct archetype iteration: `world.forEach<Transform, SpriteComponent>([](float* px, float* py, SpriteComponent* s, int count) { ... })`

### Zone Ownership

Each `World` instance (one per zone) owns its archetype tables. All archetype storage is allocated from the zone's arena. Zone unload resets the arena, destroying all archetype storage in O(1). No per-entity destructor calls, no free-list walking.

### Archetype Migration (Add/Remove Component)

When `entity->addComponent<T>()` is called, the entity moves from its current archetype to the archetype that includes T:

1. **Find or create target archetype.** Hash the new component set (sorted type IDs). If the archetype exists, reuse it. If not, allocate new column arrays from the zone arena.
2. **Copy existing component data** from the source archetype row to a new row in the target archetype. Hot components copy field-by-field into the SoA arrays; warm/cold components copy the struct.
3. **Fill the hole** in the source archetype via **swap-and-pop**: move the last entity in the source archetype into the vacated slot, update its entity-to-row mapping. This maintains contiguous packing. Swap-and-pop changes iteration order, but archetype iteration order is not guaranteed — systems must not depend on entity ordering within an archetype.
4. **Update entity-to-archetype mapping** (EntityHandle → archetype ID + row index).
5. **Migration is immediate**, not deferred. However, if migration occurs during `forEach` iteration, the iterator detects the structural change via a version counter on the archetype and safely handles it (current entity is skipped if moved, new entities at the end of the archetype are not visited in this iteration pass). A deferred command buffer is provided for systems that prefer to batch structural changes: `world.defer([&](){ entity->addComponent<T>(); })`. Deferred commands execute after ALL active iterations complete (i.e., after the system's `update()` returns), not after individual `forEach` calls. Nested `forEach` within a single system update is safe — both inner and outer iterations complete before any deferred structural changes apply.

`removeComponent<T>()` follows the same pattern in reverse: move to archetype minus T, swap-and-pop the old slot.

### Queries

`world.forEach<T1, T2>(callback)` iterates only archetypes containing both T1 and T2. This is O(matching entities) instead of O(all entities). Archetype matching is cached — the first query builds a match list, subsequent queries reuse it until the archetype set changes.

**Cache invalidation** uses a global archetype version counter on the World. Each time an archetype is created or destroyed, the counter increments. Each cached query stores the version it was built at. On forEach, if the query's version is stale, the match list is rebuilt (fast — just scan the archetype table, not entities). In practice, archetype creation is rare after initial entity setup, so queries almost always hit cache.

### Editor Compatibility

The editor's hierarchy panel, inspector, and entity iteration (`World::forEachEntity()`) continue to work through the Entity facade. The editor does not need direct archetype access. Inspector property display uses the same `getComponent<T>()` bridge. Entity creation/deletion in the editor uses the same deferred command buffer available to systems.

---

## Section 3: Spatial System Upgrade

### Dual Spatial Structure

Two structures optimized for different use cases, unified behind a common concept interface:

**SpatialGrid — fixed power-of-two grid for bounded worlds**

For known-bounds tile maps (Tiled JSON imports), a direct-indexed flat array:

```cpp
static constexpr int CELL_BITS = 5;   // 32-tile cells (128px at 32px tiles)
// GRID_BITS computed at zone load from map dimensions, rounded up to next power of two
inline uint32_t cellIndex(int tileX, int tileY, int gridBits) {
    return ((tileY >> CELL_BITS) << gridBits) | (tileX >> CELL_BITS);
}
```

Two shifts and an OR — zero hash computation, zero collisions. `gridBits` is computed once at zone load as `ceil(log2(max(mapWidthInCells, mapHeightInCells)))` and stored as a member. Using `max` ensures non-square maps are fully covered (a 200x800 tile map with 32-tile cells = 7x25 cells → `gridBits = 5`, grid is 32x32 cells). This wastes some memory for highly rectangular maps but keeps the single-`gridBits` bitshift formula intact. Cell arrays backed by the zone arena — sized exactly to `(1 << gridBits) * (1 << gridBits)` entries. For typical TWOM-style maps (200-500 tiles per side), this is a few KB.

**SpatialHashEngine — retained for unbounded/sparse regions**

The existing Mueller-style hash stays for future use: cross-zone overlap regions, open-world expansions, any context where bounds aren't known at load time. API modernized (see below).

### Modernized Query API

Both structures satisfy a `SpatialIndex` concept so systems don't care which backing they use:

- **`std::span<const EntityHandle>`** return type for query results — spatial structures store `EntityHandle` (not raw `EntityId`) so that callers get generational safety. Results point into scratch-arena-backed memory — zero allocation, zero copy. Callers can validate handles via `world.isAlive(handle)` but results are only guaranteed valid within the current frame (scratch arena resets on swap).
- **`std::expected<EntityHandle, SpatialError>`** for single-entity queries that can fail (entity not found, chunk not loaded, out of bounds)
- **`Positionable` concept** constrains template parameters at compile time with clear error messages

### Archetype Integration

Both spatial structures read directly from archetype `pos_x[]`/`pos_y[]` arrays during rebuild. The spatial rebuild iterates contiguous float arrays instead of chasing component pointers through `unordered_map`. This is where the SoA layout delivers its primary performance payoff.

### Cell-Size Tuning

Default 128px (4 tiles). Research confirms `optimal_h ≈ r to 2r` where `r` is the most common query radius. Current mob AI searches at contact/acquire radii of 1-7 tiles, so 4-tile cells hit the sweet spot (most queries check a 3x3 neighborhood of 9 cells).

### MobAISystem Spatial Unification

The current `MobAISystem` maintains its own private `SpatialHash playerGrid_` (128px cells) separate from the engine's `SpatialHashEngine`. This is unified: `MobAISystem` migrates to the engine's `SpatialGrid`/`SpatialIndex`. The engine's spatial structure stores ALL entities (players and mobs). MobAI queries filter by component presence (e.g., `queryRadius` with a filter for entities that have `CharacterStatsComponent`). This eliminates the duplicate per-frame rebuild and centralizes all spatial queries behind one structure.

---

## Section 4: Chunk Lifecycle Upgrade

### 7-State Machine

Replaces the current 3-state (Active/Sleeping/Evicted) with full lifecycle:

1. **Queued** — Identified for loading based on player proximity. Priority queue sorted by distance + movement direction prediction. Rate-limited entry into Loading state.
2. **Loading** — Tile data read from region file (or memory). Rate-limited to 2-4 chunks per frame at 60fps to prevent hitches. Structured for future worker-thread offloading.
3. **Setup** — Tile data placed in staging buffer. Entities deserialized from zone snapshot into ECS. Spatial grid registration.
4. **Active** — Fully simulated, rendered, entities ticked by all systems.
5. **Sleeping** — Beyond active radius, still in memory. Entities dormant: AI suspended, physics disabled, only position/state retained. Reduces CPU cost for large zones.
6. **Unloading** — Dirty chunks serialize mob state to zone snapshot. Entities removed from spatial grid and ECS.
7. **Evicted** — Memory returned to zone arena.

### Rate-Limited Transitions

Maximum 2-4 chunk state transitions per frame (configurable). Prevents the frame-time spike that occurs when many chunks change state simultaneously (e.g., player teleports across the map).

### Double-Buffered Chunk Data

Each chunk maintains:
- **Active tile array** — used by rendering and collision queries
- **Staging tile array** — filled by the loader

When loading completes, a single pointer swap at frame start makes staging data live. No mid-frame tearing, no partial chunk renders.

### Concentric Rings

Three distance rings from the player, measured in chunk coordinates:

- **Inner ring** (viewport + `activeBuffer` chunks, default 2) → Active state
- **Middle ring** (+ `sleepBuffer` chunks, default 4) → Sleeping state
- **Outer ring** (+ `prefetchBuffer` chunks, default 2) → Queued/Loading — pre-fetched before the player can see them
- **Beyond outer ring** → Unloading → Evicted

The prefetch ring gives the loader a full viewport-width of player movement before an unloaded chunk could become visible.

### Ticket System

Chunks hold a `ticketLevel` (uint8) instead of relying on pure distance checks. Ticket sources:

- **Player proximity** — automatic tickets based on concentric ring membership
- **System holds** — boss fights keep surrounding chunks active, cutscenes lock chunks
- **Future: other players** — in multiplayer, each player adds tickets independently

Chunk state is derived from the highest active ticket level:

| Ticket Level | Chunk State | Meaning |
|---|---|---|
| 4 | Active | In inner ring or system hold (boss fight, cutscene) |
| 3 | Sleeping | In middle ring, data retained, entities dormant |
| 2 | Loading/Setup | In prefetch ring, being loaded or initialized |
| 1 | Queued | Identified for loading, awaiting rate-limited slot |
| 0 | Unloading→Evicted | No tickets, serialize if dirty and free memory |

For single-player this behaves identically to distance checks. In multiplayer, multiple players naturally keep shared chunks alive — last ticket released triggers unload.

### Zone Transition Flow (Loading Screens)

TWOM-style discrete zone transitions (e.g., Village → Whispering Woods):

1. Player hits zone exit trigger
2. Loading screen appears with progress indicator
3. Old zone's state serializes to `ZoneSnapshot` (mob positions, spawn timers, boss state)
4. Old zone's arena resets — O(1), all memory freed instantly
5. New zone's arena allocates
6. New zone's chunks load in priority order (nearest to player entry point first)
7. Loading screen shows real progress ("Loading Whispering Woods... 14/22 chunks")
8. Entities restored from snapshot (returning zone) or freshly spawned (first visit)
9. Spatial grid rebuilt from loaded entity positions
10. Loading screen dismisses, player appears at entry point

Within a large zone, the concentric ring system streams chunks seamlessly as the player moves — no loading screen needed.

### Region Files (Persistence Skeleton)

Chunks grouped into 16x16 regions. Each region stored as a binary file:

- Filename: coordinate-encoded (e.g., `r_0_0.fate`)
- Format: header + LZ4-compressed tile data + entity snapshot block
- Only entities with a `Persistent` marker component get serialized
- Transient effects (particles, projectiles) are never saved

The serialization boundary is defined and the format is specified. Full disk persistence is future work; for now, zone snapshots live in the global persistent arena (in-memory only, lost on app restart).

---

## Section 5: Profiling & Diagnostics

### Tracy Profiler

Added as a FetchContent dependency (v0.13.0+). `TRACY_ON_DEMAND` mode — zero overhead when the viewer isn't connected.

Instrumented zones (named, color-coded):

| Zone Name | Color | What It Measures |
|---|---|---|
| `SpatialGrid_Rebuild` | Orange | Per-frame spatial rebuild from archetype arrays |
| `SpatialGrid_Query` | Green | Individual radius/nearest queries |
| `ChunkManager_Update` | Blue | Chunk state transitions and loading |
| `World_ForEach` | Yellow | Archetype iteration in ECS queries |
| `SpriteBatch_Sort` | Red | Sort pass (only when dirty flag set) |
| `ZoneTransition` | Purple | Full scene swap timing |
| `Arena_Push` | Cyan | Arena allocation tracking |
| `Arena_Reset` | Magenta | Zone arena O(1) reset events |

### Arena Memory Tracking

`TracyAlloc`/`TracyFree` wrappers on arena operations. Tracy shows per-zone memory budgets, commit growth curves, and confirms O(1) reset events visually.

### Performance Budget Targets (60fps, 16.67ms frame)

- Spatial rebuild + all queries: < 2ms
- Broad phase collision: < 0.5ms
- Chunk state transitions: < 0.5ms per frame
- Full zone load (loading screen): measured but not frame-budgeted

### SpriteBatch Dirty Flag

The SpriteBatch maintains a `sortDirty_` bool, defaulting to `true`. The render system sets it to `true` when it detects changes: a hash of `(textureId, depth)` per sprite is compared against the previous frame's hash. If the hash matches, `sortDirty_` stays false and `end()` skips the sort pass entirely. This avoids both the error-prone manual-flag approach (game code must remember to set it) and the expensive per-field write tracking approach. Cost: one uint32 hash comparison per sprite per frame, which is negligible compared to the sort it replaces.

### Sanitizer Build Configurations

CMake presets added:

- `Debug-ASan` — AddressSanitizer (2-3x slowdown): catches out-of-bounds on archetype arrays, use-after-free on entity destruction, arena overflows
- `Debug-UBSan` — UndefinedBehaviorSanitizer (~1.2x overhead): catches integer overflow in hash/index calculations, undefined bit shifts, null pointer dereference

---

## Section 6: Multiplayer Groundwork

Structural foundations only — no networking, no ENet, no packet serialization. These data structures and patterns are in place so multiplayer plugs in without architectural rework.

### AOI (Area of Interest) Data Structures

Each entity can have an `AOIRadius` float defining its visibility bubble. The spatial grid already supports radius queries — when networking arrives, the server queries `spatialGrid.queryRadius(playerPos, aoiRadius)` to determine per-player entity visibility.

### Visibility Set & Transition Tracking

Per-player `VisibilitySet` — a flat sorted array of `EntityHandle` persisted across frames:

- Each tick: compute new set from spatial query, diff against previous
- **Entered visibility** → queue full-state spawn message
- **Left visibility** → queue destroy message
- **Stayed visible** → queue delta update (only dirty components)

The diff logic is fully functional without networking. When networking arrives, diff output feeds directly into packet building.

### Hysteresis

Separate activation radius and deactivation radius (deactivation ~20% larger). Prevents rapid subscribe/unsubscribe thrashing when entities oscillate at visibility boundaries.

### Ghost Entity Scaffold

- `GhostFlag` component marker identifying read-only proxy entities
- `GhostArena` — small dedicated arena (separate from zone arenas) for ghost entity storage
- Pattern: when a player approaches a zone boundary, the system can create a ghost proxy in the destination zone's World with the player's current state
- For now: marker + arena allocation only, no cross-zone synchronization logic

### Persistent Entity IDs

64-bit globally unique IDs for entities that survive serialization/deserialization:

```
PersistentId = (zoneId:16 | creationTime:32 | sequence:16)
```

Separate from `EntityHandle` (local runtime reference, 20-bit index + 12-bit generation). Persistent IDs go into save files and future network packets. Handles are for fast in-memory lookup. Mapping between the two maintained per-zone.

**Sequence overflow:** The 16-bit sequence (65,536 per second per zone) wraps. On wrap, `creationTime` is re-read from the system clock, guaranteeing a new `(creationTime, sequence)` pair. Since `creationTime` is a 32-bit unix timestamp (seconds), two entities can only collide if 65,536+ entities are created in the same second AND the clock hasn't advanced — practically impossible for a 2D MMORPG. If paranoia warrants it, the mapping table checks for collision on insert and bumps the sequence.

### Delta Compression Markers

Components get a `dirty` bit, set on mutation, cleared after serialization. The future network serialization layer reads only dirty components for updates — up to 90% bandwidth reduction vs. full-state sends. For now, just the flag infrastructure.

---

## Files Affected

### New Files (engine/)
- `engine/memory/scratch_arena.h` — thread-local scratch arenas with conflict avoidance
- `engine/memory/zone_snapshot.h` — zone state serialization/deserialization
- `engine/ecs/archetype.h` — archetype storage, SoA arrays, component registration
- `engine/ecs/archetype_query.h` — cached archetype matching for forEach
- `engine/ecs/persistent_id.h` — 64-bit globally unique entity IDs
- `engine/spatial/spatial_grid.h` — fixed power-of-two bounded-world grid
- `engine/spatial/spatial_index.h` — concept interface unifying grid and hash
- `engine/net/aoi.h` — AOI data structures, visibility sets, hysteresis
- `engine/net/ghost.h` — ghost entity marker and arena
- `engine/profiling/tracy_zones.h` — Tracy macro wrappers and zone definitions

### Modified Files (engine/)
- `engine/memory/arena.h` — frame arena size increase (16→64 MB), Tracy wrappers
- `engine/memory/pool.h` — zone-arena backing enforcement
- `engine/ecs/world.h` / `world.cpp` — archetype-backed storage, zone arena ownership, forEach rewrite
- `engine/ecs/entity.h` / `entity.cpp` — thin facade over archetype storage
- `engine/ecs/entity_handle.h` — no structural change, used as-is
- `engine/ecs/component.h` — dirty bit, hot/cold classification marker
- `engine/spatial/spatial_hash.h` — std::span returns, std::expected, concept constraints
- `engine/tilemap/chunk.h` — 7-state machine, ticket system, double-buffered data, staging array
- `engine/tilemap/tilemap.h` / `tilemap.cpp` — region file skeleton, chunk lifecycle integration
- `engine/render/sprite_batch.h` / `sprite_batch.cpp` — dirty flag optimization
- `engine/scene/scene.h` / `scene.cpp` — zone arena lifecycle, snapshot integration
- `engine/scene/scene_manager.h` / `scene_manager.cpp` — loading screen flow, zone transition orchestration
- `CMakeLists.txt` — Tracy FetchContent, sanitizer presets

### Modified Files (game/)
- `game/game_app.h` / `game_app.cpp` — global persistent arena init, frame arena upgrade
- `game/systems/spawn_system.h` — snapshot serialization/deserialization of spawn state
- `game/systems/mob_ai_system.h` — migrates from private `SpatialHash playerGrid_` to engine's `SpatialGrid`; reads from archetype position arrays
- `game/components/transform.h` — remove Component base, add FATE_COMPONENT with Hot tier
- `game/components/sprite_component.h` — remove Component base, Warm tier
- `game/components/player_controller.h` — remove Component base, Warm tier
- `game/components/box_collider.h` — remove Component base, Warm tier
- `game/components/animator.h` — remove Component base, Warm tier
- `game/components/zone_component.h` — remove Component base, Cold tier
- `game/components/game_components.h` — all game component wrappers updated to new macro, cleanup callbacks registered for types with non-trivial members
- Game systems gradually migrate from Entity facade to direct archetype iteration

---

## What This Does NOT Include

- Actual networking (ENet, packet serialization, server loop)
- Database persistence (libpqxx, SQL schemas)
- Server-authoritative validation
- Cross-zone synchronization logic
- SIMD intrinsics (future optimization after profiling confirms bottlenecks)
- Morton/Z-order encoding (future optimization)
- C++20 coroutines for async I/O (structured for future drop-in)
- C++26 contracts/hazard pointers (not yet available)
