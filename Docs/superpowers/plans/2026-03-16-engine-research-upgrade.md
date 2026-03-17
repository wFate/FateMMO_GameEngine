# Engine Research Upgrade Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform the FateMMO engine from prototype-grade ECS into a production-grade data-oriented engine with archetype storage, zone arenas, fixed spatial grid, 7-state chunk lifecycle, Tracy profiling, and multiplayer groundwork.

**Architecture:** Inside-out build order — memory system first, then archetype ECS (backed by arenas), then spatial systems (reading from archetypes), then chunks (orchestrating all three), then profiling (wrapping everything), then multiplayer scaffolding. A compatibility bridge keeps existing game systems working: Entity remains a heap-allocated object in the World (preserving Entity\* pointer stability across frames), but internally stores (archetypeId, rowIndex) and delegates getComponent to archetype column storage.

**Critical design decisions:**
- `CompId` (uint32_t) is the new compile-time component type ID. The legacy `ComponentTypeId` (std::type_index) is retained during migration to avoid breaking entity.h.
- Transform stays as Warm tier (contiguous typed array, NOT SoA field-split). The Vec2 position member is left intact — true SoA field-splitting into flat `float pos_x[], pos_y[]` is deferred as a future SIMD optimization task. The spatial rebuild reads from contiguous Transform arrays (still a massive improvement over unordered_map pointer chasing).
- Entity\* pointers remain valid across frames. Entity is heap-allocated in the World, not a transient stack facade. Task 6 (World Rewrite) must preserve this invariant.
- ScratchArena uses RAII `ScratchScope` for automatic reset on scope exit.

**Tech Stack:** C++23, MSVC, CMake 3.20+, SDL2, OpenGL 3.3, Dear ImGui, Tracy Profiler, doctest (for unit tests)

**Spec:** `Docs/superpowers/specs/2026-03-16-engine-research-upgrade-design.md`

---

## File Structure

### New Engine Files
| File | Responsibility |
|---|---|
| `engine/memory/scratch_arena.h` | Thread-local scratch arenas with Fleury's conflict-avoidance |
| `engine/memory/zone_snapshot.h` | Zone state serialization/deserialization format |
| `engine/memory/arena_string.h` | Arena-backed string type (no heap alloc) — **deferred, non-trivial string members use cleanup callbacks for now** |
| `engine/ecs/archetype.h` | Archetype storage — SoA arrays, column management, swap-and-pop |
| `engine/ecs/archetype_query.h` | Cached archetype matching with version-counter invalidation |
| `engine/ecs/component_registry.h` | Compile-time component type ID, tier classification, SoADescriptor |
| `engine/ecs/persistent_id.h` | 64-bit globally unique entity IDs |
| `engine/ecs/command_buffer.h` | Deferred structural changes (add/remove component during iteration) |
| `engine/spatial/spatial_grid.h` | Fixed power-of-two bounded-world grid |
| `engine/spatial/spatial_index.h` | Concept interface unifying grid and hash |
| `engine/net/aoi.h` | AOI data structures, visibility sets, hysteresis |
| `engine/net/ghost.h` | Ghost entity marker and arena |
| `engine/profiling/tracy_zones.h` | Tracy macro wrappers, zone definitions, arena tracking |
| `tests/test_main.cpp` | doctest main entry point |
| `tests/test_arena.cpp` | Arena, FrameArena, ScratchArena unit tests |
| `tests/test_archetype.cpp` | Archetype storage, migration, query tests |
| `tests/test_spatial_grid.cpp` | SpatialGrid insert, query, rebuild tests |
| `tests/test_chunk_lifecycle.cpp` | Chunk state machine transition tests |

### Modified Engine Files
| File | Changes |
|---|---|
| `engine/memory/arena.h` | Frame arena 16→64 MB, Tracy wrappers, global persistent arena |
| `engine/memory/pool.h` | Zone-arena backing enforcement |
| `engine/ecs/component.h` | Strip virtual base, new FATE_COMPONENT macro, ComponentTier enum |
| `engine/ecs/entity.h` / `entity.cpp` | Thin facade over archetype storage |
| `engine/ecs/world.h` / `world.cpp` | Archetype-backed storage, zone arena ownership, forEach rewrite |
| `engine/spatial/spatial_hash.h` | std::span returns, std::expected, concept constraints |
| `engine/tilemap/chunk.h` | 7-state machine, ticket system, double-buffered data |
| `engine/tilemap/tilemap.h` / `tilemap.cpp` | Region file skeleton, chunk lifecycle integration |
| `engine/render/sprite_batch.h` / `sprite_batch.cpp` | Dirty flag optimization |
| `engine/scene/scene.h` / `scene.cpp` | Zone arena lifecycle, snapshot integration |
| `engine/scene/scene_manager.h` / `scene_manager.cpp` | Loading screen flow, zone transition |
| `CMakeLists.txt` | Tracy FetchContent, doctest, sanitizer presets |

### Modified Game Files
| File | Changes |
|---|---|
| `game/components/transform.h` | Remove Component base, Hot tier, SoADescriptor |
| `game/components/sprite_component.h` | Remove Component base, Warm tier |
| `game/components/player_controller.h` | Remove Component base, Warm tier |
| `game/components/box_collider.h` | Remove Component base, Warm tier |
| `game/components/animator.h` | Remove Component base, Warm tier |
| `game/components/zone_component.h` | Remove Component base, Cold tier |
| `game/components/game_components.h` | All wrappers updated to new macro |
| `game/systems/mob_ai_system.h` | Migrate to engine SpatialGrid |
| `game/systems/spawn_system.h` | Snapshot serialization hooks |
| `game/game_app.h` / `game_app.cpp` | Global persistent arena, frame arena upgrade |

---

## Task 1: Test Framework & Build Infrastructure

**Files:**
- Create: `tests/test_main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add doctest and Tracy to CMakeLists.txt**

Add FetchContent declarations for doctest (header-only test framework) and Tracy profiler. Add sanitizer CMake presets. Add test executable target.

```cmake
# After the existing FetchContent declarations, add:

# --- doctest (unit testing) ---
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

# --- Tracy Profiler ---
FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.11.1
)
set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy profiler")
set(TRACY_ON_DEMAND ON CACHE BOOL "Enable on-demand profiling")
FetchContent_MakeAvailable(tracy)

# Link Tracy to engine
target_link_libraries(fate_engine PUBLIC TracyClient)
target_compile_definitions(fate_engine PUBLIC TRACY_ENABLE TRACY_ON_DEMAND)

# --- Test executable ---
file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
add_executable(fate_tests ${TEST_SOURCES})
target_link_libraries(fate_tests PRIVATE fate_engine doctest::doctest)
target_include_directories(fate_tests PRIVATE ${CMAKE_SOURCE_DIR})

# --- Sanitizer presets ---
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    # MSVC AddressSanitizer
    set(CMAKE_CXX_FLAGS_ASAN "/fsanitize=address /Zi /Od" CACHE STRING "")
    set(CMAKE_EXE_LINKER_FLAGS_ASAN "/DEBUG" CACHE STRING "")
    # Note: MSVC does not support UBSan. UBSan is available via Clang-cl only.
    # For now, only ASan is configured. UBSan can be added if/when Clang-cl is adopted.
endif()
```

- [ ] **Step 2: Create test main entry point**

```cpp
// tests/test_main.cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

- [ ] **Step 3: Verify build compiles**

Run: `"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build out/build --target fate_tests`
Expected: Compiles successfully (0 tests found, 0 passed)

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt tests/test_main.cpp
git commit -m "feat(build): add doctest test framework, Tracy profiler, sanitizer presets"
```

---

## Task 2: Memory System — Scratch Arenas & Global Persistent Arena

**Files:**
- Create: `engine/memory/scratch_arena.h`
- Modify: `engine/memory/arena.h`
- Create: `tests/test_arena.cpp`

- [ ] **Step 1: Write arena tests**

```cpp
// tests/test_arena.cpp
#include <doctest/doctest.h>
#include "engine/memory/arena.h"
#include "engine/memory/scratch_arena.h"

TEST_CASE("Arena basic allocation") {
    fate::Arena arena(1024 * 1024); // 1 MB
    CHECK(arena.position() == 0);

    int* p = arena.pushType<int>(42);
    REQUIRE(p != nullptr);
    CHECK(*p == 42);
    CHECK(arena.position() > 0);

    arena.reset();
    CHECK(arena.position() == 0);
}

TEST_CASE("Arena pushArray") {
    fate::Arena arena(1024 * 1024);
    float* arr = arena.pushArray<float>(100);
    REQUIRE(arr != nullptr);
    for (int i = 0; i < 100; i++) arr[i] = (float)i;
    CHECK(arr[50] == 50.0f);
}

TEST_CASE("FrameArena double buffer") {
    fate::FrameArena frame(1024 * 1024);
    int* a = frame.pushType<int>(1);
    frame.swap();
    int* b = frame.pushType<int>(2);
    // Previous frame data still valid
    CHECK(*a == 1);
    CHECK(*b == 2);
}

TEST_CASE("ScratchArena conflict avoidance") {
    fate::Arena persistent(1024 * 1024);

    // GetScratch should return an arena that doesn't conflict
    fate::Arena* conflicts[] = { &persistent };
    auto scratch = fate::GetScratch(conflicts, 1);
    CHECK(scratch.arena != &persistent);

    // Allocate into scratch — doesn't affect persistent
    int* p = static_cast<int*>(scratch.arena->push(sizeof(int)));
    *p = 99;
    CHECK(*p == 99);
}

TEST_CASE("ScratchArena no conflicts returns first") {
    auto s1 = fate::GetScratch(nullptr, 0);
    auto s2 = fate::GetScratch(nullptr, 0);
    CHECK(s1.arena == s2.arena);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: build and run `fate_tests`
Expected: FAIL — `scratch_arena.h` doesn't exist, `GetScratch` not defined

- [ ] **Step 3: Create scratch_arena.h**

```cpp
// engine/memory/scratch_arena.h
#pragma once
#include "engine/memory/arena.h"

namespace fate {

// Fleury-style scratch arena result.
// The arena pointer is borrowed — do NOT store it past the current scope.
struct ScratchArena {
    Arena* arena = nullptr;
};

// Thread-local scratch arenas (2 per thread, 256 MB reserved each).
// GetScratch returns whichever scratch arena does NOT conflict with
// any arena in the conflicts array. This prevents accidental corruption
// when scratch and persistent allocations share an arena.
inline ScratchArena GetScratch(Arena** conflicts, int conflictCount) {
    static constexpr size_t SCRATCH_RESERVE = 256 * 1024 * 1024;
    thread_local Arena s_scratch[2] = { Arena(SCRATCH_RESERVE), Arena(SCRATCH_RESERVE) };

    // Return whichever doesn't conflict
    for (int i = 0; i < 2; ++i) {
        bool conflict = false;
        for (int c = 0; c < conflictCount; ++c) {
            if (conflicts && conflicts[c] == &s_scratch[i]) {
                conflict = true;
                break;
            }
        }
        if (!conflict) {
            return { &s_scratch[i] };
        }
    }
    // Both conflict — shouldn't happen with 2 arenas. Return first.
    return { &s_scratch[0] };
}

// Convenience: get scratch with no conflicts
inline ScratchArena GetScratch() {
    return GetScratch(nullptr, 0);
}

// RAII guard — saves arena position on construction, resets on destruction.
// Use this to scope scratch allocations within a function.
struct ScratchScope {
    Arena* arena;
    size_t savedPos;

    explicit ScratchScope(ScratchArena s) : arena(s.arena), savedPos(s.arena->position()) {}
    ~ScratchScope() { arena->resetTo(savedPos); }
    ScratchScope(const ScratchScope&) = delete;
    ScratchScope& operator=(const ScratchScope&) = delete;

    void* push(size_t size, size_t alignment = 16) { return arena->push(size, alignment); }
    template<typename T, typename... Args>
    T* pushType(Args&&... args) { return arena->pushType<T>(std::forward<Args>(args)...); }
};

} // namespace fate
```

- [ ] **Step 4: Add `resetTo()` method to Arena**

In `engine/memory/arena.h`, add to the Arena class:
```cpp
void resetTo(size_t pos) { pos_ = pos; }
```

This allows ScratchScope to restore the arena to a saved position.

- [ ] **Step 5: Update arena.h — increase FrameArena to 64 MB**

In `engine/memory/arena.h`, change FrameArena default reserve:
```cpp
explicit FrameArena(size_t reservePerBuffer = 64 * 1024 * 1024)
```

- [ ] **Step 6: Run tests to verify they pass**

Run: build and run `fate_tests`
Expected: All arena tests PASS

- [ ] **Step 7: Commit**

```bash
git add engine/memory/scratch_arena.h engine/memory/arena.h tests/test_arena.cpp
git commit -m "feat(memory): add thread-local scratch arenas, increase FrameArena to 64MB"
```

---

## Task 3: Component Type System Migration

**Files:**
- Modify: `engine/ecs/component.h`
- Create: `engine/ecs/component_registry.h`

- [ ] **Step 1: Create component_registry.h with compile-time type system**

```cpp
// engine/ecs/component_registry.h
#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <tuple>

namespace fate {

// Component tier classification for archetype storage strategy
enum class ComponentTier : uint8_t {
    Hot,   // SoA field-split into individual float arrays (future, deferred)
    Warm,  // Contiguous typed array, not field-split (SpriteComponent, Transform)
    Cold   // Contiguous typed array, rarely accessed (InventoryComponent)
};

// New compile-time component type ID — monotonic counter, no RTTI needed.
// Named CompId (not ComponentTypeId) to avoid collision with the legacy
// ComponentTypeId = std::type_index still used by entity.h during migration.
using CompId = uint32_t;

inline CompId nextCompId() {
    static std::atomic<CompId> counter{0};
    return counter.fetch_add(1);
}

template<typename T>
CompId componentId() {
    static const CompId id = nextCompId();
    return id;
}

// SoA field descriptor for hot components
template<typename Class, typename Field>
struct SoAField {
    const char* name;
    Field Class::* pointer;

    constexpr SoAField(const char* n, Field Class::* p) : name(n), pointer(p) {}
    static constexpr size_t size() { return sizeof(Field); }
    static constexpr size_t alignment() { return alignof(Field); }
};

// Default SoADescriptor — empty (component is not SoA-split)
template<typename T>
struct SoADescriptor {
    static constexpr auto fields = std::tuple<>();
    static constexpr bool isSoA = false;
};

// Helper to check if a type has SoA fields
template<typename T>
constexpr bool hasSoAFields = SoADescriptor<T>::isSoA;

// New FATE_COMPONENT macro — compile-time, no virtual dispatch
// Usage: struct MyComponent { FATE_COMPONENT(MyComponent) ... };
#define FATE_COMPONENT(ClassName) \
    static constexpr const char* COMPONENT_NAME = #ClassName; \
    static constexpr fate::CompId COMPONENT_TYPE_ID = fate::componentId<ClassName>(); \
    static constexpr fate::ComponentTier COMPONENT_TIER = fate::ComponentTier::Warm; \
    bool enabled = true;

// For hot components that override the tier (future SoA splitting)
#define FATE_COMPONENT_HOT(ClassName) \
    static constexpr const char* COMPONENT_NAME = #ClassName; \
    static constexpr fate::CompId COMPONENT_TYPE_ID = fate::componentId<ClassName>(); \
    static constexpr fate::ComponentTier COMPONENT_TIER = fate::ComponentTier::Hot; \
    bool enabled = true;

// For cold components
#define FATE_COMPONENT_COLD(ClassName) \
    static constexpr const char* COMPONENT_NAME = #ClassName; \
    static constexpr fate::CompId COMPONENT_TYPE_ID = fate::componentId<ClassName>(); \
    static constexpr fate::ComponentTier COMPONENT_TIER = fate::ComponentTier::Cold; \
    bool enabled = true;

} // namespace fate
```

- [ ] **Step 2: Update engine/ecs/component.h — keep backward compat temporarily**

The old `Component` base class stays for now. The old `FATE_COMPONENT` macro is renamed to `FATE_LEGACY_COMPONENT`. The new macros from `component_registry.h` use `FATE_COMPONENT` (the new compile-time version). The legacy `ComponentTypeId` (std::type_index) is kept as-is — no rename — since `entity.h` depends on it. The new system uses `CompId` (uint32_t) from `component_registry.h`.

```cpp
// engine/ecs/component.h
#pragma once
#include "engine/ecs/component_registry.h"
#include <cstdint>
#include <string>
#include <typeinfo>
#include <typeindex>

namespace fate {

// Legacy component type ID — still used by entity.h's unordered_map during migration.
// This is std::type_index, NOT the new CompId (uint32_t).
using ComponentTypeId = std::type_index;

template<typename T>
ComponentTypeId getComponentTypeId() {
    return std::type_index(typeid(T));
}

// Legacy base component — kept during migration, will be removed
// once all game components switch to new FATE_COMPONENT macros.
struct Component {
    virtual ~Component() = default;
    virtual const char* typeName() const = 0;
    virtual ComponentTypeId typeId() const = 0;
    bool enabled = true;
};

// Legacy macro — game code uses this during migration period
#define FATE_LEGACY_COMPONENT(ClassName) \
    const char* typeName() const override { return #ClassName; } \
    fate::ComponentTypeId typeId() const override { return fate::getComponentTypeId<ClassName>(); }

} // namespace fate
```

Note: `entity.h` is NOT modified in this task. It continues using `ComponentTypeId` (std::type_index) and `unique_ptr<Component>` in its unordered_map. The archetype system uses `CompId` (uint32_t). The two systems coexist until Task 6 (World Rewrite) bridges them.

- [ ] **Step 3: Rename all game FATE_COMPONENT usages to FATE_LEGACY_COMPONENT**

This is an atomic step — all game files must be updated before building. Search and replace `FATE_COMPONENT(` with `FATE_LEGACY_COMPONENT(` in these files:
- `game/components/game_components.h` (all component structs)
- `game/components/animator.h`
- `game/components/box_collider.h`
- `game/components/sprite_component.h`
- `game/components/player_controller.h`
- `game/components/zone_component.h`
- `game/components/polygon_collider.h`
- `game/systems/spawn_system.h` (SpawnZoneComponent)

- [ ] **Step 4: Verify full build compiles**

Run: full build of fate_engine and FateEngine targets
Expected: Compiles cleanly. entity.h unchanged, still uses legacy ComponentTypeId (std::type_index). Game components use FATE_LEGACY_COMPONENT. New FATE_COMPONENT macro exists in component_registry.h but is not yet used by any game code.

- [ ] **Step 5: Commit**

```bash
git add engine/ecs/component.h engine/ecs/component_registry.h game/components/ game/systems/spawn_system.h
git commit -m "feat(ecs): add compile-time component registry, tier classification, SoA descriptors"
```

---

## Task 4: Archetype Storage Core

**Files:**
- Create: `engine/ecs/archetype.h`
- Create: `engine/ecs/command_buffer.h`
- Create: `tests/test_archetype.cpp`

- [ ] **Step 1: Write archetype tests**

```cpp
// tests/test_archetype.cpp
#include <doctest/doctest.h>
#include "engine/ecs/archetype.h"
#include "engine/ecs/component_registry.h"
#include "engine/memory/arena.h"

namespace {

struct Position {
    FATE_COMPONENT_HOT(Position)
    float x = 0.0f;
    float y = 0.0f;
};

template<> struct fate::SoADescriptor<Position> {
    static constexpr auto fields = std::make_tuple(
        fate::SoAField{"x", &Position::x},
        fate::SoAField{"y", &Position::y}
    );
    static constexpr bool isSoA = true;
};

struct Health {
    FATE_COMPONENT(Health)
    float hp = 100.0f;
    float maxHp = 100.0f;
};

struct Name {
    FATE_COMPONENT_COLD(Name)
    char name[32] = "unnamed";
};

} // anonymous namespace

TEST_CASE("Archetype creation and entity add") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::ArchetypeStorage storage(arena);

    auto archId = storage.findOrCreateArchetype({
        fate::componentId<Position>(),
        fate::componentId<Health>()
    });
    CHECK(archId != fate::INVALID_ARCHETYPE);

    auto row = storage.addEntity(archId, fate::EntityHandle(1, 1));
    CHECK(row != fate::INVALID_ROW);

    // Write position data
    auto* posCol = storage.getColumn<Position>(archId);
    REQUIRE(posCol != nullptr);
}

TEST_CASE("Archetype swap-and-pop on entity remove") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::ArchetypeStorage storage(arena);

    auto archId = storage.findOrCreateArchetype({
        fate::componentId<Position>()
    });

    auto row0 = storage.addEntity(archId, fate::EntityHandle(1, 1));
    auto row1 = storage.addEntity(archId, fate::EntityHandle(2, 1));
    auto row2 = storage.addEntity(archId, fate::EntityHandle(3, 1));

    // Remove middle entity — last entity should swap in
    storage.removeEntity(archId, row1);
    CHECK(storage.entityCount(archId) == 2);
}

TEST_CASE("Archetype migration on addComponent") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::ArchetypeStorage storage(arena);

    // Create entity with Position only
    auto archA = storage.findOrCreateArchetype({
        fate::componentId<Position>()
    });
    auto row = storage.addEntity(archA, fate::EntityHandle(1, 1));

    // Migrate to Position + Health
    auto archB = storage.migrateEntity(
        archA, row, fate::EntityHandle(1, 1),
        fate::componentId<Health>(), true // adding
    );
    CHECK(archB != archA);
    CHECK(storage.entityCount(archA) == 0);
    CHECK(storage.entityCount(archB) == 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: FAIL — `archetype.h` doesn't exist

- [ ] **Step 3: Create archetype.h — core storage**

```cpp
// engine/ecs/archetype.h
#pragma once
#include "engine/ecs/entity_handle.h"
#include "engine/ecs/component_registry.h"
#include "engine/memory/arena.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <functional>
#include <span>

namespace fate {

using ArchetypeId = uint32_t;
using RowIndex = uint32_t;
constexpr ArchetypeId INVALID_ARCHETYPE = UINT32_MAX;
constexpr RowIndex INVALID_ROW = UINT32_MAX;

// Type-erased column in an archetype — stores N instances of one component type
struct ArchetypeColumn {
    CompId typeId = 0;
    size_t elemSize = 0;
    size_t elemAlign = 0;
    uint8_t* data = nullptr;
    size_t capacity = 0;

    // Optional destructor for non-trivial types (per-archetype-column, not per-entity)
    std::function<void(void*, size_t)> destroyFn;

    void* at(RowIndex row) {
        return data + row * elemSize;
    }
    const void* at(RowIndex row) const {
        return data + row * elemSize;
    }
};

// One archetype — a unique set of component types with contiguous storage
struct Archetype {
    ArchetypeId id = INVALID_ARCHETYPE;
    std::vector<CompId> typeIds; // sorted
    std::vector<ArchetypeColumn> columns;
    std::vector<EntityHandle> handles;    // which entity is in each row
    uint32_t count = 0;                   // active entity count
    uint32_t capacity = 0;

    // Map from CompId to column index for O(1) lookup
    std::unordered_map<CompId, size_t> typeToColumn;

    int getColumnIndex(CompId typeId) const {
        auto it = typeToColumn.find(typeId);
        return (it != typeToColumn.end()) ? static_cast<int>(it->second) : -1;
    }

    bool hasType(CompId typeId) const {
        return typeToColumn.find(typeId) != typeToColumn.end();
    }
};

// Manages all archetypes for a World, backed by a zone arena
class ArchetypeStorage {
public:
    explicit ArchetypeStorage(Arena& arena, uint32_t initialCapacity = 64)
        : arena_(arena), initialCapacity_(initialCapacity) {}

    // Find existing archetype or create new one from sorted type ID set
    ArchetypeId findOrCreateArchetype(std::vector<CompId> typeIds) {
        std::sort(typeIds.begin(), typeIds.end());

        // Check cache
        auto it = signatureToArchetype_.find(typeIds);
        if (it != signatureToArchetype_.end()) return it->second;

        // Create new
        ArchetypeId id = static_cast<ArchetypeId>(archetypes_.size());
        archetypes_.emplace_back();
        auto& arch = archetypes_.back();
        arch.id = id;
        arch.typeIds = typeIds;
        arch.capacity = initialCapacity_;
        arch.count = 0;
        arch.handles.resize(initialCapacity_);

        // Create columns
        for (size_t i = 0; i < typeIds.size(); ++i) {
            ArchetypeColumn col;
            col.typeId = typeIds[i];

            // Look up registered size/alignment
            auto regIt = typeRegistry_.find(typeIds[i]);
            if (regIt != typeRegistry_.end()) {
                col.elemSize = regIt->second.size;
                col.elemAlign = regIt->second.alignment;
                col.destroyFn = regIt->second.destroyFn;
            } else {
                col.elemSize = 1; // fallback, shouldn't happen
                col.elemAlign = 1;
            }

            col.data = static_cast<uint8_t*>(
                arena_.push(col.elemSize * initialCapacity_, col.elemAlign));
            col.capacity = initialCapacity_;

            arch.typeToColumn[typeIds[i]] = i;
            arch.columns.push_back(std::move(col));
        }

        signatureToArchetype_[typeIds] = id;
        ++version_;
        return id;
    }

    // Register a component type's size, alignment, and optional destructor
    template<typename T>
    void registerType() {
        TypeInfo info;
        info.size = sizeof(T);
        info.alignment = alignof(T);
        if constexpr (!std::is_trivially_destructible_v<T>) {
            info.destroyFn = [](void* data, size_t count) {
                T* arr = static_cast<T*>(data);
                for (size_t i = 0; i < count; ++i) {
                    arr[i].~T();
                }
            };
        }
        typeRegistry_[componentId<T>()] = info;
    }

    // Add entity to archetype, returns row index
    RowIndex addEntity(ArchetypeId archId, EntityHandle handle) {
        auto& arch = archetypes_[archId];

        // Grow if needed
        if (arch.count >= arch.capacity) {
            growArchetype(arch);
        }

        RowIndex row = arch.count++;
        arch.handles[row] = handle;

        // Zero-initialize component data
        for (auto& col : arch.columns) {
            std::memset(col.at(row), 0, col.elemSize);
        }

        return row;
    }

    // Remove entity from archetype (swap-and-pop)
    // Returns the handle that was swapped into the vacated row, or NULL_ENTITY_HANDLE
    EntityHandle removeEntity(ArchetypeId archId, RowIndex row) {
        auto& arch = archetypes_[archId];
        if (row >= arch.count) return NULL_ENTITY_HANDLE;

        RowIndex lastRow = arch.count - 1;
        EntityHandle swapped = NULL_ENTITY_HANDLE;

        if (row != lastRow) {
            // Swap last into the removed slot
            swapped = arch.handles[lastRow];
            arch.handles[row] = swapped;

            for (auto& col : arch.columns) {
                std::memcpy(col.at(row), col.at(lastRow), col.elemSize);
            }
        }

        --arch.count;
        return swapped;
    }

    // Migrate entity: add or remove a component type.
    // Returns the new archetype ID and updates entityRow to the new row.
    ArchetypeId migrateEntity(ArchetypeId fromArchId, RowIndex fromRow,
                               EntityHandle handle,
                               CompId typeId, bool adding) {
        auto& fromArch = archetypes_[fromArchId];

        // Build new type set
        std::vector<CompId> newTypes = fromArch.typeIds;
        if (adding) {
            newTypes.push_back(typeId);
        } else {
            newTypes.erase(std::remove(newTypes.begin(), newTypes.end(), typeId),
                          newTypes.end());
        }

        ArchetypeId toArchId = findOrCreateArchetype(newTypes);
        auto& toArch = archetypes_[toArchId];

        // Add to destination
        RowIndex toRow = addEntity(toArchId, handle);

        // Copy shared columns
        for (auto& fromCol : fromArch.columns) {
            int toColIdx = toArch.getColumnIndex(fromCol.typeId);
            if (toColIdx >= 0) {
                auto& toCol = toArch.columns[toColIdx];
                std::memcpy(toCol.at(toRow), fromCol.at(fromRow), fromCol.elemSize);
            }
        }

        // Remove from source
        removeEntity(fromArchId, fromRow);

        return toArchId;
    }

    // Get typed column pointer for direct iteration
    template<typename T>
    T* getColumn(ArchetypeId archId) {
        auto& arch = archetypes_[archId];
        int colIdx = arch.getColumnIndex(componentId<T>());
        if (colIdx < 0) return nullptr;
        return static_cast<T*>(static_cast<void*>(arch.columns[colIdx].data));
    }

    // Get entity handles array
    EntityHandle* getHandles(ArchetypeId archId) {
        return archetypes_[archId].handles.data();
    }

    uint32_t entityCount(ArchetypeId archId) const {
        return archetypes_[archId].count;
    }

    // Version counter — incremented when archetypes are created/destroyed
    uint64_t version() const { return version_; }

    // Iterate all archetypes
    size_t archetypeCount() const { return archetypes_.size(); }
    Archetype& getArchetype(ArchetypeId id) { return archetypes_[id]; }
    const Archetype& getArchetype(ArchetypeId id) const { return archetypes_[id]; }

    // Run all column destructors (call before arena reset)
    void destroyAll() {
        for (auto& arch : archetypes_) {
            for (auto& col : arch.columns) {
                if (col.destroyFn && arch.count > 0) {
                    col.destroyFn(col.data, arch.count);
                }
            }
            arch.count = 0;
        }
    }

    // Iterate archetypes that contain ALL specified types
    template<typename Fn>
    void forEachMatchingArchetype(const std::vector<CompId>& required, Fn&& fn) {
        for (auto& arch : archetypes_) {
            if (arch.count == 0) continue;
            bool match = true;
            for (auto typeId : required) {
                if (!arch.hasType(typeId)) { match = false; break; }
            }
            if (match) fn(arch);
        }
    }

private:
    Arena& arena_;
    uint32_t initialCapacity_;
    std::vector<Archetype> archetypes_;
    uint64_t version_ = 0;

    // Signature (sorted type IDs) → archetype ID
    struct VectorHash {
        size_t operator()(const std::vector<CompId>& v) const {
            size_t seed = v.size();
            for (auto id : v) {
                seed ^= std::hash<CompId>{}(id) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };
    std::unordered_map<std::vector<CompId>, ArchetypeId, VectorHash> signatureToArchetype_;

    // Registered type info
    struct TypeInfo {
        size_t size = 0;
        size_t alignment = 0;
        std::function<void(void*, size_t)> destroyFn;
    };
    std::unordered_map<CompId, TypeInfo> typeRegistry_;

    void growArchetype(Archetype& arch) {
        uint32_t newCap = arch.capacity * 2;

        // Grow handles
        arch.handles.resize(newCap);

        // Grow each column
        for (auto& col : arch.columns) {
            uint8_t* newData = static_cast<uint8_t*>(
                arena_.push(col.elemSize * newCap, col.elemAlign));
            if (arch.count > 0) {
                std::memcpy(newData, col.data, col.elemSize * arch.count);
            }
            // Old memory is abandoned in arena (reclaimed on arena reset)
            col.data = newData;
            col.capacity = newCap;
        }

        arch.capacity = newCap;
    }
};

} // namespace fate
```

- [ ] **Step 4: Create command_buffer.h**

```cpp
// engine/ecs/command_buffer.h
#pragma once
#include <vector>
#include <functional>

namespace fate {

// Deferred command buffer for structural ECS changes during iteration.
// Commands execute after ALL active iterations complete.
class CommandBuffer {
public:
    void push(std::function<void()> cmd) {
        commands_.push_back(std::move(cmd));
    }

    void execute() {
        for (auto& cmd : commands_) cmd();
        commands_.clear();
    }

    bool empty() const { return commands_.empty(); }
    size_t size() const { return commands_.size(); }

private:
    std::vector<std::function<void()>> commands_;
};

} // namespace fate
```

- [ ] **Step 5: Run tests to verify they pass**

Run: build and run `fate_tests`
Expected: All archetype tests PASS

- [ ] **Step 6: Commit**

```bash
git add engine/ecs/archetype.h engine/ecs/command_buffer.h tests/test_archetype.cpp
git commit -m "feat(ecs): archetype storage core with SoA columns, migration, swap-and-pop"
```

---

## Task 5: Archetype Query System

**Files:**
- Create: `engine/ecs/archetype_query.h`

- [ ] **Step 1: Create archetype_query.h with cached matching**

```cpp
// engine/ecs/archetype_query.h
#pragma once
#include "engine/ecs/archetype.h"
#include <vector>

namespace fate {

// Cached query — stores matched archetype IDs for a given component set.
// Invalidated when the archetype storage version changes.
struct ArchetypeQuery {
    std::vector<CompId> required;
    std::vector<ArchetypeId> matchedArchetypes;
    uint64_t cachedVersion = 0;

    void refresh(ArchetypeStorage& storage) {
        if (cachedVersion == storage.version()) return;

        matchedArchetypes.clear();
        for (size_t i = 0; i < storage.archetypeCount(); ++i) {
            auto& arch = storage.getArchetype(static_cast<ArchetypeId>(i));
            bool match = true;
            for (auto typeId : required) {
                if (!arch.hasType(typeId)) { match = false; break; }
            }
            if (match) matchedArchetypes.push_back(arch.id);
        }
        cachedVersion = storage.version();
    }
};

} // namespace fate
```

- [ ] **Step 2: Verify build compiles**

- [ ] **Step 3: Commit**

```bash
git add engine/ecs/archetype_query.h
git commit -m "feat(ecs): cached archetype query matching with version-counter invalidation"
```

---

## Task 6: World Rewrite — Archetype-Backed with Entity Facade

**Files:**
- Modify: `engine/ecs/world.h`
- Modify: `engine/ecs/world.cpp`
- Modify: `engine/ecs/entity.h`
- Modify: `engine/ecs/entity.cpp`

This is the most critical and highest-risk task. The World switches to archetype storage internally while preserving the `entity->getComponent<T>()` API. **Work on a feature branch. Verify the game runs before merging.**

**CRITICAL DESIGN CONSTRAINT: Entity\* pointer stability.** Game code stores `Entity*` across frames (editor selection, mob AI target tracking, etc.). Entity MUST remain a heap-allocated object in the World — NOT a transient stack facade. The Entity object stores `(archetypeId, rowIndex)` which the World updates when archetype migration or swap-and-pop occurs. `getComponent<T>()` returns a pointer into the archetype column, which is stable as long as no archetype migration happens to that entity within the same frame.

- [ ] **Step 1: Rewrite world.h**

The new World owns an `ArchetypeStorage` (backed by a zone arena) AND keeps heap-allocated Entity objects for pointer stability. The `EntitySlot` changes to:

```cpp
struct EntitySlot {
    Entity* entity = nullptr;       // heap-allocated, stable pointer
    ArchetypeId archetypeId = INVALID_ARCHETYPE;
    RowIndex row = INVALID_ROW;
    uint32_t generation = 1;
    bool alive = false;
};
```

Key changes:
- `World` constructor takes an optional `Arena*` (zone arena). If nullptr, uses global new/delete (for backward compat during migration).
- `ArchetypeStorage archetypes_` member, initialized with the zone arena
- `forEach<T1, T2>` iterates matching archetypes (O(matching entities) instead of O(all entities))
- `forEachEntity` iterates all entity slots for editor compatibility
- `getEntity()` returns the stable `Entity*` from the slot
- `createEntityH()` creates Entity, adds to default archetype, returns handle
- `destroyEntity()` does swap-and-pop removal from archetype, updates affected entity's row index
- `CommandBuffer commandBuffer_` for deferred structural changes during iteration
- `destroyAll()` runs column destructors, then arena reset

When swap-and-pop moves the last entity into a vacated slot, the World must update the swapped entity's `EntitySlot::row` to reflect its new position. This is the critical bookkeeping step.

- [ ] **Step 2: Rewrite entity.h — bridge to archetype storage**

Entity remains heap-allocated with stable pointer, but its component storage changes from `unordered_map<ComponentTypeId, unique_ptr<Component>>` to archetype delegation:

```cpp
class Entity {
public:
    // Existing API preserved
    EntityId id() const;
    const std::string& name() const;
    void setName(const std::string& name);
    bool isActive() const;
    void setActive(bool active);
    const std::string& tag() const;
    void setTag(const std::string& tag);
    EntityHandle handle() const;

    // Component access — delegates to archetype storage via World
    template<typename T>
    T* getComponent() const {
        if (!world_) return nullptr;
        return world_->getComponentFromArchetype<T>(archetypeId_, row_);
    }

    template<typename T, typename... Args>
    T* addComponent(Args&&... args) {
        if (!world_) return nullptr;
        return world_->addComponentToEntity<T>(handle_, std::forward<Args>(args)...);
    }

    template<typename T>
    bool hasComponent() const {
        if (!world_) return nullptr;
        return world_->hasComponentInArchetype<T>(archetypeId_);
    }

    template<typename T>
    void removeComponent() {
        if (world_) world_->removeComponentFromEntity<T>(handle_);
    }

    size_t componentCount() const;

private:
    friend class World; // World updates archetypeId_ and row_ on migration
    EntityId id_;
    EntityHandle handle_;
    World* world_ = nullptr;
    ArchetypeId archetypeId_ = INVALID_ARCHETYPE;
    RowIndex row_ = INVALID_ROW;
    std::string name_;
    std::string tag_;
    bool active_ = true;
};
```

The `forEachComponent()` method is removed (it relied on polymorphic iteration). If the prefab system uses it, that will be fixed in Step 5.

- [ ] **Step 3: Update entity.cpp**

Minimal — most logic moves to World or is inline in the facade.

- [ ] **Step 4: Update world.cpp**

Implement the new internal storage, entity creation, destruction, forEach, and deferred command execution.

- [ ] **Step 5: Build and fix compilation errors**

This step will likely require iterating through compilation errors in game code that depends on the old Entity/World interface. The facade should handle most cases, but some patterns may need adjustment.

Files likely needing minor fixes:
- `engine/editor/editor.cpp` — uses `forEachEntity`, `forEach<T1, T2>`, `entity->getComponent<T>()`
- `engine/ecs/prefab.h` / `prefab.cpp` — entity creation from JSON
- All game systems — `forEach` usage patterns

- [ ] **Step 6: Run full build and verify game launches**

Run: full build of FateEngine, launch game
Expected: Game runs with same behavior as before. Entities render, editor works, combat works.

- [ ] **Step 7: Commit**

```bash
git add engine/ecs/world.h engine/ecs/world.cpp engine/ecs/entity.h engine/ecs/entity.cpp
git commit -m "feat(ecs): archetype-backed World with Entity facade for backward compatibility"
```

---

## Task 7: Spatial Grid — Fixed Power-of-Two

**Files:**
- Create: `engine/spatial/spatial_grid.h`
- Create: `engine/spatial/spatial_index.h`
- Modify: `engine/spatial/spatial_hash.h`
- Create: `tests/test_spatial_grid.cpp`

- [ ] **Step 1: Write spatial grid tests**

```cpp
// tests/test_spatial_grid.cpp
#include <doctest/doctest.h>
#include "engine/spatial/spatial_grid.h"
#include "engine/memory/arena.h"

TEST_CASE("SpatialGrid insert and queryRadius") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::SpatialGrid grid;
    grid.init(arena, 200, 200, 32, 128.0f); // 200x200 tiles, 32px tiles, 128px cells

    grid.beginRebuild(10);
    grid.addEntity(fate::EntityHandle(1, 1), 100.0f, 100.0f);
    grid.addEntity(fate::EntityHandle(2, 1), 110.0f, 105.0f);
    grid.addEntity(fate::EntityHandle(3, 1), 5000.0f, 5000.0f); // far away
    grid.endRebuild();

    auto results = grid.queryRadius(100.0f, 100.0f, 50.0f);
    CHECK(results.size() == 2); // entities 1 and 2

    auto far = grid.queryRadius(5000.0f, 5000.0f, 50.0f);
    CHECK(far.size() == 1); // entity 3 only
}

TEST_CASE("SpatialGrid findNearest") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::SpatialGrid grid;
    grid.init(arena, 100, 100, 32, 128.0f);

    grid.beginRebuild(3);
    grid.addEntity(fate::EntityHandle(1, 1), 0.0f, 0.0f);
    grid.addEntity(fate::EntityHandle(2, 1), 50.0f, 0.0f);
    grid.addEntity(fate::EntityHandle(3, 1), 200.0f, 0.0f);
    grid.endRebuild();

    auto nearest = grid.findNearest(40.0f, 0.0f, 100.0f);
    REQUIRE(nearest.has_value());
    CHECK(nearest.value().index() == 2); // entity 2 is closest to (40,0)
}
```

- [ ] **Step 2: Create spatial_index.h — concept interface**

```cpp
// engine/spatial/spatial_index.h
#pragma once
#include "engine/ecs/entity_handle.h"
#include <span>
#include <expected>
#include <concepts>

namespace fate {

enum class SpatialError : uint8_t {
    NotFound,
    OutOfBounds,
    ChunkNotLoaded
};

template<typename T>
concept SpatialIndex = requires(T t, float x, float y, float r, EntityHandle h) {
    { t.beginRebuild(uint32_t{}) };
    { t.addEntity(h, x, y) };
    { t.endRebuild() };
    { t.queryRadius(x, y, r) } -> std::same_as<std::span<const EntityHandle>>;
    { t.findNearest(x, y, r) } -> std::same_as<std::expected<EntityHandle, SpatialError>>;
    { t.entityCount() } -> std::convertible_to<uint32_t>;
};

} // namespace fate
```

- [ ] **Step 3: Create spatial_grid.h**

```cpp
// engine/spatial/spatial_grid.h
#pragma once
#include "engine/ecs/entity_handle.h"
#include "engine/memory/arena.h"
#include "engine/memory/scratch_arena.h"
#include <span>
#include <expected>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>

namespace fate {

enum class SpatialError : uint8_t;

// Fixed power-of-two spatial grid for bounded tile worlds.
// Two shifts and an OR for cell lookup — zero hash computation, zero collisions.
class SpatialGrid {
public:
    SpatialGrid() = default;

    // Initialize grid from map dimensions.
    // mapWidth/mapHeight in tiles, tileSize in pixels, cellSizePx in pixels.
    void init(Arena& arena, int mapWidthTiles, int mapHeightTiles,
              int tileSize = 32, float cellSizePx = 128.0f) {
        tileSize_ = tileSize;
        cellSizePx_ = cellSizePx;
        invCellSize_ = 1.0f / cellSizePx;

        // Cell dimensions in tiles
        cellTiles_ = static_cast<int>(cellSizePx / tileSize);
        if (cellTiles_ < 1) cellTiles_ = 1;

        // Compute cellBits (log2 of cell size in tiles)
        cellBits_ = 0;
        int tmp = cellTiles_;
        while (tmp > 1) { ++cellBits_; tmp >>= 1; }

        // Grid dimensions
        int cellsX = (mapWidthTiles + cellTiles_ - 1) / cellTiles_;
        int cellsY = (mapHeightTiles + cellTiles_ - 1) / cellTiles_;
        int maxCells = cellsX > cellsY ? cellsX : cellsY;

        // gridBits = ceil(log2(maxCells))
        gridBits_ = 0;
        tmp = 1;
        while (tmp < maxCells) { ++gridBits_; tmp <<= 1; }

        gridSize_ = 1 << gridBits_;
        totalCells_ = gridSize_ * gridSize_;

        // Allocate cell arrays from arena
        cellCounts_ = arena.pushArray<uint32_t>(totalCells_);
        cellStarts_ = arena.pushArray<uint32_t>(totalCells_ + 1);
    }

    void beginRebuild(uint32_t maxEntities) {
        count_ = 0;
        handles_.clear();
        handles_.reserve(maxEntities);
        posX_.clear(); posX_.reserve(maxEntities);
        posY_.clear(); posY_.reserve(maxEntities);
        cellHashes_.clear(); cellHashes_.reserve(maxEntities);

        for (uint32_t i = 0; i < totalCells_; ++i) cellCounts_[i] = 0;
    }

    void addEntity(EntityHandle handle, float px, float py) {
        uint32_t cell = cellIndex(px, py);
        handles_.push_back(handle);
        posX_.push_back(px);
        posY_.push_back(py);
        cellHashes_.push_back(cell);
        cellCounts_[cell]++;
        ++count_;
    }

    void endRebuild() {
        // Prefix sum
        cellStarts_[0] = 0;
        for (uint32_t i = 0; i < totalCells_; ++i) {
            cellStarts_[i + 1] = cellStarts_[i] + cellCounts_[i];
        }

        // Counting sort into sorted arrays
        sorted_.resize(count_);
        std::vector<uint32_t> cursor(cellStarts_, cellStarts_ + totalCells_);
        for (uint32_t i = 0; i < count_; ++i) {
            uint32_t cell = cellHashes_[i];
            sorted_[cursor[cell]++] = i;
        }
    }

    // Query all entities within radius. Returns span into scratch arena memory.
    std::span<const EntityHandle> queryRadius(float cx, float cy, float radius) {
        auto scratch = GetScratch();
        float rSq = radius * radius;

        int minCX, minCY, maxCX, maxCY;
        cellRange(cx, cy, radius, minCX, minCY, maxCX, maxCY);

        // Allocate result buffer from scratch arena
        auto* resultBuf = static_cast<EntityHandle*>(
            scratch.arena->push(sizeof(EntityHandle) * count_, alignof(EntityHandle)));
        uint32_t resultCount = 0;

        for (int y = minCY; y <= maxCY; ++y) {
            for (int x = minCX; x <= maxCX; ++x) {
                if (x < 0 || y < 0 || x >= gridSize_ || y >= gridSize_) continue;
                uint32_t cell = (y << gridBits_) | x;
                uint32_t start = cellStarts_[cell];
                uint32_t end = cellStarts_[cell + 1];

                for (uint32_t s = start; s < end; ++s) {
                    uint32_t idx = sorted_[s];
                    float dx = posX_[idx] - cx;
                    float dy = posY_[idx] - cy;
                    if (dx * dx + dy * dy <= rSq) {
                        resultBuf[resultCount++] = handles_[idx];
                    }
                }
            }
        }

        return std::span<const EntityHandle>(resultBuf, resultCount);
    }

    // Find nearest entity within radius.
    std::expected<EntityHandle, SpatialError> findNearest(float cx, float cy, float radius) {
        float bestDistSq = radius * radius;
        EntityHandle best = NULL_ENTITY_HANDLE;

        int minCX, minCY, maxCX, maxCY;
        cellRange(cx, cy, radius, minCX, minCY, maxCX, maxCY);

        for (int y = minCY; y <= maxCY; ++y) {
            for (int x = minCX; x <= maxCX; ++x) {
                if (x < 0 || y < 0 || x >= gridSize_ || y >= gridSize_) continue;
                uint32_t cell = (y << gridBits_) | x;
                uint32_t start = cellStarts_[cell];
                uint32_t end = cellStarts_[cell + 1];

                for (uint32_t s = start; s < end; ++s) {
                    uint32_t idx = sorted_[s];
                    float dx = posX_[idx] - cx;
                    float dy = posY_[idx] - cy;
                    float dSq = dx * dx + dy * dy;
                    if (dSq < bestDistSq) {
                        bestDistSq = dSq;
                        best = handles_[idx];
                    }
                }
            }
        }

        if (best.isNull()) return std::unexpected(SpatialError::NotFound);
        return best;
    }

    uint32_t entityCount() const { return count_; }

private:
    int tileSize_ = 32;
    float cellSizePx_ = 128.0f;
    float invCellSize_ = 1.0f / 128.0f;
    int cellTiles_ = 4;
    int cellBits_ = 2;
    int gridBits_ = 0;
    int gridSize_ = 0;
    uint32_t totalCells_ = 0;
    uint32_t count_ = 0;

    // Arena-allocated per-cell arrays
    uint32_t* cellCounts_ = nullptr;
    uint32_t* cellStarts_ = nullptr;

    // Per-frame entity data (rebuilt each frame)
    std::vector<EntityHandle> handles_;
    std::vector<float> posX_;
    std::vector<float> posY_;
    std::vector<uint32_t> cellHashes_;
    std::vector<uint32_t> sorted_;

    uint32_t cellIndex(float px, float py) const {
        int tx = static_cast<int>(std::floor(px / cellSizePx_));
        int ty = static_cast<int>(std::floor(py / cellSizePx_));
        // Clamp to grid bounds
        if (tx < 0) tx = 0; if (tx >= gridSize_) tx = gridSize_ - 1;
        if (ty < 0) ty = 0; if (ty >= gridSize_) ty = gridSize_ - 1;
        return (ty << gridBits_) | tx;
    }

    void cellRange(float cx, float cy, float radius,
                   int& minCX, int& minCY, int& maxCX, int& maxCY) const {
        minCX = static_cast<int>(std::floor((cx - radius) / cellSizePx_));
        minCY = static_cast<int>(std::floor((cy - radius) / cellSizePx_));
        maxCX = static_cast<int>(std::floor((cx + radius) / cellSizePx_));
        maxCY = static_cast<int>(std::floor((cy + radius) / cellSizePx_));
    }
};

} // namespace fate
```

- [ ] **Step 4: Modernize spatial_hash.h — add SpatialError enum, keep for fallback use**

Move `SpatialError` enum to `spatial_index.h` (done in step 2). Update `SpatialHashEngine` to use `EntityHandle` instead of `EntityId` in its stored entries. Keep the existing Mueller hash implementation intact.

- [ ] **Step 5: Run tests to verify they pass**

- [ ] **Step 6: Commit**

```bash
git add engine/spatial/spatial_grid.h engine/spatial/spatial_index.h engine/spatial/spatial_hash.h tests/test_spatial_grid.cpp
git commit -m "feat(spatial): fixed power-of-two spatial grid with SpatialIndex concept"
```

---

## Task 8: Chunk Lifecycle Upgrade — 7-State Machine with Tickets

**Files:**
- Modify: `engine/tilemap/chunk.h`
- Modify: `engine/tilemap/tilemap.h` / `tilemap.cpp`
- Create: `tests/test_chunk_lifecycle.cpp`

- [ ] **Step 1: Write chunk lifecycle tests**

Test state transitions, ticket system, rate limiting. Verify that ticket levels map correctly to chunk states, that rate limiting caps transitions per frame, and that concentric ring logic produces correct ticket levels.

- [ ] **Step 2: Rewrite chunk.h — 7-state machine with tickets**

Replace `ChunkState` enum (3 states) with 7 states. Add `ticketLevel` field, staging buffer pointer, dirty flag. Add `ChunkTicket` struct for ticket management. Update `ChunkManager::updateChunkStates` to use tickets and rate-limit transitions.

Key changes:
- `ChunkState`: Queued, Loading, Setup, Active, Sleeping, Unloading, Evicted
- `ChunkData` gets: `ticketLevel`, `int* stagingTiles` (double buffer), `bool dirty`
- `ChunkManager` gets: `maxTransitionsPerFrame`, `prefetchBuffer`, priority queue for loading
- Ticket level → state mapping table from spec

- [ ] **Step 3: Update tilemap.h/cpp for new chunk states**

The tilemap rendering and collision checking should only process Active chunks. Sleeping/Loading chunks are skipped. Update `buildFromLayers` to set initial state.

- [ ] **Step 4: Run tests, verify full build, verify game launches**

- [ ] **Step 5: Commit**

```bash
git add engine/tilemap/chunk.h engine/tilemap/tilemap.h engine/tilemap/tilemap.cpp tests/test_chunk_lifecycle.cpp
git commit -m "feat(tilemap): 7-state chunk lifecycle with ticket system and rate-limited transitions"
```

---

## Task 9: Zone Snapshot & Persistent Entity IDs

**Files:**
- Create: `engine/memory/zone_snapshot.h`
- Create: `engine/ecs/persistent_id.h`

- [ ] **Step 1: Create persistent_id.h**

64-bit ID: `(zoneId:16 | creationTime:32 | sequence:16)`. Include overflow handling (re-read clock on sequence wrap). Provide `PersistentId::generate()` and `PersistentId::null()`.

- [ ] **Step 2: Create zone_snapshot.h**

`ZoneSnapshot` struct holding:
- Map of `PersistentId` → serialized entity data (binary blob per entity)
- Spawn zone state (tracked mobs, respawn timers as absolute timestamps)
- Chunk dirty flags
- Version header

Provide `serialize(World&, ...)` and `restore(World&, ...)` methods.

The snapshot itself is allocated from the global persistent arena. Entity data within the snapshot is a flat binary buffer per entity: `[PersistentId][componentCount][typeId, size, data]...`

- [ ] **Step 3: Verify build compiles**

- [ ] **Step 4: Commit**

```bash
git add engine/ecs/persistent_id.h engine/memory/zone_snapshot.h
git commit -m "feat(persistence): zone snapshots with persistent entity IDs"
```

---

## Task 10: Game Component Migration

**Files:**
- Modify: `game/components/transform.h`
- Modify: `game/components/sprite_component.h`
- Modify: `game/components/player_controller.h`
- Modify: `game/components/box_collider.h`
- Modify: `game/components/animator.h`
- Modify: `game/components/zone_component.h`
- Modify: `game/components/game_components.h`

- [ ] **Step 1: Migrate Transform to new macro (Warm tier, Vec2 preserved)**

Remove `Component` base class inheritance. Replace `FATE_LEGACY_COMPONENT` with `FATE_COMPONENT` (Warm tier, NOT Hot). The `Vec2 position` member is kept as-is — no SoA field splitting. Transform is stored as a contiguous typed array in the archetype, which is already a massive improvement over the old unordered_map pointer chasing. True SoA field splitting (flat `float pos_x[], pos_y[]`) is a future SIMD optimization task.

```cpp
// game/components/transform.h
#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"

namespace fate {
struct Transform {
    FATE_COMPONENT(Transform)

    Vec2 position;
    float rotation = 0.0f;
    float depth = 0.0f;
    Vec2 scale = Vec2::one();
};
} // namespace fate
```

- [ ] **Step 2: Migrate all other game components**

For each component:
- Remove `: public Component` inheritance
- Replace `FATE_LEGACY_COMPONENT(X)` with `FATE_COMPONENT(X)` (Warm) or `FATE_COMPONENT_COLD(X)` (Cold)
- Components with non-trivial members (vectors, strings, maps) keep their members but get registered with the archetype cleanup callback system

Classification:
- **Warm (FATE_COMPONENT):** Transform, SpriteComponent, PlayerController, BoxCollider, Animator, MobAIComponent, EnemyStatsComponent, CombatControllerComponent, DamageableComponent, TargetingComponent, MobNameplateComponent
- **Cold (FATE_COMPONENT_COLD):** All social components (guild, party, friends, trade, market, chat), InventoryComponent, SkillManagerComponent, StatusEffectComponent, CrowdControlComponent, SpawnZoneComponent, NameplateComponent

Note: Hot tier (SoA field splitting) is deferred. All components are Warm or Cold for now.

- [ ] **Step 3: Update game_components.h — register all types with archetype storage**

Add a `registerGameComponents(ArchetypeStorage&)` function that calls `storage.registerType<T>()` for every game component.

- [ ] **Step 4: Build and fix compilation errors throughout game code**

The facade should handle most cases. Fix any remaining issues where game code assumed the old `Component` base class.

- [ ] **Step 5: Run game and verify all systems work**

- [ ] **Step 6: Commit**

```bash
git add game/components/ game/systems/spawn_system.h
git commit -m "feat(game): migrate all components to new archetype-compatible type system"
```

---

## Task 11: Scene & SceneManager — Zone Arena Lifecycle

**Files:**
- Modify: `engine/scene/scene.h` / `scene.cpp`
- Modify: `engine/scene/scene_manager.h` / `scene_manager.cpp`
- Modify: `game/game_app.h` / `game_app.cpp`

- [ ] **Step 1: Update Scene to own a zone arena**

Scene creates a zone arena (4 GB reserve) on construction. The World within the Scene is backed by this arena. On `onExit()`, the scene serializes state to a ZoneSnapshot (stored in global persistent arena), then calls `world.destroyAll()` followed by `arena.reset()`.

- [ ] **Step 2: Update SceneManager for zone transitions with loading screen**

Add `transitionToScene(name, entryPoint)` method that orchestrates:
1. Call `onExit()` on current scene (serialize + arena reset)
2. Set loading state flag
3. Create new scene with fresh arena
4. Load chunks in priority order (rate-limited)
5. Restore from snapshot if one exists
6. Call `onEnter()` on new scene
7. Clear loading state

Add `isLoading()` query and `loadProgress()` (0.0 - 1.0) for UI.

- [ ] **Step 3: Update GameApp — global persistent arena, frame arena upgrade**

Add global persistent arena initialization in `onInit()`. Update frame arena reserve to 64 MB. Wire scene transitions through the new SceneManager API.

- [ ] **Step 4: Build, run, verify scene system works**

- [ ] **Step 5: Commit**

```bash
git add engine/scene/ game/game_app.h game/game_app.cpp
git commit -m "feat(scene): zone arena lifecycle with snapshot persistence and loading screen flow"
```

---

## Task 12: Tracy Profiler Integration

**Files:**
- Create: `engine/profiling/tracy_zones.h`
- Modify: `engine/memory/arena.h`

- [ ] **Step 1: Create tracy_zones.h — macro wrappers**

```cpp
// engine/profiling/tracy_zones.h
#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#define FATE_ZONE(name, color) ZoneScopedNC(name, color)
#define FATE_ZONE_NAME(name) ZoneScopedN(name)
#define FATE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define FATE_FREE(ptr) TracyFree(ptr)
#define FATE_FRAME_MARK FrameMark
#else
#define FATE_ZONE(name, color)
#define FATE_ZONE_NAME(name)
#define FATE_ALLOC(ptr, size)
#define FATE_FREE(ptr)
#define FATE_FRAME_MARK
#endif
```

- [ ] **Step 2: Add Tracy zones to arena push/reset**

Wrap `Arena::push()` with `FATE_ALLOC` and `Arena::reset()` with `FATE_ZONE("Arena_Reset", 0xFF00FF)`.

- [ ] **Step 3: Add Tracy zones to spatial grid, chunk manager, sprite batch, world forEach**

Instrument the hot paths identified in the spec:
- `SpatialGrid::endRebuild()` — "SpatialGrid_Rebuild", orange
- `SpatialGrid::queryRadius()` — "SpatialGrid_Query", green
- `ChunkManager::updateChunkStates()` — "ChunkManager_Update", blue
- SpriteBatch sort — "SpriteBatch_Sort", red

- [ ] **Step 4: Add FATE_FRAME_MARK and FrameArena::swap() to main loop**

In `app.cpp` main loop:
- Add `FATE_FRAME_MARK` at the end of each frame
- Add `frameArena_.swap()` at the start of each frame (before update). This resets the current frame's scratch allocations while preserving the previous frame's data.
- Add ScratchArena reset at frame start (reset both thread-local scratch arenas)

- [ ] **Step 5: Build, verify Tracy zones compile, verify game runs**

- [ ] **Step 6: Commit**

```bash
git add engine/profiling/tracy_zones.h engine/memory/arena.h engine/spatial/ engine/tilemap/ engine/render/ engine/app.cpp
git commit -m "feat(profiling): Tracy profiler integration with named zones and arena tracking"
```

---

## Task 13: SpriteBatch Dirty Flag

**Files:**
- Modify: `engine/render/sprite_batch.h`
- Modify: `engine/render/sprite_batch.cpp`

- [ ] **Step 1: Add sort hash and dirty flag to SpriteBatch**

Add `sortDirty_` bool and `prevSortHash_` uint32_t. In `end()`, compute a hash of `(textureId, depth)` across all entries. If it matches `prevSortHash_`, skip the sort. Otherwise, sort and update the hash.

- [ ] **Step 2: Build, run, verify rendering is unchanged**

- [ ] **Step 3: Commit**

```bash
git add engine/render/sprite_batch.h engine/render/sprite_batch.cpp
git commit -m "feat(render): SpriteBatch dirty flag skips sort when draw order unchanged"
```

---

## Task 14: Multiplayer Groundwork — AOI, Ghost, Delta Markers

**Files:**
- Create: `engine/net/aoi.h`
- Create: `engine/net/ghost.h`

- [ ] **Step 1: Create aoi.h — visibility sets with hysteresis**

```cpp
// engine/net/aoi.h
#pragma once
#include "engine/ecs/entity_handle.h"
#include <vector>
#include <algorithm>
#include <cstdint>

namespace fate {

// Area of Interest configuration
struct AOIConfig {
    float activationRadius = 320.0f;   // 10 tiles * 32px
    float deactivationRadius = 384.0f; // 20% larger for hysteresis
};

// Tracks which entities are visible to a player across frames.
// Computes enter/leave/stay diffs for future network replication.
struct VisibilitySet {
    std::vector<EntityHandle> current;
    std::vector<EntityHandle> previous;

    // Diff results (populated by computeDiff)
    std::vector<EntityHandle> entered;
    std::vector<EntityHandle> left;
    std::vector<EntityHandle> stayed;

    void computeDiff() {
        entered.clear();
        left.clear();
        stayed.clear();

        // Both arrays must be sorted for set operations
        std::sort(current.begin(), current.end());
        std::sort(previous.begin(), previous.end());

        // Entered = in current but not previous
        std::set_difference(current.begin(), current.end(),
                          previous.begin(), previous.end(),
                          std::back_inserter(entered));

        // Left = in previous but not current
        std::set_difference(previous.begin(), previous.end(),
                          current.begin(), current.end(),
                          std::back_inserter(left));

        // Stayed = intersection
        std::set_intersection(current.begin(), current.end(),
                            previous.begin(), previous.end(),
                            std::back_inserter(stayed));
    }

    void advance() {
        previous = std::move(current);
        current.clear();
    }
};

} // namespace fate
```

- [ ] **Step 2: Create ghost.h — ghost entity scaffold**

```cpp
// engine/net/ghost.h
#pragma once
#include "engine/ecs/entity_handle.h"
#include "engine/ecs/component_registry.h"
#include "engine/memory/arena.h"

namespace fate {

// Marker component for ghost (read-only proxy) entities
struct GhostFlag {
    FATE_COMPONENT_COLD(GhostFlag)
    EntityHandle sourceHandle;   // the real entity this ghosts
    uint16_t sourceZoneId = 0;   // zone the real entity lives in
};

// Small dedicated arena for ghost entities (separate from zone arenas)
class GhostArena {
public:
    explicit GhostArena(size_t reserve = 16 * 1024 * 1024)
        : arena_(reserve) {}

    Arena& arena() { return arena_; }
    void reset() { arena_.reset(); }

private:
    Arena arena_;
};

} // namespace fate
```

- [ ] **Step 3: Build, verify compilation**

- [ ] **Step 4: Commit**

```bash
git add engine/net/aoi.h engine/net/ghost.h
git commit -m "feat(net): AOI visibility sets with hysteresis, ghost entity scaffold"
```

---

## Task 15: MobAI System Spatial Unification

**Files:**
- Modify: `game/systems/mob_ai_system.h`

- [ ] **Step 1: Migrate MobAISystem from private SpatialHash to engine SpatialGrid**

Remove the `SpatialHash playerGrid_` member. Instead, accept a reference to the zone's `SpatialGrid` (passed from the World or injected via system init). Use the engine spatial grid's `queryRadius` with a filter for player entities (check for `CharacterStatsComponent`).

The DEAR tick scaling logic and attack resolution stay unchanged.

- [ ] **Step 2: Build, run, verify mob AI behaves identically**

- [ ] **Step 3: Commit**

```bash
git add game/systems/mob_ai_system.h
git commit -m "feat(game): unify MobAISystem onto engine SpatialGrid, remove duplicate spatial hash"
```

---

## Task 16: Final Integration & Verification

- [ ] **Step 1: Full clean build**

Run: clean build of all targets (FateEngine, fate_tests)
Expected: Zero warnings, zero errors

- [ ] **Step 2: Run all tests**

Run: `fate_tests`
Expected: All tests pass

- [ ] **Step 3: Run game and verify**

Launch FateEngine. Verify:
- Entities render correctly
- Editor hierarchy/inspector work
- Mob AI chases and attacks
- Spawn zones populate mobs
- Camera follows player
- Combat system functions
- Zone debug overlays render

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "feat(engine): complete research upgrade - archetype ECS, zone arenas, spatial grid, chunk lifecycle, Tracy, multiplayer groundwork"
```
