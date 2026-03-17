# Phase 3: Asset Hot-Reload & Allocator Visualization

## Overview

Two independent subsystems completing Phase 3 of the engine research upgrade:

- **3a: Asset Hot-Reload** — file watching, generational asset handles, type-erased loaders, debounced reload for textures/JSON/shaders
- **3b: Allocator Visualization** — ImGui/ImPlot panels for arena watermarks, pool heat maps, and frame-arena timeline

## 3a: Asset Hot-Reload

### AssetHandle — Generational Indices

A 32-bit value encoding a slot index and generation counter. Components store handles instead of raw pointers or shared_ptr, allowing assets to be reloaded without invalidating references.

```cpp
// engine/asset/asset_handle.h
struct AssetHandle {
    uint32_t bits = 0;  // 20-bit index | 12-bit generation

    uint32_t index() const { return bits & 0xFFFFF; }
    uint32_t generation() const { return bits >> 20; }
    bool valid() const { return bits != 0; }

    static AssetHandle make(uint32_t index, uint32_t gen) {
        return { (gen << 20) | (index & 0xFFFFF) };
    }

    bool operator==(AssetHandle o) const { return bits == o.bits; }
    bool operator!=(AssetHandle o) const { return bits != o.bits; }
};

// For use as map key / set element
namespace std {
    template<> struct hash<fate::AssetHandle> {
        size_t operator()(fate::AssetHandle h) const noexcept {
            return std::hash<uint32_t>{}(h.bits);
        }
    };
}
```

- 20-bit index: ~1M asset slots
- 12-bit generation: 4096 reloads before wrap
- **Slot 0 is permanently reserved** as the null sentinel (never allocated), mirroring the ECS World pattern. This prevents generation wrap on slot 0 from producing a false-null handle.
- Serializes as the asset path string (resolved to handle on load)

### AssetSlot — Per-Asset Storage

```cpp
struct AssetSlot {
    std::string path;
    uint32_t generation = 0;
    AssetKind kind;         // see note on naming below
    void* data = nullptr;
    bool loaded = false;
};
```

### AssetRegistry — Unified Type-Erased Registry

**Naming note:** The editor already defines `enum class AssetType { Sprite, Script, Scene, Shader, Other }` in `editor.h` for asset browser categorization. To avoid collision, the asset system uses `AssetKind` for the loader type discriminator.

```cpp
// engine/asset/asset_registry.h
enum class AssetKind : uint8_t { Texture, Json, Shader };

struct AssetLoader {
    AssetKind kind;
    void* (*load)(const std::string& path);
    bool (*reload)(void* existing, const std::string& path);
    bool (*validate)(const std::string& path);
    void (*destroy)(void* data);
    std::vector<std::string> extensions;
};

class AssetRegistry {
public:
    static AssetRegistry& instance();

    void registerLoader(AssetLoader loader);

    AssetHandle load(const std::string& path);

    template<typename T>
    T* get(AssetHandle handle);

    void queueReload(const std::string& path);
    void processReloads();

    AssetHandle find(const std::string& path) const;
    void clear();

private:
    std::vector<AssetSlot> slots_;       // slot 0 reserved as null
    std::vector<uint32_t> freeList_;
    std::unordered_map<std::string, uint32_t> pathToIndex_;
    std::vector<AssetLoader> loaders_;
    std::mutex reloadQueueMutex_;

    // Debounce state
    struct PendingReload {
        std::string path;
        float lastEventTime;
    };
    std::vector<PendingReload> pendingReloads_;
    static constexpr float kDebounceDelay = 0.3f; // 300ms
};
```

Constructor initializes `slots_` with one dummy entry at index 0 so real allocations start at index 1.

**Reload flow:**

1. FileWatcher detects change -> `queueReload(path)` (thread-safe via mutex)
2. Main thread calls `processReloads()` each frame — **unconditionally before the editor pause guard** in `App::update()`, alongside the existing `processDestroyQueue()`. This ensures hot-reload works while editing, which is the primary use case.
3. For each pending path where `now - lastEventTime > kDebounceDelay` (300ms):
   - Find slot by path -> find loader by asset kind
   - Call `validate(path)` -> if invalid, log warning, skip
   - Call `reload(existing, path)` -> if success, bump `slot.generation`
4. Systems holding stale-generation handles get nullptr from `get<T>()`

**Extension-to-loader mapping:** On `load(path)`, the registry matches the file extension against registered loaders.

### FileWatcher — Windows ReadDirectoryChangesW

```cpp
// engine/asset/file_watcher.h
class FileWatcher {
public:
    using Callback = std::function<void(const std::string& path)>;

    void start(const std::string& directory, Callback onFileChanged);
    void stop();
    ~FileWatcher();

private:
    std::jthread watchThread_;
    std::atomic<bool> running_{false};
    HANDLE dirHandle_ = INVALID_HANDLE_VALUE;
};
```

Implementation:

- `CreateFileW` with `FILE_LIST_DIRECTORY` + `FILE_FLAG_OVERLAPPED` on the assets directory
- `ReadDirectoryChangesW` with `FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME`, recursive
- Runs in a `std::jthread`, blocks on `ReadDirectoryChangesW`
- On change: normalize path (forward slashes, relative to assets root), call callback
- Stop via `CancelIoEx` + join

The watcher is minimal — reports "this path changed." Debouncing, validation, and reloading are handled by `AssetRegistry` on the main thread.

### Asset Loaders

Three concrete loaders registered at engine init.

**Texture Loader:**

- `load`: `stb_image` -> `glGenTextures` -> `glTexImage2D` -> return `new Texture`
- `reload`: `stb_image` into temp -> call `glTexImage2D` on the **existing GL texture name** (OpenGL allows respecifying storage on an existing texture object). This preserves the GL id regardless of dimension changes, avoiding stale texture IDs in SpriteBatch sort keys or cached bindings.
- `validate`: `stbi_info()` — checks file is valid image
- Extensions: `.png`, `.jpg`, `.bmp`

**JSON Loader:**

- `load`: `std::ifstream` -> `nlohmann::json::parse` -> return `new nlohmann::json`
- `reload`: parse into temp -> swap with existing
- `validate`: `json::parse` in try/catch — reject malformed files
- Extensions: `.json`

**Shader Loader:**

Uses the existing `Shader` class (`engine/render/shader.h`), which already has `loadFromFile()`, `loadFromSource()`, uniform caching, and `bind()`/`unbind()`. A new `reloadFromFile()` method is added to `Shader`:

```cpp
// Added to Shader class
bool reloadFromFile(const std::string& vertPath, const std::string& fragPath);
```

This compiles and links a new program. On success, it deletes the old program, swaps in the new `programId_`, and clears the `uniformCache_` (locations may differ). On failure, it logs the compile/link error and keeps the old program running.

- `load`: `new Shader` -> `loadFromFile(vertPath, fragPath)` -> return pointer
- `reload`: call `reloadFromFile()` on the existing `Shader*` -> return success/failure
- `validate`: read file, check non-empty
- Extensions: `.vert`, `.frag`, `.glsl`

For paired shader files (.vert/.frag), a change to either file triggers reload of the pair. The loader infers the partner path by swapping the extension.

**Registration:**

```cpp
// In App::onInit()
AssetRegistry::instance().registerLoader(makeTextureLoader());
AssetRegistry::instance().registerLoader(makeJsonLoader());
AssetRegistry::instance().registerLoader(makeShaderLoader());
```

### Migration from TextureCache

**Step 1 (this implementation):** `TextureCache::load()` delegates to `AssetRegistry` internally. Returns a non-owning `shared_ptr<Texture>` for backward compatibility. Callers that can be migrated switch to `AssetHandle` directly.

**Step 2 (this implementation):** Sprite component and other texture holders switch from `shared_ptr<Texture>` to `AssetHandle`. Render systems call `registry.get<Texture>(handle)` at draw time.

**Step 3 (later cleanup):** Delete `TextureCache` entirely once all callers migrated.

Serialization: `AssetHandle` serializes as the path string in JSON. On deserialization, the path is resolved back to a handle via `AssetRegistry::load()`.

### Shutdown Ordering

Shutdown must happen in this order to avoid dangling callbacks and use-after-free:

1. **`FileWatcher::stop()`** — joins the watcher thread, ensures no further `queueReload()` calls
2. **`AssetRegistry::instance().clear()`** — calls `destroy()` on every slot (deletes textures via `glDeleteTextures`, deletes shaders via `glDeleteProgram`, deletes json objects)
3. **GL context teardown** (SDL_GL_DeleteContext) — safe because all GL resources are already freed

This sequence is enforced in `App::shutdown()`, matching the existing pattern where `TextureCache::clear()` already runs before SDL cleanup.

## 3b: Allocator Visualization

### AllocatorRegistry

All arenas and pools register with a central registry so the debug panel can discover them.

```cpp
// engine/memory/allocator_registry.h
#if defined(ENGINE_MEMORY_DEBUG)

enum class AllocatorType : uint8_t { Arena, FrameArena, Pool };

struct AllocatorInfo {
    const char* name;
    AllocatorType type;
    std::function<size_t()> getUsed;
    std::function<size_t()> getCommitted;
    std::function<size_t()> getReserved;
    // Pool-specific (empty for arenas)
    std::function<size_t()> getActiveBlocks;
    std::function<size_t()> getTotalBlocks;
    // Pool heat map: returns a pointer to the occupancy bitmap (1 bit per block, 1=occupied)
    // nullptr for arenas. The bitmap is owned by the PoolAllocator.
    std::function<const uint8_t*()> getOccupancyBitmap;
};

class AllocatorRegistry {
public:
    static AllocatorRegistry& instance();

    void add(AllocatorInfo info);
    void remove(const char* name);  // uses string comparison, not pointer identity
    const std::vector<AllocatorInfo>& all() const;

private:
    std::vector<AllocatorInfo> allocators_;
};

#endif // ENGINE_MEMORY_DEBUG
```

Uses `std::function<size_t()>` instead of raw function pointers, since registrations capture `this` in lambdas. This is debug-only code so the std::function overhead is irrelevant.

Registration is external to the allocator classes — Arena, FrameArena are unchanged. Each owner registers at construction:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
AllocatorRegistry::instance().add({
    .name = "ZoneArena",
    .type = AllocatorType::Arena,
    .getUsed = [this]() { return zoneArena_.position(); },
    .getCommitted = [this]() { return zoneArena_.committed(); },
    .getReserved = [this]() { return zoneArena_.reserved(); },
});
#endif
```

### Pool Occupancy Bitmap

`PoolAllocator` gains a debug-only occupancy bitmap for the heat map visualization:

```cpp
// Added to PoolAllocator under #if defined(ENGINE_MEMORY_DEBUG)
uint8_t* occupancyBitmap_ = nullptr;  // 1 bit per block, 1=occupied

// In init(): allocate bitmap from arena (blockCount / 8 bytes, rounded up)
// In alloc(): set bit for allocated block index
// In free(): clear bit for freed block index

const uint8_t* occupancyBitmap() const { return occupancyBitmap_; }
```

Cost: `blockCount / 8` bytes (e.g., 128 bytes for 1024 blocks). The bitmap is allocated from the same backing arena. Block index is computed as `(block - memory_) / blockSize_`. Compiles away entirely in release builds.

### Memory Debug Panel

A single ImGui window with three tabs, added to Editor as `drawMemoryPanel()`.

**Tab 1: Arena Watermarks**

- `ImGui::ProgressBar(used / reserved)` per arena with color coding:
  - Green: < 70%, Yellow: 70-90%, Red: > 90%
- Text: `"ZoneArena: 12.4 MB / 256 MB (4.8%)"`
- High-water mark line via `ImDrawList::AddLine` tracking peak `position()`
- Committed vs reserved as a second dimmer bar (physical memory touched)

**Tab 2: Pool Heat Maps**

- Grid of colored cells per pool, one per block (using the occupancy bitmap):
  - Red = occupied, Gray = free
- `ImDrawList::AddRectFilled` packed into rows
- Summary: `"ParticlePool: 847/1024 active (82.7%)"`
- Tooltip on hover shows block index
- Pools >1024 blocks use downsampled view (each cell = 8 blocks, color by occupancy ratio)

**Tab 3: Frame Arena Timeline (ImPlot)**

- Ring buffer of 300 samples, updated at **end-of-frame** (after all systems have allocated, before `swap()`) with `frameArena.current().position()`
- `ImPlot::PlotLine("Frame Arena", samples, 300)` — scrolling line chart
- Y-axis: bytes, auto-scaled
- Horizontal reference line at high-water mark
- Reveals allocation spikes and trending growth

**Editor integration:**

```cpp
// Added to editor.h
void drawMemoryPanel();
bool showMemoryPanel_ = false;  // toggled via View menu
```

## New Files

```
engine/asset/asset_handle.h          -- AssetHandle struct + std::hash specialization
engine/asset/asset_registry.h        -- AssetRegistry, AssetSlot, AssetLoader, AssetKind
engine/asset/asset_registry.cpp      -- AssetRegistry implementation
engine/asset/file_watcher.h          -- FileWatcher class
engine/asset/file_watcher.cpp        -- Windows ReadDirectoryChangesW implementation
engine/asset/loaders.h               -- makeTextureLoader/JsonLoader/ShaderLoader declarations
engine/asset/loaders.cpp             -- Loader implementations
engine/memory/allocator_registry.h   -- AllocatorRegistry (header-only, debug-guarded)
engine/editor/memory_panel.h         -- drawMemoryPanel declaration
engine/editor/memory_panel.cpp       -- ImGui/ImPlot visualization implementation
```

## Modified Files

```
engine/render/texture.h/cpp          -- Texture reload via glTexImage2D on existing GL name
engine/render/texture.h              -- TextureCache delegates to AssetRegistry
engine/render/shader.h/cpp           -- Shader::reloadFromFile() method added
engine/memory/pool.h                 -- Occupancy bitmap under ENGINE_MEMORY_DEBUG
engine/app.h/cpp                     -- processReloads() before pause guard, FileWatcher lifecycle
engine/editor/editor.h/cpp           -- Memory panel toggle in View menu, drawMemoryPanel() call
engine/ecs/world.h/cpp               -- AllocatorRegistry registration for zone arena
game/game_app.cpp                    -- Asset loader registration, allocator registration
CMakeLists.txt                       -- ImPlot sources, ENGINE_MEMORY_DEBUG option, new sources
```

## CMake Changes

- Vendor ImPlot: add `implot.h`, `implot.cpp`, `implot_items.cpp` to the existing `imgui_lib` static library target (ImPlot depends on ImGui context which is already available in that target)
- Add `engine/asset/` source files to the engine target
- `option(ENGINE_MEMORY_DEBUG "Enable allocator debug panels" ON)`
- `-DENGINE_MEMORY_DEBUG` added to compile definitions when option is ON

## Test Coverage

- `AssetHandle`: generation wrap, index packing/unpacking, stale detection, equality, hash
- `AssetRegistry`: load/get/reload cycle, stale handle returns nullptr, duplicate path dedup, extension matching, slot 0 never allocated
- `AllocatorRegistry`: add/remove (by string comparison)/query round-trip
- Debounce logic: time-based filtering of rapid events
- Pool occupancy bitmap: alloc sets bit, free clears bit, round-trip
- FileWatcher: manual verification (OS-dependent, not unit tested)

## Tracy Complement

Existing `FATE_ALLOC`/`FATE_FREE` Tracy macros unchanged. The ImGui panels show what Tracy doesn't: arena internal state, pool occupancy patterns, frame-over-frame trends. Tracy remains the tool for CPU profiling; the memory panel is for allocator-specific insight without needing to connect the external profiler.
