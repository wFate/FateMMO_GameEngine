# Engine Infrastructure Upgrade — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add arena allocators, generational entity handles, an optimized spatial hash, tilemap chunk system, and distance-based AI tick scaling to the FateMMO engine.

**Architecture:** Six new engine-level modules (`engine/memory/`, `engine/spatial/`, `engine/tilemap/chunk.h`, `engine/ecs/entity_handle.h`) plus integration changes to World, Tilemap, MobAISystem, and GameApp. Each task produces a compilable engine — no intermediate broken states. Proprietary game files that change are already in `.gitignore`.

**Tech Stack:** C++23, SDL2, OpenGL 3.3, nlohmann/json, VirtualAlloc (Windows memory API)

**Build command:** `"C:/Program Files/Microsoft Visual Studio/2025/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Debug`

**Configure command (if build/ doesn't exist):** `"C:/Program Files/Microsoft Visual Studio/2025/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" -S . -B build`

---

## Dependency Graph

```
Task 1: Arena Allocators ─────────────────────┐
Task 2: Generational Handles ─────────────────┤
Task 3: Spatial Hash (engine-level) ──────────┤──→ Task 6: Integration
Task 4: Chunk System ─────────────────────────┤
Task 5: DEAR Tick Scaling ────────────────────┘
```

Tasks 1–5 are independent of each other and can be built in parallel. Task 6 wires them together.

---

## File Map

### New engine files (public, git-tracked)
| File | Responsibility |
|------|---------------|
| `engine/memory/arena.h` | Frame arena + Zone arena (VirtualAlloc-backed bump allocators) |
| `engine/memory/pool.h` | Fixed-size pool allocator built on arena |
| `engine/ecs/entity_handle.h` | `EntityHandle` packed type + `HandlePool` slot manager |
| `engine/spatial/spatial_hash.h` | Müller dense spatial hash with flat arrays |
| `engine/tilemap/chunk.h` | `Chunk`, `ChunkState`, `ChunkManager` for tilemap streaming |

### Modified engine files (public, git-tracked)
| File | Change |
|------|--------|
| `engine/ecs/world.h` | Add slot-array entity storage, O(1) `getEntity(EntityHandle)` |
| `engine/ecs/world.cpp` | Rewrite `createEntity`/`destroyEntity`/`getEntity`/`processDestroyQueue` |
| `engine/core/types.h` | Add `EntityHandle` typedef alias for backward compat |
| `engine/tilemap/tilemap.h` | Add chunk-aware storage, delegate render/collision to ChunkManager |
| `engine/tilemap/tilemap.cpp` | Refactor `loadFromFile` to populate chunks, render via ChunkManager |
| `engine/app.h` | Add `FrameArena` member, expose via accessor |
| `engine/app.cpp` | Init/reset frame arena in game loop |

### Modified game files (proprietary, gitignored)
| File | Change |
|------|--------|
| `game/shared/spatial_hash.h` | Replace with thin wrapper around engine `SpatialHash` or update includes |
| `game/systems/mob_ai_system.h` | Add DEAR tick scaling, use engine spatial hash |
| `game/systems/spawn_system.h` | Update `TrackedMob.entityId` to `EntityHandle` |
| `game/systems/combat_action_system.h` | Update entity lookups to use handles |
| `game/systems/zone_system.h` | Update entity lookups to use handles |
| `game/systems/gameplay_system.h` | Update entity lookups to use handles |
| `game/shared/spawn_zone.h` | Update `TrackedMob.entityId` to `EntityHandle` |
| `game/components/zone_component.h` | Update `EntityId` references if any |
| `game/components/game_components.h` | Update `TargetingComponent.selectedTargetId` to `EntityHandle` |
| `game/game_app.cpp` | Wire frame arena, use engine spatial hash |

---

## Chunk 1: Foundation Systems (Tasks 1–2)

### Task 1: Arena Allocators

**Files:**
- Create: `engine/memory/arena.h`
- Create: `engine/memory/pool.h`
- Modify: `engine/app.h:49-61`
- Modify: `engine/app.cpp:90-106`

- [ ] **Step 1: Create `engine/memory/arena.h`**

```cpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace fate {

// ==========================================================================
// Arena — bump allocator backed by virtual memory
//
// Reserves a large virtual address range up front, commits pages on demand.
// All allocations freed in O(1) by resetting the position pointer.
// ==========================================================================
class Arena {
public:
    // Reserve `reserveSize` bytes of virtual address space.
    // No physical memory committed until push() is called.
    explicit Arena(size_t reserveSize = 256 * 1024 * 1024) {
        reserveSize_ = alignUp(reserveSize, pageSize());
#ifdef _WIN32
        base_ = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, reserveSize_, MEM_RESERVE, PAGE_NOACCESS));
#else
        base_ = static_cast<uint8_t*>(
            mmap(nullptr, reserveSize_, PROT_NONE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (base_ == MAP_FAILED) base_ = nullptr;
#endif
        assert(base_ && "Arena: virtual memory reservation failed");
    }

    ~Arena() {
        if (base_) {
#ifdef _WIN32
            VirtualFree(base_, 0, MEM_RELEASE);
#else
            munmap(base_, reserveSize_);
#endif
        }
    }

    // Non-copyable, movable
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& other) noexcept
        : base_(other.base_), pos_(other.pos_),
          committed_(other.committed_), reserveSize_(other.reserveSize_) {
        other.base_ = nullptr;
        other.pos_ = 0;
        other.committed_ = 0;
        other.reserveSize_ = 0;
    }
    Arena& operator=(Arena&& other) noexcept {
        if (this != &other) {
            this->~Arena();
            new (this) Arena(std::move(other));
        }
        return *this;
    }

    // Allocate `size` bytes with given alignment. Returns nullptr if out of reserved space.
    void* push(size_t size, size_t alignment = 16) {
        size_t aligned = alignUp(pos_, alignment);
        size_t newPos = aligned + size;
        if (newPos > reserveSize_) return nullptr;

        // Commit more pages if needed
        if (newPos > committed_) {
            size_t commitTarget = alignUp(newPos, pageSize());
            if (commitTarget > reserveSize_) commitTarget = reserveSize_;
#ifdef _WIN32
            VirtualAlloc(base_ + committed_, commitTarget - committed_,
                         MEM_COMMIT, PAGE_READWRITE);
#else
            mprotect(base_ + committed_, commitTarget - committed_,
                     PROT_READ | PROT_WRITE);
#endif
            committed_ = commitTarget;
        }

        pos_ = newPos;
        return base_ + aligned;
    }

    // Typed allocation helper
    template<typename T, typename... Args>
    T* pushType(Args&&... args) {
        void* mem = push(sizeof(T), alignof(T));
        if (!mem) return nullptr;
        return new (mem) T(std::forward<Args>(args)...);
    }

    // Allocate array of T
    template<typename T>
    T* pushArray(size_t count) {
        void* mem = push(sizeof(T) * count, alignof(T));
        if (!mem) return nullptr;
        // Default-construct
        T* arr = static_cast<T*>(mem);
        for (size_t i = 0; i < count; ++i)
            new (&arr[i]) T();
        return arr;
    }

    // Reset position — all allocations freed in O(1). Does NOT decommit pages.
    void reset() { pos_ = 0; }

    // Reset and decommit all but the first `keepBytes` of committed pages.
    void resetAndDecommit(size_t keepBytes = 0) {
        pos_ = 0;
        size_t keep = alignUp(keepBytes, pageSize());
        if (committed_ > keep) {
#ifdef _WIN32
            VirtualFree(base_ + keep, committed_ - keep, MEM_DECOMMIT);
#else
            madvise(base_ + keep, committed_ - keep, MADV_DONTNEED);
            mprotect(base_ + keep, committed_ - keep, PROT_NONE);
#endif
            committed_ = keep;
        }
    }

    // Stats
    size_t position() const { return pos_; }
    size_t committed() const { return committed_; }
    size_t reserved() const { return reserveSize_; }
    uint8_t* base() const { return base_; }

private:
    uint8_t* base_ = nullptr;
    size_t pos_ = 0;
    size_t committed_ = 0;
    size_t reserveSize_ = 0;

    static size_t alignUp(size_t value, size_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    static size_t pageSize() {
#ifdef _WIN32
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return si.dwPageSize; // typically 4096
#else
        return (size_t)sysconf(_SC_PAGESIZE);
#endif
    }
};

// ==========================================================================
// FrameArena — double-buffered arena for per-frame temporaries
//
// Call swap() at frame start. Current arena is for this frame's allocations;
// previous arena's data is still valid (for referencing last frame's results).
// ==========================================================================
class FrameArena {
public:
    explicit FrameArena(size_t reservePerBuffer = 16 * 1024 * 1024)
        : arenas_{Arena(reservePerBuffer), Arena(reservePerBuffer)} {}

    // Call at frame start. Resets the new current buffer.
    void swap() {
        current_ ^= 1;
        arenas_[current_].reset();
    }

    Arena& current() { return arenas_[current_]; }
    Arena& previous() { return arenas_[current_ ^ 1]; }

    void* push(size_t size, size_t alignment = 16) {
        return arenas_[current_].push(size, alignment);
    }

    template<typename T, typename... Args>
    T* pushType(Args&&... args) {
        return arenas_[current_].pushType<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T* pushArray(size_t count) {
        return arenas_[current_].pushArray<T>(count);
    }

private:
    Arena arenas_[2];
    int current_ = 0;
};

} // namespace fate
```

- [ ] **Step 2: Create `engine/memory/pool.h`**

```cpp
#pragma once
#include "engine/memory/arena.h"
#include <cstdint>
#include <cassert>

namespace fate {

// ==========================================================================
// PoolAllocator — fixed-size block pool built on top of an Arena
//
// Provides O(1) alloc/dealloc via embedded free list.
// When the backing arena resets, all pool state is implicitly freed.
// ==========================================================================
class PoolAllocator {
public:
    // blockSize: size of each allocation (minimum sizeof(void*))
    // blockCount: maximum number of blocks
    // arena: backing arena (pool lives as long as arena does)
    PoolAllocator() = default;

    void init(Arena& arena, size_t blockSize, size_t blockCount) {
        blockSize_ = blockSize < sizeof(void*) ? sizeof(void*) : blockSize;
        blockCount_ = blockCount;
        activeCount_ = 0;

        // Allocate contiguous block array from the arena
        memory_ = static_cast<uint8_t*>(arena.push(blockSize_ * blockCount_, 16));
        assert(memory_ && "PoolAllocator: arena allocation failed");

        // Build free list (each free block stores pointer to next free block)
        freeList_ = nullptr;
        for (size_t i = blockCount_; i > 0; --i) {
            void* block = memory_ + (i - 1) * blockSize_;
            *static_cast<void**>(block) = freeList_;
            freeList_ = block;
        }
    }

    void* alloc() {
        if (!freeList_) return nullptr; // pool exhausted
        void* block = freeList_;
        freeList_ = *static_cast<void**>(freeList_);
        ++activeCount_;
        return block;
    }

    void free(void* block) {
        if (!block) return;
        assert(block >= memory_ && block < memory_ + blockSize_ * blockCount_);
        *static_cast<void**>(block) = freeList_;
        freeList_ = block;
        --activeCount_;
    }

    size_t activeCount() const { return activeCount_; }
    size_t blockCount() const { return blockCount_; }
    size_t blockSize() const { return blockSize_; }
    bool isFull() const { return freeList_ == nullptr; }

private:
    uint8_t* memory_ = nullptr;
    void* freeList_ = nullptr;
    size_t blockSize_ = 0;
    size_t blockCount_ = 0;
    size_t activeCount_ = 0;
};

} // namespace fate
```

- [ ] **Step 3: Wire FrameArena into App**

In `engine/app.h`, add member and accessor:
```cpp
// Add include at top:
#include "engine/memory/arena.h"

// Add to private members (after line 60):
    FrameArena frameArena_;

// Add to public accessors (after line 47):
    FrameArena& frameArena() { return frameArena_; }
```

In `engine/app.cpp`, add frame arena swap at the start of the game loop (line 94, inside `while (running_)`):
```cpp
        frameArena_.swap();
```

- [ ] **Step 4: Verify compilation**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Build succeeds with 0 errors.

- [ ] **Step 5: Commit**

```bash
git add engine/memory/arena.h engine/memory/pool.h engine/app.h engine/app.cpp
git commit -m "feat(memory): add arena and pool allocators with frame arena integration"
```

---

### Task 2: Generational Entity Handles

**Files:**
- Create: `engine/ecs/entity_handle.h`
- Modify: `engine/core/types.h:6` (EntityId typedef)
- Modify: `engine/ecs/entity.h` (entity uses slot index)
- Modify: `engine/ecs/entity.cpp`
- Modify: `engine/ecs/world.h` (slot-array storage, O(1) lookup)
- Modify: `engine/ecs/world.cpp` (rewrite entity lifecycle)
- Modify: `engine/editor/editor.h:83,116` (selectedEntities_ set)

**Strategy:** EntityHandle is a new type that packs (index, generation) into 32 bits. World switches from `vector<unique_ptr<Entity>>` to a flat slot array with free list. The public API stays compatible — `createEntity()` returns `Entity*`, `getEntity(EntityHandle)` is O(1). Old code using `EntityId` continues to work because `EntityHandle` is implicitly constructible from `uint32_t` during the transition.

- [ ] **Step 1: Create `engine/ecs/entity_handle.h`**

```cpp
#pragma once
#include <cstdint>

namespace fate {

// ==========================================================================
// EntityHandle — packed generational handle [index:20 | generation:12]
//
// Provides O(1) entity lookup and stale-reference detection.
// Max 1,048,575 concurrent entities, 4,096 generations before wrap.
// ==========================================================================
struct EntityHandle {
    uint32_t value = 0;

    static constexpr uint32_t INDEX_BITS = 20;
    static constexpr uint32_t GEN_BITS = 12;
    static constexpr uint32_t INDEX_MASK = (1u << INDEX_BITS) - 1;  // 0xFFFFF
    static constexpr uint32_t GEN_MASK = (1u << GEN_BITS) - 1;     // 0xFFF
    static constexpr uint32_t MAX_INDEX = INDEX_MASK;               // 1,048,575
    static constexpr uint32_t MAX_GEN = GEN_MASK;                   // 4,095

    constexpr EntityHandle() = default;
    constexpr explicit EntityHandle(uint32_t raw) : value(raw) {}
    constexpr EntityHandle(uint32_t index, uint32_t generation)
        : value((index & INDEX_MASK) | ((generation & GEN_MASK) << INDEX_BITS)) {}

    constexpr uint32_t index() const { return value & INDEX_MASK; }
    constexpr uint32_t generation() const { return (value >> INDEX_BITS) & GEN_MASK; }

    constexpr bool isNull() const { return value == 0; }
    constexpr explicit operator bool() const { return value != 0; }

    constexpr bool operator==(const EntityHandle& other) const { return value == other.value; }
    constexpr bool operator!=(const EntityHandle& other) const { return value != other.value; }
    constexpr bool operator<(const EntityHandle& other) const { return value < other.value; }

    // Backward compat: allow use as hash map key
    struct Hash {
        size_t operator()(const EntityHandle& h) const { return std::hash<uint32_t>{}(h.value); }
    };
};

static constexpr EntityHandle NULL_ENTITY_HANDLE{};

} // namespace fate
```

- [ ] **Step 2: Update `engine/core/types.h` — keep EntityId as alias**

Add at the top of types.h (after the existing `EntityId` typedef):
```cpp
// After: using EntityId = uint32_t;
// Add:
#include "engine/ecs/entity_handle.h"
```

Keep `EntityId = uint32_t` as-is for now. Game code transitions to `EntityHandle` gradually. `INVALID_ENTITY` (0) maps to `NULL_ENTITY_HANDLE` (also 0).

- [ ] **Step 3: Add slot array to World**

Replace World's entity storage. In `engine/ecs/world.h`:

Replace private members:
```cpp
private:
    // Slot array: entities stored by index for O(1) lookup
    struct EntitySlot {
        std::unique_ptr<Entity> entity;
        uint32_t generation = 1; // starts at 1 so handle (0,0) is always invalid
        bool alive = false;
    };

    std::vector<EntitySlot> slots_;
    std::vector<uint32_t> freeSlots_;  // recycled slot indices
    std::vector<std::unique_ptr<System>> systems_;
    std::vector<EntityHandle> destroyQueue_;
    uint32_t nextSlotIndex_ = 1; // slot 0 is reserved (null handle)
```

Add new public methods alongside existing ones:
```cpp
    // New handle-based API
    EntityHandle createEntityH(const std::string& name = "Entity");
    void destroyEntity(EntityHandle handle);
    Entity* getEntity(EntityHandle handle) const;
    bool isAlive(EntityHandle handle) const;

    // Legacy API (kept for backward compat, delegates to handle API)
    Entity* createEntity(const std::string& name = "Entity");
    void destroyEntity(EntityId id); // linear search fallback
    Entity* getEntity(EntityId id) const; // linear search fallback
```

- [ ] **Step 4: Implement slot-array World in `engine/ecs/world.cpp`**

Full rewrite of entity lifecycle methods:
```cpp
World::World() {
    // Reserve slot 0 as null sentinel
    slots_.resize(1);
    slots_[0].generation = 0;
    slots_[0].alive = false;
    slots_.reserve(1024);
}

EntityHandle World::createEntityH(const std::string& name) {
    uint32_t slotIdx;
    if (!freeSlots_.empty()) {
        slotIdx = freeSlots_.back();
        freeSlots_.pop_back();
    } else {
        slotIdx = nextSlotIndex_++;
        if (slotIdx >= slots_.size()) {
            slots_.resize(slotIdx + 1);
        }
    }

    auto& slot = slots_[slotIdx];
    slot.entity = std::make_unique<Entity>(slotIdx, name); // id = slot index
    slot.alive = true;
    // generation was already bumped on destroy, or starts at 1

    EntityHandle handle(slotIdx, slot.generation);
    slot.entity->setHandle(handle);

    if (name != "Tile") {
        LOG_DEBUG("World", "Created entity '%s' (handle=%u, idx=%u, gen=%u)",
                  name.c_str(), handle.value, handle.index(), handle.generation());
    }
    return handle;
}

Entity* World::createEntity(const std::string& name) {
    EntityHandle h = createEntityH(name);
    return getEntity(h);
}

void World::destroyEntity(EntityHandle handle) {
    destroyQueue_.push_back(handle);
}

void World::destroyEntity(EntityId id) {
    // Legacy: linear scan to find handle
    for (uint32_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->id() == id) {
            destroyQueue_.push_back(EntityHandle(i, slots_[i].generation));
            return;
        }
    }
}

Entity* World::getEntity(EntityHandle handle) const {
    if (handle.isNull()) return nullptr;
    uint32_t idx = handle.index();
    if (idx >= slots_.size()) return nullptr;
    auto& slot = slots_[idx];
    if (!slot.alive || slot.generation != handle.generation()) return nullptr;
    return slot.entity.get();
}

Entity* World::getEntity(EntityId id) const {
    // Legacy O(n) fallback — searches by entity id
    for (uint32_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->id() == id) {
            return slots_[i].entity.get();
        }
    }
    return nullptr;
}

bool World::isAlive(EntityHandle handle) const {
    if (handle.isNull()) return false;
    uint32_t idx = handle.index();
    if (idx >= slots_.size()) return false;
    return slots_[idx].alive && slots_[idx].generation == handle.generation();
}

void World::processDestroyQueue() {
    for (auto handle : destroyQueue_) {
        uint32_t idx = handle.index();
        if (idx >= slots_.size()) continue;
        auto& slot = slots_[idx];
        if (!slot.alive || slot.generation != handle.generation()) continue;

        if (slot.entity) {
            LOG_DEBUG("World", "Destroyed entity '%s' (idx=%u, gen=%u)",
                      slot.entity->name().c_str(), idx, slot.generation);
        }
        slot.entity.reset();
        slot.alive = false;
        slot.generation = (slot.generation + 1) & EntityHandle::GEN_MASK;
        if (slot.generation == 0) slot.generation = 1; // skip 0 to keep null handle invalid
        freeSlots_.push_back(idx);
    }
    destroyQueue_.clear();
}

size_t World::entityCount() const {
    size_t count = 0;
    for (uint32_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].alive) ++count;
    }
    return count;
}
```

Update `forEach` to iterate slots:
```cpp
template<typename T>
void forEach(const std::function<void(Entity*, T*)>& fn) {
    for (uint32_t i = 1; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (!slot.alive || !slot.entity || !slot.entity->isActive()) continue;
        T* comp = slot.entity->getComponent<T>();
        if (comp && comp->enabled) {
            fn(slot.entity.get(), comp);
        }
    }
}
```

- [ ] **Step 5: Add handle storage to Entity**

In `engine/ecs/entity.h`, add handle member:
```cpp
// Add include:
#include "engine/ecs/entity_handle.h"

// Add to Entity class:
    EntityHandle handle() const { return handle_; }
    void setHandle(EntityHandle h) { handle_ = h; }

// Add to private:
    EntityHandle handle_;
```

- [ ] **Step 6: Update `forEachEntity` and `findByName`/`findByTag` in world.h/cpp**

These iterate all entities for editor/debug use:
```cpp
void World::forEachEntity(const std::function<void(Entity*)>& fn) {
    for (uint32_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].alive && slots_[i].entity) {
            fn(slots_[i].entity.get());
        }
    }
}

Entity* World::findByName(const std::string& name) const {
    for (uint32_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->name() == name)
            return slots_[i].entity.get();
    }
    return nullptr;
}

Entity* World::findByTag(const std::string& tag) const {
    for (uint32_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].alive && slots_[i].entity && slots_[i].entity->tag() == tag)
            return slots_[i].entity.get();
    }
    return nullptr;
}
```

- [ ] **Step 7: Update Editor to use EntityHandle**

In `engine/editor/editor.h`:
```cpp
// Change: std::set<EntityId> selectedEntities_;
// To:     std::set<EntityHandle, std::less<>> selectedEntities_;

// Change: bool isSelected(EntityId id) const
// To:     bool isSelected(EntityHandle h) const { return selectedEntities_.count(h) > 0; }
```

The editor stores `Entity*` for `selectedEntity_` which is still fine — it's validated each frame against the world.

- [ ] **Step 8: Verify compilation**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Build succeeds. Run game — entities create/destroy normally, editor selects entities.

- [ ] **Step 9: Commit**

```bash
git add engine/ecs/entity_handle.h engine/ecs/entity.h engine/ecs/entity.cpp engine/ecs/world.h engine/ecs/world.cpp engine/core/types.h engine/editor/editor.h
git commit -m "feat(ecs): add generational entity handles with O(1) slot-array lookup"
```

---

## Chunk 2: Spatial Systems (Tasks 3–5)

### Task 3: Engine-Level Spatial Hash (Müller Dense Hash)

**Files:**
- Create: `engine/spatial/spatial_hash.h`
- Modify: `game/shared/spatial_hash.h` (repoint to engine version)

- [ ] **Step 1: Create `engine/spatial/spatial_hash.h`**

Müller dense hash — two flat arrays rebuilt each frame via counting sort:

```cpp
#pragma once
#include "engine/core/types.h"
#include <vector>
#include <cmath>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <functional>

namespace fate {

// ==========================================================================
// SpatialHashEngine — Müller-style dense spatial hash
//
// Two flat arrays (cellCount + objectMap) rebuilt each frame via prefix sums.
// O(n) rebuild with pure sequential writes. O(1) cell lookup.
// Cache-friendly: no hash map, no pointer chasing, no per-cell vectors.
//
// Usage:
//   hash.beginRebuild(entityCount);
//   for each entity: hash.addEntity(id, position);
//   hash.endRebuild();     // builds prefix sums
//   hash.queryRadius(...); // O(cells_checked * entities_per_cell)
// ==========================================================================
class SpatialHashEngine {
public:
    explicit SpatialHashEngine(float cellSize = 128.0f, uint32_t tableSize = 4096)
        : cellSize_(cellSize), tableSize_(tableSize) {
        cellCount_.resize(tableSize_ + 1, 0);
        // tableSize should be power of 2 for fast modulo
    }

    void setCellSize(float size) { cellSize_ = size; }
    float cellSize() const { return cellSize_; }

    // --- Rebuild API (call each frame) ---

    void beginRebuild(size_t maxEntities) {
        entries_.clear();
        entries_.reserve(maxEntities);
        std::fill(cellCount_.begin(), cellCount_.end(), 0);
    }

    void addEntity(EntityId id, Vec2 position) {
        uint32_t cell = hashPosition(position.x, position.y);
        entries_.push_back({id, position, cell});
        cellCount_[cell]++;
    }

    void endRebuild() {
        // Prefix sum: cellCount_[i] becomes the start index for cell i
        uint32_t sum = 0;
        for (uint32_t i = 0; i < tableSize_; ++i) {
            uint32_t count = cellCount_[i];
            cellCount_[i] = sum;
            sum += count;
        }
        cellCount_[tableSize_] = sum; // sentinel

        // Scatter entries into sorted order
        sorted_.resize(entries_.size());
        for (auto& e : entries_) {
            uint32_t idx = cellCount_[e.cell]++;
            if (idx < sorted_.size()) {
                sorted_[idx] = e;
            }
        }

        // Restore cellCount to start indices (undo the increment from scatter)
        uint32_t prev = 0;
        for (uint32_t i = 0; i < tableSize_; ++i) {
            uint32_t tmp = cellCount_[i];
            cellCount_[i] = prev;
            prev = tmp;
        }
        cellCount_[tableSize_] = prev;
    }

    // --- Query API ---

    EntityId findNearest(Vec2 center, float radius) const {
        return findNearest(center, radius, [](EntityId) { return true; });
    }

    template<typename FilterFn>
    EntityId findNearest(Vec2 center, float radius, FilterFn&& filter) const {
        EntityId best = INVALID_ENTITY;
        float bestDistSq = radius * radius;

        int minCX, minCY, maxCX, maxCY;
        cellRange(center, radius, minCX, minCY, maxCX, maxCY);

        for (int cy = minCY; cy <= maxCY; ++cy) {
            for (int cx = minCX; cx <= maxCX; ++cx) {
                uint32_t cell = hashCell(cx, cy);
                uint32_t start = cellCount_[cell];
                uint32_t end = cellCount_[cell + 1];

                for (uint32_t i = start; i < end; ++i) {
                    auto& e = sorted_[i];
                    float dx = e.position.x - center.x;
                    float dy = e.position.y - center.y;
                    float dSq = dx * dx + dy * dy;
                    if (dSq < bestDistSq && filter(e.id)) {
                        bestDistSq = dSq;
                        best = e.id;
                    }
                }
            }
        }
        return best;
    }

    template<typename FilterFn>
    void queryRadius(Vec2 center, float radius,
                     std::vector<EntityId>& results, FilterFn&& filter) const {
        float radiusSq = radius * radius;

        int minCX, minCY, maxCX, maxCY;
        cellRange(center, radius, minCX, minCY, maxCX, maxCY);

        for (int cy = minCY; cy <= maxCY; ++cy) {
            for (int cx = minCX; cx <= maxCX; ++cx) {
                uint32_t cell = hashCell(cx, cy);
                uint32_t start = cellCount_[cell];
                uint32_t end = cellCount_[cell + 1];

                for (uint32_t i = start; i < end; ++i) {
                    auto& e = sorted_[i];
                    float dx = e.position.x - center.x;
                    float dy = e.position.y - center.y;
                    float dSq = dx * dx + dy * dy;
                    if (dSq <= radiusSq && filter(e.id)) {
                        results.push_back(e.id);
                    }
                }
            }
        }
    }

    void queryRadius(Vec2 center, float radius, std::vector<EntityId>& results) const {
        queryRadius(center, radius, results, [](EntityId) { return true; });
    }

    template<typename BoundsCheckFn>
    EntityId findAtPoint(Vec2 point, BoundsCheckFn&& boundsCheck) const {
        EntityId best = INVALID_ENTITY;
        float bestDistSq = std::numeric_limits<float>::max();

        int cx = toCell(point.x);
        int cy = toCell(point.y);

        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                uint32_t cell = hashCell(cx + dx, cy + dy);
                uint32_t start = cellCount_[cell];
                uint32_t end = cellCount_[cell + 1];

                for (uint32_t i = start; i < end; ++i) {
                    auto& e = sorted_[i];
                    if (boundsCheck(e.id, point)) {
                        float ddx = e.position.x - point.x;
                        float ddy = e.position.y - point.y;
                        float dSq = ddx * ddx + ddy * ddy;
                        if (dSq < bestDistSq) {
                            bestDistSq = dSq;
                            best = e.id;
                        }
                    }
                }
            }
        }
        return best;
    }

    // --- Stats ---
    size_t entityCount() const { return entries_.size(); }
    uint32_t tableSize() const { return tableSize_; }

private:
    struct Entry {
        EntityId id;
        Vec2 position;
        uint32_t cell;
    };

    float cellSize_;
    uint32_t tableSize_;
    std::vector<uint32_t> cellCount_;  // size = tableSize + 1
    std::vector<Entry> entries_;       // insertion order
    std::vector<Entry> sorted_;        // sorted by cell (after endRebuild)

    int toCell(float v) const {
        return static_cast<int>(std::floor(v / cellSize_));
    }

    // Müller hash: excellent distribution for 2D coordinates
    uint32_t hashCell(int cx, int cy) const {
        uint32_t h = static_cast<uint32_t>(
            std::abs(cx * 92837111 ^ cy * 689287499));
        return h % tableSize_;
    }

    uint32_t hashPosition(float x, float y) const {
        return hashCell(toCell(x), toCell(y));
    }

    void cellRange(Vec2 center, float radius,
                   int& minCX, int& minCY, int& maxCX, int& maxCY) const {
        minCX = toCell(center.x - radius);
        minCY = toCell(center.y - radius);
        maxCX = toCell(center.x + radius);
        maxCY = toCell(center.y + radius);
    }
};

} // namespace fate
```

- [ ] **Step 2: Update `game/shared/spatial_hash.h` to delegate to engine version**

Replace the entire file with a thin compatibility layer:
```cpp
#pragma once
// Legacy compatibility — game code can continue using SpatialHash name.
// New code should use SpatialHashEngine from engine/spatial/spatial_hash.h directly.
#include "engine/spatial/spatial_hash.h"

namespace fate {

class SpatialHash {
public:
    explicit SpatialHash(float cellSize = 128.0f) : engine_(cellSize) {}

    void setCellSize(float size) { engine_.setCellSize(size); }
    float cellSize() const { return engine_.cellSize(); }

    void clear() { entityCount_ = 0; }

    void insert(EntityId id, Vec2 position) {
        if (entityCount_ == 0) engine_.beginRebuild(256);
        engine_.addEntity(id, position);
        entityCount_++;
    }

    // Call after all inserts (before queries). Game systems must call this.
    void finalize() { if (entityCount_ > 0) engine_.endRebuild(); }

    template<typename FilterFn>
    EntityId findNearest(Vec2 center, float radius, FilterFn&& filter) const {
        return engine_.findNearest(center, radius, std::forward<FilterFn>(filter));
    }

    EntityId findNearest(Vec2 center, float radius) const {
        return engine_.findNearest(center, radius);
    }

    template<typename FilterFn>
    void queryRadius(Vec2 center, float radius,
                     std::vector<EntityId>& results, FilterFn&& filter) const {
        engine_.queryRadius(center, radius, results, std::forward<FilterFn>(filter));
    }

    template<typename BoundsCheckFn>
    EntityId findAtPoint(Vec2 point, BoundsCheckFn&& boundsCheck) const {
        return engine_.findAtPoint(point, std::forward<BoundsCheckFn>(boundsCheck));
    }

    size_t entityCount() const { return entityCount_; }

private:
    SpatialHashEngine engine_;
    size_t entityCount_ = 0;
};

} // namespace fate
```

- [ ] **Step 3: Update MobAISystem to call `finalize()` after inserts**

In `game/systems/mob_ai_system.h`, after the `playerGrid_.insert()` loop, before any queries:
```cpp
        playerGrid_.finalize();  // Build prefix sums for O(1) cell lookup
```

- [ ] **Step 4: Verify compilation**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add engine/spatial/spatial_hash.h
git commit -m "feat(spatial): add Müller dense spatial hash with prefix-sum rebuild"
```
(game/shared/spatial_hash.h is gitignored — changes are local only)

---

### Task 4: Tilemap Chunk System

**Files:**
- Create: `engine/tilemap/chunk.h`
- Modify: `engine/tilemap/tilemap.h`
- Modify: `engine/tilemap/tilemap.cpp`

- [ ] **Step 1: Create `engine/tilemap/chunk.h`**

```cpp
#pragma once
#include "engine/core/types.h"
#include "engine/render/sprite_batch.h"
#include "engine/render/camera.h"
#include <vector>
#include <cstdint>
#include <cmath>

namespace fate {

// Forward declaration
struct Tileset;
struct TilemapLayer;

static constexpr int CHUNK_SIZE = 32; // tiles per chunk side

enum class ChunkState : uint8_t {
    Active,    // Fully rendered and simulated
    Sleeping,  // Data retained in memory, not rendered
    Evicted    // No data in memory
};

// A single chunk — 32x32 tiles of one layer
struct ChunkData {
    int chunkX = 0;      // chunk coordinate (not tile, not pixel)
    int chunkY = 0;
    int layerIndex = 0;
    std::vector<int> tiles; // CHUNK_SIZE * CHUNK_SIZE GIDs (0 = empty)
    ChunkState state = ChunkState::Evicted;
    bool dirty = false;   // modified since last save

    int getTile(int localX, int localY) const {
        if (localX < 0 || localX >= CHUNK_SIZE || localY < 0 || localY >= CHUNK_SIZE)
            return 0;
        return tiles[localY * CHUNK_SIZE + localX];
    }
};

// Manages chunks for a single tilemap layer
struct ChunkLayer {
    std::string name;
    bool visible = true;
    float opacity = 1.0f;
    bool isCollisionLayer = false;
    int widthInChunks = 0;
    int heightInChunks = 0;
    std::vector<ChunkData> chunks; // flat array: [cy * widthInChunks + cx]

    ChunkData* getChunk(int cx, int cy) {
        if (cx < 0 || cx >= widthInChunks || cy < 0 || cy >= heightInChunks)
            return nullptr;
        return &chunks[cy * widthInChunks + cx];
    }

    const ChunkData* getChunk(int cx, int cy) const {
        if (cx < 0 || cx >= widthInChunks || cy < 0 || cy >= heightInChunks)
            return nullptr;
        return &chunks[cy * widthInChunks + cx];
    }
};

// Manages chunk lifecycle based on camera proximity
class ChunkManager {
public:
    // Initialize from raw layer data (called during tilemap load)
    void buildFromLayers(const std::vector<TilemapLayer>& layers,
                         int mapWidth, int mapHeight) {
        int chunksW = (mapWidth + CHUNK_SIZE - 1) / CHUNK_SIZE;
        int chunksH = (mapHeight + CHUNK_SIZE - 1) / CHUNK_SIZE;

        chunkLayers_.clear();
        chunkLayers_.reserve(layers.size());

        for (int li = 0; li < static_cast<int>(layers.size()); ++li) {
            auto& srcLayer = layers[li];
            ChunkLayer cl;
            cl.name = srcLayer.name;
            cl.visible = srcLayer.visible;
            cl.opacity = srcLayer.opacity;
            cl.isCollisionLayer = srcLayer.isCollisionLayer;
            cl.widthInChunks = chunksW;
            cl.heightInChunks = chunksH;
            cl.chunks.resize(chunksW * chunksH);

            // Split tile data into chunks
            for (int cy = 0; cy < chunksH; ++cy) {
                for (int cx = 0; cx < chunksW; ++cx) {
                    auto& chunk = cl.chunks[cy * chunksW + cx];
                    chunk.chunkX = cx;
                    chunk.chunkY = cy;
                    chunk.layerIndex = li;
                    chunk.tiles.resize(CHUNK_SIZE * CHUNK_SIZE, 0);
                    chunk.state = ChunkState::Active; // all active on load

                    // Copy tile data from source layer
                    for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                        int srcRow = cy * CHUNK_SIZE + ly;
                        if (srcRow >= srcLayer.height) break;
                        for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                            int srcCol = cx * CHUNK_SIZE + lx;
                            if (srcCol >= srcLayer.width) break;
                            int srcIdx = srcRow * srcLayer.width + srcCol;
                            if (srcIdx < static_cast<int>(srcLayer.data.size())) {
                                chunk.tiles[ly * CHUNK_SIZE + lx] = srcLayer.data[srcIdx];
                            }
                        }
                    }
                }
            }

            chunkLayers_.push_back(std::move(cl));
        }
    }

    // Update chunk states based on camera position.
    // activeRadius: chunks within this many chunks of camera center are Active.
    // sleepRadius: chunks within this are Sleeping. Beyond = Evicted.
    void updateChunkStates(const Rect& visibleBounds, Vec2 origin,
                           int tileWidth, int tileHeight,
                           int activeBuffer = 2, int sleepBuffer = 4) {
        // Convert visible bounds to chunk coordinates
        int camMinCX = static_cast<int>(std::floor((visibleBounds.x - origin.x) / (tileWidth * CHUNK_SIZE))) - activeBuffer;
        int camMinCY = static_cast<int>(std::floor((visibleBounds.y - origin.y) / (tileHeight * CHUNK_SIZE))) - activeBuffer;
        int camMaxCX = static_cast<int>(std::ceil((visibleBounds.x + visibleBounds.w - origin.x) / (tileWidth * CHUNK_SIZE))) + activeBuffer;
        int camMaxCY = static_cast<int>(std::ceil((visibleBounds.y + visibleBounds.h - origin.y) / (tileHeight * CHUNK_SIZE))) + activeBuffer;

        int sleepMinCX = camMinCX - sleepBuffer;
        int sleepMinCY = camMinCY - sleepBuffer;
        int sleepMaxCX = camMaxCX + sleepBuffer;
        int sleepMaxCY = camMaxCY + sleepBuffer;

        for (auto& cl : chunkLayers_) {
            for (int cy = 0; cy < cl.heightInChunks; ++cy) {
                for (int cx = 0; cx < cl.widthInChunks; ++cx) {
                    auto& chunk = cl.chunks[cy * cl.widthInChunks + cx];
                    if (cx >= camMinCX && cx <= camMaxCX &&
                        cy >= camMinCY && cy <= camMaxCY) {
                        chunk.state = ChunkState::Active;
                    } else if (cx >= sleepMinCX && cx <= sleepMaxCX &&
                               cy >= sleepMinCY && cy <= sleepMaxCY) {
                        chunk.state = ChunkState::Sleeping;
                    } else {
                        chunk.state = ChunkState::Evicted;
                    }
                }
            }
        }
    }

    // Check collision against active chunk data
    bool checkCollision(const Rect& worldRect, Vec2 origin,
                        int tileWidth, int tileHeight) const {
        for (auto& cl : chunkLayers_) {
            if (!cl.isCollisionLayer) continue;

            // Convert to tile range
            int startCol = static_cast<int>((worldRect.x - origin.x) / tileWidth);
            int startRow = static_cast<int>((worldRect.y - origin.y) / tileHeight);
            int endCol = static_cast<int>((worldRect.x + worldRect.w - origin.x) / tileWidth);
            int endRow = static_cast<int>((worldRect.y + worldRect.h - origin.y) / tileHeight);

            if (startCol < 0) startCol = 0;
            if (startRow < 0) startRow = 0;

            for (int row = startRow; row <= endRow; ++row) {
                for (int col = startCol; col <= endCol; ++col) {
                    int cx = col / CHUNK_SIZE;
                    int cy = row / CHUNK_SIZE;
                    auto* chunk = cl.getChunk(cx, cy);
                    if (!chunk || chunk->state == ChunkState::Evicted) continue;

                    int localX = col - cx * CHUNK_SIZE;
                    int localY = row - cy * CHUNK_SIZE;
                    if (chunk->getTile(localX, localY) > 0) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Get tile GID at world position from named layer
    int getTileAt(const std::string& layerName, float worldX, float worldY,
                  Vec2 origin, int tileWidth, int tileHeight) const {
        for (auto& cl : chunkLayers_) {
            if (cl.name != layerName) continue;
            int col = static_cast<int>((worldX - origin.x) / tileWidth);
            int row = static_cast<int>((worldY - origin.y) / tileHeight);
            int cx = col / CHUNK_SIZE;
            int cy = row / CHUNK_SIZE;
            auto* chunk = cl.getChunk(cx, cy);
            if (!chunk || chunk->state == ChunkState::Evicted) return 0;
            return chunk->getTile(col - cx * CHUNK_SIZE, row - cy * CHUNK_SIZE);
        }
        return 0;
    }

    const std::vector<ChunkLayer>& layers() const { return chunkLayers_; }
    std::vector<ChunkLayer>& layers() { return chunkLayers_; }

private:
    std::vector<ChunkLayer> chunkLayers_;
};

} // namespace fate
```

- [ ] **Step 2: Update `engine/tilemap/tilemap.h` — add ChunkManager**

Add include and member:
```cpp
#include "engine/tilemap/chunk.h"

// Add to Tilemap class private members:
    ChunkManager chunkManager_;

// Add new public method:
    ChunkManager& chunkManager() { return chunkManager_; }
    const ChunkManager& chunkManager() const { return chunkManager_; }
```

- [ ] **Step 3: Update `engine/tilemap/tilemap.cpp` — build chunks on load, render via chunks**

After loading layers in `loadFromFile()`, add at the end (before the LOG_INFO):
```cpp
        // Build chunk data from loaded layers
        chunkManager_.buildFromLayers(layers_, mapWidth_, mapHeight_);
```

Replace `render()` to use chunk-based rendering:
```cpp
void Tilemap::render(SpriteBatch& batch, Camera& camera, float depth) {
    Rect visible = camera.getVisibleBounds();

    // Update chunk lifecycle
    chunkManager_.updateChunkStates(visible, origin, tileWidth_, tileHeight_);

    float layerDepth = depth;
    for (auto& cl : chunkManager_.layers()) {
        if (!cl.visible || cl.isCollisionLayer) continue;

        for (int cy = 0; cy < cl.heightInChunks; ++cy) {
            for (int cx = 0; cx < cl.widthInChunks; ++cx) {
                auto& chunk = cl.chunks[cy * cl.widthInChunks + cx];
                if (chunk.state != ChunkState::Active) continue;

                // Chunk world bounds
                float chunkWorldX = origin.x + cx * CHUNK_SIZE * tileWidth_;
                float chunkWorldY = origin.y + cy * CHUNK_SIZE * tileHeight_;
                float chunkWorldW = CHUNK_SIZE * (float)tileWidth_;
                float chunkWorldH = CHUNK_SIZE * (float)tileHeight_;

                // Frustum cull entire chunk
                if (chunkWorldX + chunkWorldW < visible.x ||
                    chunkWorldX > visible.x + visible.w ||
                    chunkWorldY + chunkWorldH < visible.y ||
                    chunkWorldY > visible.y + visible.h) {
                    continue;
                }

                // Render tiles in this chunk
                for (int ly = 0; ly < CHUNK_SIZE; ++ly) {
                    int worldRow = cy * CHUNK_SIZE + ly;
                    for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
                        int worldCol = cx * CHUNK_SIZE + lx;
                        int gid = chunk.tiles[ly * CHUNK_SIZE + lx];
                        if (gid <= 0) continue;

                        const Tileset* ts = findTileset(gid);
                        if (!ts || !ts->texture) continue;

                        int localId = gid - ts->firstGid;
                        Rect uv = ts->getTileUV(localId);
                        Vec2 worldPos = tileToWorld(worldCol, worldRow);

                        // Per-tile frustum cull within chunk
                        if (worldPos.x + tileWidth_ < visible.x ||
                            worldPos.x - tileWidth_ > visible.x + visible.w ||
                            worldPos.y + tileHeight_ < visible.y ||
                            worldPos.y - tileHeight_ > visible.y + visible.h) {
                            continue;
                        }

                        SpriteDrawParams params;
                        params.position = worldPos;
                        params.size = {(float)tileWidth_, (float)tileHeight_};
                        params.sourceRect = uv;
                        params.color = Color(1, 1, 1, cl.opacity);
                        params.depth = layerDepth;

                        batch.draw(ts->texture, params);
                    }
                }
            }
        }
        layerDepth += 0.1f;
    }
}
```

Replace `checkCollision()` to delegate to ChunkManager:
```cpp
bool Tilemap::checkCollision(const Rect& worldRect) const {
    return chunkManager_.checkCollision(worldRect, origin, tileWidth_, tileHeight_);
}
```

Replace `getTileAt()` to delegate:
```cpp
int Tilemap::getTileAt(const std::string& layerName, float worldX, float worldY) const {
    return chunkManager_.getTileAt(layerName, worldX, worldY, origin, tileWidth_, tileHeight_);
}
```

- [ ] **Step 4: Verify compilation and tilemap rendering**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Build succeeds. Tilemap renders identically to before.

- [ ] **Step 5: Commit**

```bash
git add engine/tilemap/chunk.h engine/tilemap/tilemap.h engine/tilemap/tilemap.cpp
git commit -m "feat(tilemap): add chunk-based tile storage with lifecycle management"
```

---

### Task 5: DEAR Distance-Proportional AI Tick Scaling

**Files:**
- Modify: `game/systems/mob_ai_system.h:24-89`

- [ ] **Step 1: Add tick accumulator to MobAIComponent**

In `game/components/game_components.h`, add to MobAIComponent:
```cpp
struct MobAIComponent : public Component {
    FATE_COMPONENT(MobAIComponent)
    MobAI ai;
    float tickAccumulator = 0.0f;  // DEAR: time since last AI tick
};
```

- [ ] **Step 2: Implement DEAR tick scaling in MobAISystem**

Replace the mob iteration loop in `MobAISystem::update()`:

```cpp
    void update(float dt) override {
        if (!world_) return;

        gameTime_ += dt;

        // Rebuild player spatial hash each frame
        playerGrid_.clear();
        world_->forEach<CharacterStatsComponent, Transform>(
            [&](Entity* entity, CharacterStatsComponent* statsComp, Transform* transform) {
                if (statsComp->stats.isAlive()) {
                    playerGrid_.insert(entity->id(), transform->position);
                }
            }
        );
        playerGrid_.finalize();

        // Find the local player position for distance calculations
        Vec2 playerPos{0, 0};
        bool hasPlayer = false;
        world_->forEach<Transform, PlayerController>(
            [&](Entity*, Transform* t, PlayerController* ctrl) {
                if (ctrl->isLocalPlayer) {
                    playerPos = t->position;
                    hasPlayer = true;
                }
            }
        );

        // Iterate every mob with DEAR tick scaling
        world_->forEach<MobAIComponent, EnemyStatsComponent>(
            [&](Entity* entity, MobAIComponent* aiComp, EnemyStatsComponent* enemyComp) {
                auto* transform = entity->getComponent<Transform>();
                if (!transform) return;

                auto& ai    = aiComp->ai;
                auto& stats = enemyComp->stats;

                if (!stats.isAlive) return;

                Vec2 currentPos = transform->position;

                // --- DEAR tick scaling ---
                // Compute distance-proportional tick interval
                float tickInterval = 0.0f; // 0 = every frame
                if (hasPlayer) {
                    float distSq = (currentPos - playerPos).lengthSq();
                    float distTiles = std::sqrt(distSq) / 32.0f; // convert pixels to tiles

                    if (distTiles > 48.0f) {
                        return; // Fully dormant — beyond activation range
                    }

                    // DEAR formula: interval scales with distance²
                    // At 3 tiles: ~0.017s (every frame at 60fps)
                    // At 10 tiles: ~0.19s  (~12 fps)
                    // At 20 tiles: ~0.78s  (~1.3 fps)
                    // At 40 tiles: ~3.1s
                    tickInterval = (distTiles * distTiles) / 512.0f;
                }

                aiComp->tickAccumulator += dt;
                if (aiComp->tickAccumulator < tickInterval) {
                    return; // Skip this frame — not time to tick yet
                }
                float aiDt = aiComp->tickAccumulator;
                aiComp->tickAccumulator = 0.0f;

                // Find nearest alive player via spatial hash
                Entity* nearestPlayer = nullptr;
                float searchRadius = std::max(ai.acquireRadius, ai.contactRadius);
                EntityId nearestId = playerGrid_.findNearest(currentPos, searchRadius);
                if (nearestId != INVALID_ENTITY) {
                    nearestPlayer = world_->getEntity(nearestId);
                }

                Vec2 targetPos;
                Vec2* targetPosPtr = nullptr;
                if (nearestPlayer) {
                    auto* playerTransform = nearestPlayer->getComponent<Transform>();
                    if (playerTransform) {
                        targetPos = playerTransform->position;
                        targetPosPtr = &targetPos;
                    }
                }

                ai.onAttackFired = [&, entity, nearestPlayer]() {
                    resolveAttack(entity, nearestPlayer, stats);
                };

                // Tick MobAI with accumulated delta time
                Vec2 velocity = ai.tick(gameTime_, aiDt, currentPos, targetPosPtr);

                transform->position += velocity * aiDt;

                auto* sprite = entity->getComponent<SpriteComponent>();
                if (sprite) {
                    Direction facing = ai.getFacingDirection();
                    sprite->flipX = (facing == Direction::Left);
                }
            }
        );
    }
```

- [ ] **Step 3: Verify compilation**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Build succeeds. Mobs near player behave normally; distant mobs tick less frequently.

- [ ] **Step 4: Commit**

(MobAISystem and game_components are gitignored — no git commit needed for game files. Only note in log.)

---

## Chunk 3: Integration (Task 6)

### Task 6: Wire Everything Together

**Files:**
- Modify: `game/game_app.cpp`
- Modify: `game/shared/spawn_zone.h` (EntityHandle in TrackedMob)
- Modify: `game/systems/spawn_system.h` (use EntityHandle)
- Modify: `game/components/game_components.h` (TargetingComponent uses EntityHandle)
- Modify: `game/systems/combat_action_system.h` (EntityHandle for target refs)

- [ ] **Step 1: Update TrackedMob to use EntityHandle**

In `game/shared/spawn_zone.h`:
```cpp
#include "engine/ecs/entity_handle.h"

struct TrackedMob {
    EntityHandle entityHandle;  // was: EntityId entityId = 0;
    // ... rest unchanged
};
```

- [ ] **Step 2: Update TargetingComponent to use EntityHandle**

In `game/components/game_components.h`:
```cpp
struct TargetingComponent : public Component {
    FATE_COMPONENT(TargetingComponent)
    EntityHandle selectedTarget;  // was: uint32_t selectedTargetId = 0;
    TargetType targetType = TargetType::None;
    float maxTargetRange = 10.0f;
    bool hasTarget() const { return !selectedTarget.isNull(); }
    void clearTarget() { selectedTarget = {}; targetType = TargetType::None; }
};
```

- [ ] **Step 3: Update SpawnSystem to use EntityHandle**

In `game/systems/spawn_system.h`, update all `world.getEntity(tm.entityId)` calls to `world.getEntity(tm.entityHandle)`, and when creating mobs:
```cpp
// After EntityFactory::createMob returns mob:
tm.entityHandle = mob->handle();
```

- [ ] **Step 4: Update CombatActionSystem entity references**

In `game/systems/combat_action_system.h`, update any `getEntity(entityId)` calls to use handles where available. Where entities come from `forEach` callbacks (already have `Entity*`), no change needed.

- [ ] **Step 5: Full build and smoke test**

Run: `cmake --build build --config Debug 2>&1 | tail -5`
Expected: Build succeeds with 0 errors.

Run the game:
- Player spawns and moves with WASD
- Mobs spawn in Whispering Woods zone
- Mobs aggro and chase when player approaches
- Distant mobs tick less frequently (visible as slightly delayed reactions)
- Tilemap renders correctly (if test_map.json exists)
- Editor (F3) shows entities, selecting/deleting works
- No crashes on entity destroy/respawn

- [ ] **Step 6: Commit engine changes**

```bash
git add engine/
git commit -m "feat: engine infrastructure upgrade — arenas, handles, spatial hash, chunks, DEAR AI"
```

(All game/ changes are gitignored and remain local.)
