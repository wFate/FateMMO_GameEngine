# Zone system, spatial hash, and chunked world management for a 2D MMORPG engine

**A Müller-style dense spatial hash combined with arena-backed per-zone memory and a ticket-based chunk lifecycle gives a tile-based 2D MMORPG the optimal tradeoff of cache performance, O(1) spatial queries, and hitch-free streaming.** This architecture unifies rendering, collision, AI activation, and multiplayer interest management behind a single spatial structure. The design leverages C++20/23 features—`std::span` for zero-copy query results, concepts for type-safe interfaces, `std::expected` for exception-free error handling, and coroutines for async chunk I/O—while building on Ryan Fleury's virtual-memory arena philosophy to eliminate fragmentation across zone load/unload cycles. What follows is production-ready guidance organized from data structures through integration and profiling.

---

## The spatial hash: why it dominates for uniform tile worlds

For a tile-based 2D MMORPG where entities are roughly uniform in size, **spatial hashing beats quadtrees on every metric that matters at runtime**. Insert, update, and remove are all O(1). A fixed-radius query checks exactly `ceil(2r/h + 1)²` cells—typically 4–9 for common interaction radii. Quadtrees require O(log n) traversal with pointer chasing at every level, destroying both L1 data-cache and branch-predictor performance. Benchmarks from the Bevy engine ecosystem (Leetless.de, 2023/2025) showed spatial hashing **100–400× faster** than brute-force ECS scanning for 10,000 entities across query radii from 5 to 50 units. Nathan Reed's hash-table benchmarks (2015, Core i7-4710HQ) demonstrated that a data-oriented open-addressing table with segregated hash/state arrays (his "DO1" layout) beat `std::unordered_map` on every operation except large-payload insertion.

The recommended implementation for a bounded tile world uses a **fixed power-of-two grid** backed by flat arrays, eliminating hash computation entirely:

```cpp
static constexpr int CELL_BITS = 5;   // 32-tile cells
static constexpr int GRID_BITS = 10;  // 1024×1024 grid
inline uint32_t cellIndex(int tileX, int tileY) {
    return ((tileY >> CELL_BITS) << GRID_BITS) | (tileX >> CELL_BITS);
}
```

This compiles to two shifts and an OR—zero multiplication, zero modular arithmetic. For sparse or unbounded regions beyond the main world grid, Matthias Müller's dense spatial hash (from Ten Minute Physics) provides the ideal fallback: two flat arrays (`cellCount[]` and `entityMap[]`) rebuilt each frame using prefix sums. The rebuild is O(n) with pure sequential writes, which is faster than incremental quadtree maintenance because the write pattern saturates memory bandwidth without any cache misses. The hash function `abs((x * 92837111) ^ (y * 689287499)) % tableSize` provides excellent distribution for 2D coordinates with just two multiplies and an XOR.

**Cell-size tuning** follows a simple rule: set the cell dimension to approximately **2× the radius of the most common entity** or approximately equal to the most common query radius. This ensures entities typically span 1–4 cells, and radial queries need at most a 3×3 neighborhood. The Bevy benchmarks confirmed that `optimal_h ≈ r` to `2r` where `r` is the query radius.

For SIMD exploitation, the bucket's entity data should use **SoA layout**—separate contiguous arrays for `entity_id`, `pos_x`, `pos_y`—enabling `_mm_load_ps` to process four entities per instruction in the narrow phase. When iterating adjacent cells, insert `_mm_prefetch(&cell_starts[nextCell], _MM_HINT_T0)` to hide latency. Batch queries by sorting querying entities by cell before processing, maximizing cache reuse across the cell's entity list. Morton/Z-order encoding (bit interleaving, ~5 cycles with BMI2 `pdep`/`pext`) improves cache locality when iterating grid cells for rendering or broad-phase sweeps, since numerically adjacent Z-codes correspond to spatially adjacent cells.

Quadtrees remain superior in one scenario: when entity sizes span orders of magnitude. The Hierarchical Spatial Hash Grid (HSHG, supahero1/GitHub) handles variable sizes with multiple overlapping grids at different resolutions, but its author acknowledges queries are **~30× slower than a quadtree** for heavily variable sizes. For a tile-based game where entities are roughly the same size, this situation simply does not arise.

---

## Chunk streaming without hitches: lifecycle, persistence, and ECS integration

**Chunk sizing** should be a power of two—**32×32 tiles** is the recommended default, offering a good balance between file I/O granularity, memory footprint (~2 KB raw tile data per chunk), and coordinate arithmetic via bit shifts. A three-level hierarchy keeps the system organized: **tile** (base unit) → **chunk** (32×32 tiles, the streaming unit) → **region** (16×16 chunks, the persistence/file unit) → **zone** (server assignment unit). Ultima 7 pioneered this tile→chunk→superchunk pattern; Minecraft codified it as the industry standard.

The chunk lifecycle operates as a state machine with **rate-limited transitions** to prevent frame hitches:

1. **Queued** → chunks identified for loading based on player proximity, sorted by priority (distance + movement direction prediction)
2. **Loading** → async file I/O on a worker thread, limited to **2–4 chunks per frame** at 60 fps
3. **Setup** → decompression, entity deserialization into ECS, spatial hash registration
4. **Active** → fully simulated, entities ticked, rendered
5. **Sleeping** → beyond active radius but still in memory; entities dormant (AI suspended, physics disabled), only position/state retained
6. **Unloading** → serialized if dirty, removed from ECS and spatial hash
7. **Evicted** → memory returned to zone arena

The critical technique for hitch-free loading is **double-buffered chunk data**: each chunk maintains an active tile array (used for rendering/queries) and a staging array (filled by the async loader). When the load completes, a single pointer swap at frame start makes the new data live. PaperMC's ticket-based system provides a sophisticated reference model—chunks hold "tickets" from players and other systems, transitioning states based on ticket levels rather than simple distance checks. This prevents thrashing when players oscillate near boundaries.

**Pre-fetching via concentric rings** is essential: maintain an outer ring (cached/pre-loaded, ~2 chunk widths beyond the viewport) and an inner ring (active/rendered). Load when entering the outer ring; activate when entering the inner ring; begin unloading when exiting the outer ring. This gives the async loader a full viewport-width of movement before the player could possibly see an unloaded chunk.

For **ECS integration**, both EnTT and Flecs recommend keeping spatial structures *external* to the ECS rather than encoding them as components. EnTT's creator explicitly states: "For spatial lookup, use a grid-based lookup, a quad tree or any other approach suitable for broad phase. You can put entities in these data structures and use them to get your data from the registry." The practical pattern is a parallel `EntityLocator` structure—a spatial hash mapping cell coordinates to `std::vector<entt::entity>`—rebuilt or incrementally updated each frame from a position-component query. Flecs supports **query groups** and relationship tags for grouping entities by zone, and its deferred command-buffer system safely queues structural changes (entity creation/destruction from streaming) during system iteration.

**Persistence** follows the "in-memory source of truth" pattern: the database is a recovery mechanism, not the authoritative state. Dirty-flag each chunk on modification; background-serialize dirty chunks every **30–60 seconds** via LZ4 compression into per-region binary files with coordinate-encoded filenames. Only entities with a `Persistent` component get serialized—transient effects like particles and projectiles are never saved. Player position persists every 30 seconds or on zone change; inventory persists on item change. Entity references use **64-bit globally unique persistent IDs** that survive serialization/deserialization across chunk boundaries, following the Terasology approach.

---

## Arena and pool allocators: Ryan Fleury's philosophy made concrete

Ryan Fleury's core insight, articulated in "Untangling Lifetimes: The Arena Allocator" (2022) and the "Enter The Arena" talk (Handmade Cities Boston, 2023), is that **`malloc`/`free` is the wrong interface for most programs**. The solution is grouping allocations by shared lifetime into arenas—bump allocators backed by reserved virtual memory that can be freed with a single pointer reset.

The **per-zone arena** is the most impactful application for a streaming world engine. Each zone reserves a large virtual address range (e.g., 4 GB) using `VirtualAlloc(MEM_RESERVE)` or `mmap(PROT_NONE)`, then commits pages on demand as entities and tile data stream in. When the zone unloads, a single `ArenaReset()` call returns the position to zero—**all zone memory freed in O(1)**, no iteration over data structures, no individual `free()` calls. Benchmarks show arena allocation running **~10× faster than malloc**, with deallocation **~358,000× faster** (one integer assignment vs. walking free lists).

Fleury's **scratch arena system** is equally critical for per-frame query temporaries. Each thread holds **two scratch arenas** in thread-local storage. The `GetScratch(Arena** conflicts, int count)` function returns whichever scratch arena does not conflict with any persistent arena in scope—this prevents accidental corruption when scratch and persistent allocations share an arena. Chris Wellons' refinement passes the scratch arena **by copy** (just the header struct), making allocations on the copy implicitly freed when the function returns.

For entities with individual lifetimes *within* a zone arena, compose a **pool allocator on top of the arena**. The arena provides backing memory; the pool provides O(1) alloc/dealloc of fixed-size blocks via an embedded free list (the `next` pointer lives inside the unused slot itself—zero overhead). When the zone arena resets, the pool and all its entities vanish together. This composition eliminates the tension between "entities have individual lifetimes" and "zones have bulk lifetimes."

The recommended allocator stack for the engine:

- **Frame arena** (double-buffered, 64 MB each): all per-frame temporaries—spatial query results, UI commands, physics scratch, render command lists. Reset at frame start. The second buffer allows safe references to the previous frame's data.
- **Zone arena** (virtual-memory-backed, 4 GB reserved each): all entities, tile data, spatial hash buckets, and zone-local state. Reset on zone unload.
- **Thread-local scratch arenas** (2 per thread, 256 MB reserved each): function-local temporaries, string building, decompression buffers. Fleury's conflict-avoidance API prevents aliasing with persistent arenas.
- **Entity pool** (built on zone arena): fixed-size blocks for entity/component allocation with free-list recycling and generational handles to detect stale references.

The **generational handle** pattern solves the ABA problem for cross-zone entity references. Each handle is a packed `(index: 20 bits, generation: 12 bits)` integer. When entity slot 5 is destroyed and reused, its generation increments, invalidating all outstanding handles to the previous occupant. Handles are trivially serializable for network sync and save files.

---

## Data-oriented spatial structures: SoA, hot/cold splitting, and SIMD

Mike Acton's foundational principle—"the purpose of all programs is to transform data from one form to another"—directly dictates spatial structure design. The spatial hash transforms position data → cell membership → collision pairs. Each transformation should operate on **contiguous, homogeneous arrays** rather than scattered object fields.

**SoA storage for spatial data** separates each coordinate axis into its own array:

```cpp
struct SpatialData {
    std::vector<float> pos_x;   // XXXX...
    std::vector<float> pos_y;   // YYYY...
    std::vector<float> vel_x;   // Contiguous for physics pass
    std::vector<float> vel_y;
};
```

This layout enables `_mm_load_ps(&pos_x[i])` to process 4 entities' X-coordinates per SIMD instruction during frustum culling or distance checks. X-Plane developers confirmed that splitting coordinates into individual arrays specifically enabled "loading four objects into four lanes of a SIMD register" for vectorized culling. The **AoSoA (Array of Structures of Arrays)** hybrid—packing SIMD-width groups of 4 or 8 entities into cache-line-sized blocks—offers a middle ground when operations frequently need both X and Y of the same entity, as used by Ogre-Next and Unity DOTS.

**Hot/cold data splitting** is mandatory for cache efficiency. Position and velocity (read every frame for spatial queries, physics, rendering) are hot data. Loot tables, debug flags, creation timestamps, and display names are cold data. Separating them means more entities fit per cache line during the hot-path iteration. Robert Nystrom (Game Programming Patterns): "The cost of compulsory misses on cold data is offset by the improved cache hit rate on hot data." In practice, the hot component struct should be **≤64 bytes** (one cache line) to maximize throughput.

**Memory-mapped tile data** via `mmap()` lets the OS manage page caching for large worlds automatically. Map the region file as read-only, hint with `MADV_RANDOM` for query workloads, and the kernel loads pages on demand—only actively-visited regions consume physical RAM. Caveats: I/O errors manifest as SIGSEGV/SIGBUS (install a signal handler), and on very fast NVMe SSDs, standard buffered I/O can outperform mmap due to page-fault overhead. For streaming, mmap works best as the backing store for cold tile data, while hot working-set tiles live in committed zone-arena memory.

---

## Integration: one spatial structure driving every subsystem

The zone/spatial-hash system is the **single most impactful architectural decision** in a game engine because it drives cascading performance benefits across every subsystem. The key insight: game interactions are inherently spatial and local.

**Render culling**: The renderer queries the spatial hash with the camera viewport rectangle to retrieve only entities in visible cells. For a 2D tile engine with 32×32-tile chunks, this is a simple AABB-vs-cell test yielding the visible chunk set. Pre-render each chunk's tiles to an off-screen texture (the "big tile" pattern from MDN's tilemap guidance), then blit visible chunks to the framebuffer. Entity sprites are retrieved from the spatial hash's visible cells only.

**Collision detection**: The spatial hash provides the broad phase, reducing O(n²) pair checks to O(k) per entity where k is the cell occupancy. Static geometry (walls, terrain) goes into a persistent spatial structure; dynamic entities go into the per-frame rebuilt hash. Only entities sharing a cell or neighboring cells (3×3 neighborhood in 2D) enter the narrow phase (SAT or GJK).

**AI activation** uses a **distance-proportional tick rate** (the DEAR pattern from Airplane/Paper server): `tick_frequency = (distance²) / (2^modifier)`. At modifier=9: entities at 10 tiles tick every frame, at 50 tiles every 5 frames, at 100 tiles every 20 frames. This scales AI CPU cost smoothly. Entities beyond a hard activation range (configurable per type: monsters 48 tiles, animals 32, villagers 16) are fully dormant—no pathfinding, no behavior-tree evaluation. Paper MC extends this with periodic **heartbeat wake-ups** for long-dormant entities to maintain world believability.

**Spawn management** ties directly to the zone lifecycle. When a zone activates, its spawn-point database initializes and creates entities. Respawn timers run only in active zones. When a zone sleeps, spawn state is serialized. Population-adaptive spawning (inspired by GW2's dynamic events) adjusts spawn density based on active player count in the zone.

**ECS system scheduling** follows a stage-based pipeline: Input → Spatial Rebuild → Physics Broad Phase → Physics Narrow Phase → AI → Game Logic → Network Replication → Rendering. Systems that read spatial data execute after the spatial-rebuild stage. Zone tags as archetype components enable O(1) filtering—a `CombatSystem` queries only entities with both `Combat` and `ActiveZone` components, skipping all dormant-zone entities without iteration.

---

## Multiplayer: interest management and server-side zoning

The spatial hash directly drives **Area of Interest (AOI) management**, the single most effective network optimization for an MMORPG. Each player has an AOI radius (~10 chunks or viewport + buffer). The server queries the spatial hash with this radius to determine which entities are relevant to each client, reducing per-player bandwidth from O(n) (all entities) to O(k) (nearby entities, k << n).

The **"update many, fetch one" pattern** (from Phaser Quest) is the recommended AOI implementation: when an event occurs, the corresponding AOI cell *and all neighboring cells* record the change. During the update cycle, each player's client receives only the update packet from their current cell. This makes expensive writes (events) rarer than cheap reads (per-tick fetches). AOI transitions compute set differences: entities entering visibility get full-state spawn messages; entities leaving get destroy messages. **Hysteresis**—using a slightly larger deactivation radius than activation radius—prevents rapid subscribe/unsubscribe thrashing at boundaries.

**Server-side zone architecture** follows the area-based sharding model used by Albion Online and described in PRDeving's MMO Architecture series. Each zone runs in its own process/thread with an independent tick loop and ECS registry. Cross-zone interactions route through a shared message bus or world server. The critical technique for seamless zone transitions is the **ghost entity pattern**: when a player approaches a zone boundary, a read-only "ghost" proxy is created on the destination server with the player's current state. On boundary crossing, authority transfers—the ghost becomes the master entity, the original becomes a ghost, then the source ghost is eventually destroyed. An overlap region of 1–2 chunk widths ensures visibility continuity.

The arena/pool memory design adapts naturally to this architecture. Each zone server's ECS registry backs its component storage with a zone arena. When a zone server shuts down or the zone unloads, the entire arena resets. Ghost entities live in a separate small arena on the destination server, with their own pool allocator. Generational handles serve as the cross-zone entity identifier—they're just integers, trivially serializable over the network, and the generation check catches stale references from entities that were destroyed during a zone transition.

**Bandwidth optimization** from spatial hashing: set the hash cell size approximately equal to the AOI radius for optimal query performance. Use **delta compression** (only send changed components, not full state) for entities remaining in a player's AOI across ticks—this reduces bandwidth by up to 90%. Apply **distance-based update priority**: closer entities receive more frequent, higher-precision updates; peripheral entities receive lower-frequency, quantized updates.

---

## Profiling spatial queries: Tracy, sanitizers, and cache analysis

**Tracy Profiler** (v0.13.0, open-source) is the primary instrumentation tool, with **~15 ns overhead per zone** (~8 ns per event, 2 events per zone). Even with thousands of spatial queries per frame, overhead stays under 50 µs. Instrument the spatial hash rebuild, individual queries, and chunk load/unload operations with named, color-coded zones:

```cpp
void SpatialHash::Rebuild() {
    ZoneScopedNC("SpatialHash_Rebuild", 0xFF8800);
    // ... rebuild
}
void SpatialHash::Query(AABB region, std::span<EntityID> out) {
    ZoneScopedNC("SpatialHash_Query", 0x00FF88);
    // ... query
}
```

Tracy's memory tracking (`TracyAlloc`/`TracyFree`) with callstack capture enables per-zone memory budget monitoring and leak detection. Use `TRACY_ON_DEMAND` to enable profiling only when the viewer connects, eliminating overhead in normal development runs.

**Performance targets** for spatial queries at 60 fps (**16.67 ms frame budget**): physics (including collision) should consume **2–4 ms** total, with the broad phase (spatial hash rebuild + pair generation) under **0.5–1 ms** for 1,000–5,000 dynamic entities. A single spatial hash cell lookup should cost **0.5–5 µs** depending on density. The entire spatial query system—rebuild, all queries for culling/collision/AI—should stay under **2 ms**.

**ASan + UBSan** should be the baseline in all development and CI builds. UBSan has negligible overhead (~1.2×) and catches integer overflow in hash index calculations and undefined shifts. ASan (2–3× slowdown) catches out-of-bounds access in grid cells, use-after-free on entity destruction, and heap corruption from spatial hash resizing. TSan (5–15× slowdown) is essential for validating concurrent spatial hash rebuilds but cannot combine with ASan—run it in a separate CI configuration. For cache analysis, Linux `perf stat -e L1-dcache-load-misses` and Intel VTune's Memory Access analysis reveal whether the spatial hash layout achieves the expected cache-line utilization. Target: spatial hash cell lookup should complete in **≤2 cache misses** (open addressing with segregated hash/state arrays).

---

## Modern C++ features that earn their keep

**`std::span`** replaces every raw `pointer + size` pair in spatial query interfaces. Query results return `std::span<const EntityID>` pointing into the spatial hash's internal buffer—zero-copy, bounds-aware, range-for compatible. Fixed-extent spans (`std::span<const EntityID, 9>`) encode compile-time guarantees for known-size results like "entities in a 3×3 neighborhood."

**Concepts** constrain spatial interfaces at compile time with clear error messages:

```cpp
template<typename T>
concept Positionable = requires(T e) {
    { e.x() } -> std::convertible_to<float>;
    { e.y() } -> std::convertible_to<float>;
};
template<Positionable E>
auto query_radius(std::span<const E> entities, float cx, float cy, float r)
    -> std::vector<E>;
```

**`std::expected<T, E>`** (C++23) provides exception-free error handling ideal for spatial queries that can fail (chunk not loaded, out of bounds, entity not found). Its monadic chaining—`.and_then()`, `.transform()`, `.or_else()`—composes query pipelines without exception overhead:

```cpp
auto target = find_nearest_enemy(pos, 50.0f)
    .and_then([](EntityHandle h) { return get_position(h); })
    .transform([&](vec2 p) { return compute_path(pos, p); });
```

**C++20 coroutines** provide the cleanest abstraction for async chunk loading. A `task<ChunkData> load_chunk_async(ChunkCoord)` coroutine `co_await`s async file I/O, suspends for decompression on a worker thread, then resumes on the game thread for ECS registration—all without callback spaghetti. UE5Coro demonstrates this pattern in production with `co_await MoveToTask()` and `co_await MoveToGameThread()` for thread affinity control. The caveat: coroutine frame heap allocation is not always elided by compilers, so profile to confirm.

**C++26 contracts** (expected in the standard ~2026) will add `pre(is_valid(handle))` and `post(r: r >= 0.0f)` annotations directly to spatial query functions, catching invalid handles and out-of-bounds results as checkable specifications rather than runtime assertions. **Hazard pointers** (also C++26) enable lock-free concurrent spatial hash access without premature reclamation—useful for read-heavy query workloads from multiple threads against a spatially-partitioned world.

---

## Conclusion

The architecture converges on a clear stack: **flat power-of-two spatial hash** (two-array Müller design for dynamic entities, direct-indexed grid for static tile lookups) → **32×32-tile chunks** with ticket-based lifecycle and double-buffered async loading → **per-zone virtual-memory arenas** with composed pool allocators for entity recycling → **SoA data layout** with hot/cold splitting for SIMD-friendly spatial queries → **ghost entity pattern** for seamless multiplayer zone transitions with spatial-hash-driven AOI. Three insights stand out as non-obvious. First, rebuilding the entire spatial hash from scratch each frame (Müller's approach) is often *faster* than incremental updates because the sequential write pattern saturates memory bandwidth with zero cache misses—measure before assuming incremental is better. Second, Ryan Fleury's two-scratch-arena-per-thread pattern with conflict avoidance eliminates an entire class of dangling-reference bugs in query temporaries without any runtime cost. Third, the DEAR distance-proportional tick-rate approach for AI activation is strictly superior to a hard activation radius—it eliminates the visible "pop" of entities suddenly coming alive while consuming less total CPU by smoothly scaling work with distance.