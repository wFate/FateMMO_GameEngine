# Phase 3: Asset Hot-Reload & Allocator Visualization — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add generational asset handles with hot-reload (textures, JSON, shaders) and ImGui/ImPlot allocator visualization panels to the engine.

**Architecture:** A unified `AssetRegistry` with type-erased loaders replaces `TextureCache`. A Windows file watcher detects changes and queues debounced reloads on the main thread. Separately, an `AllocatorRegistry` feeds arena/pool stats to a 3-tab ImGui debug panel (watermarks, heat maps, ImPlot timeline).

**Tech Stack:** C++23, OpenGL 3.3, SDL2, ImGui (docking), ImPlot, stb_image, nlohmann/json, doctest

**Build command:**
```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build --config Debug
```

**Test command:**
```bash
./out/build/Debug/fate_tests.exe
```

**CRITICAL RULES:**
- NEVER create `.bat` files — they hang the bash shell indefinitely
- NEVER add `Co-Authored-By: Claude` lines to commits
- Always register new components in `game/register_components.h`
- Slot 0 is reserved (null sentinel) in AssetRegistry, matching the ECS World pattern

---

## File Structure

### New Files
| File | Responsibility |
|------|---------------|
| `engine/asset/asset_handle.h` | `AssetHandle` struct (32-bit generational index), `std::hash` specialization |
| `engine/asset/asset_registry.h` | `AssetKind` enum, `AssetLoader` struct, `AssetSlot`, `AssetRegistry` class declaration |
| `engine/asset/asset_registry.cpp` | `AssetRegistry` implementation (load, get, reload queue, debounce, processReloads) |
| `engine/asset/file_watcher.h` | `FileWatcher` class declaration |
| `engine/asset/file_watcher.cpp` | Windows `ReadDirectoryChangesW` background thread implementation |
| `engine/asset/loaders.h` | `makeTextureLoader()`, `makeJsonLoader()`, `makeShaderLoader()` declarations |
| `engine/asset/loaders.cpp` | Concrete loader implementations (load/reload/validate/destroy for each type) |
| `engine/memory/allocator_registry.h` | `AllocatorRegistry`, `AllocatorInfo` (header-only, `ENGINE_MEMORY_DEBUG` guarded) |
| `engine/editor/memory_panel.h` | `drawMemoryPanel()` declaration |
| `engine/editor/memory_panel.cpp` | ImGui/ImPlot 3-tab panel (arena watermarks, pool heat maps, frame timeline) |
| `tests/test_asset_handle.cpp` | AssetHandle unit tests |
| `tests/test_asset_registry.cpp` | AssetRegistry unit tests |
| `tests/test_allocator_registry.cpp` | AllocatorRegistry unit tests |

### Modified Files
| File | Changes |
|------|---------|
| `engine/render/texture.h` | Add `Texture::reloadFromFile()` method |
| `engine/render/texture.cpp` | Implement `reloadFromFile()` — `glTexImage2D` on existing GL name |
| `engine/render/shader.h` | Add `Shader::reloadFromFile()` method, add `vertPath_`/`fragPath_` members |
| `engine/render/shader.cpp` | Implement `Shader::reloadFromFile()` — compile new, swap on success |
| `engine/memory/pool.h` | Add occupancy bitmap under `ENGINE_MEMORY_DEBUG` |
| `engine/app.h` | Add `FileWatcher` and `AssetRegistry` forward decls, declare lifecycle hooks |
| `engine/app.cpp` | `processReloads()` before pause guard, file watcher start/stop in init/shutdown |
| `engine/editor/editor.h` | Add `showMemoryPanel_` bool, `drawMemoryPanel()` declaration |
| `engine/editor/editor.cpp` | Memory panel toggle in View menu, call `drawMemoryPanel()` in `renderUI()` |
| `game/register_components.h` | Update SpriteComponent deserializer to use `AssetRegistry` |
| `CMakeLists.txt` | ImPlot sources in `imgui_lib`, `ENGINE_MEMORY_DEBUG` option |

---

## Task 1: AssetHandle

**Files:**
- Create: `engine/asset/asset_handle.h`
- Create: `tests/test_asset_handle.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_asset_handle.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/asset/asset_handle.h"

TEST_CASE("AssetHandle default is invalid") {
    fate::AssetHandle h;
    CHECK(h.bits == 0);
    CHECK_FALSE(h.valid());
}

TEST_CASE("AssetHandle make and unpack") {
    auto h = fate::AssetHandle::make(42, 7);
    CHECK(h.index() == 42);
    CHECK(h.generation() == 7);
    CHECK(h.valid());
}

TEST_CASE("AssetHandle max index") {
    auto h = fate::AssetHandle::make(0xFFFFF, 1);
    CHECK(h.index() == 0xFFFFF);
    CHECK(h.generation() == 1);
}

TEST_CASE("AssetHandle max generation") {
    auto h = fate::AssetHandle::make(1, 0xFFF);
    CHECK(h.index() == 1);
    CHECK(h.generation() == 0xFFF);
}

TEST_CASE("AssetHandle equality") {
    auto a = fate::AssetHandle::make(5, 3);
    auto b = fate::AssetHandle::make(5, 3);
    auto c = fate::AssetHandle::make(5, 4);
    CHECK(a == b);
    CHECK(a != c);
}

TEST_CASE("AssetHandle std::hash works") {
    auto a = fate::AssetHandle::make(5, 3);
    auto b = fate::AssetHandle::make(5, 3);
    std::hash<fate::AssetHandle> hasher;
    CHECK(hasher(a) == hasher(b));
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -20`
Expected: Compile error — `engine/asset/asset_handle.h` not found

- [ ] **Step 3: Write minimal implementation**

Create `engine/asset/asset_handle.h`:

```cpp
#pragma once
#include <cstdint>
#include <functional>

namespace fate {

struct AssetHandle {
    uint32_t bits = 0;

    uint32_t index() const { return bits & 0xFFFFF; }
    uint32_t generation() const { return bits >> 20; }
    bool valid() const { return bits != 0; }

    static AssetHandle make(uint32_t index, uint32_t gen) {
        return { (gen << 20) | (index & 0xFFFFF) };
    }

    bool operator==(AssetHandle o) const { return bits == o.bits; }
    bool operator!=(AssetHandle o) const { return bits != o.bits; }
};

} // namespace fate

template<>
struct std::hash<fate::AssetHandle> {
    size_t operator()(fate::AssetHandle h) const noexcept {
        return std::hash<uint32_t>{}(h.bits);
    }
};
```

- [ ] **Step 4: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="AssetHandle*"`
Expected: All 6 AssetHandle tests pass

- [ ] **Step 5: Commit**

```bash
git add engine/asset/asset_handle.h tests/test_asset_handle.cpp
git commit -m "feat(asset): AssetHandle with generational 20+12 bit packing"
```

---

## Task 2: AssetRegistry Core (load/get, no reload yet)

**Files:**
- Create: `engine/asset/asset_registry.h`
- Create: `engine/asset/asset_registry.cpp`
- Create: `tests/test_asset_registry.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_asset_registry.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/asset/asset_registry.h"

// Mock loader: stores an int on the heap
static void* mockLoad(const std::string& path) {
    return new int(42);
}
static bool mockReload(void* existing, const std::string& path) {
    *static_cast<int*>(existing) = 99;
    return true;
}
static bool mockValidate(const std::string& path) { return true; }
static void mockDestroy(void* data) { delete static_cast<int*>(data); }

static fate::AssetLoader makeMockLoader() {
    return {
        .kind = fate::AssetKind::Texture,
        .load = mockLoad,
        .reload = mockReload,
        .validate = mockValidate,
        .destroy = mockDestroy,
        .extensions = {".mock"}
    };
}

TEST_CASE("AssetRegistry slot 0 is reserved") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    CHECK(h.index() != 0); // slot 0 is null sentinel
}

TEST_CASE("AssetRegistry load and get") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    REQUIRE(h.valid());
    int* val = reg.get<int>(h);
    REQUIRE(val != nullptr);
    CHECK(*val == 42);
}

TEST_CASE("AssetRegistry duplicate path returns same handle") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h1 = reg.load("test.mock");
    auto h2 = reg.load("test.mock");
    CHECK(h1 == h2);
}

TEST_CASE("AssetRegistry stale handle returns nullptr") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    // Manually bump generation to simulate reload
    // We'll test proper reload flow in a separate test
    auto stale = fate::AssetHandle::make(h.index(), h.generation() + 1);
    CHECK(reg.get<int>(stale) == nullptr);
}

TEST_CASE("AssetRegistry find by path") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    auto found = reg.find("test.mock");
    CHECK(h == found);

    auto notFound = reg.find("nonexistent.mock");
    CHECK_FALSE(notFound.valid());
}

TEST_CASE("AssetRegistry clear destroys all assets") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    REQUIRE(reg.get<int>(h) != nullptr);
    reg.clear();
    // After clear, handle is stale
    CHECK(reg.get<int>(h) == nullptr);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -20`
Expected: Compile error — `engine/asset/asset_registry.h` not found

- [ ] **Step 3: Write the AssetRegistry header**

Create `engine/asset/asset_registry.h`:

```cpp
#pragma once
#include "engine/asset/asset_handle.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace fate {

enum class AssetKind : uint8_t { Texture, Json, Shader };

struct AssetLoader {
    AssetKind kind;
    void* (*load)(const std::string& path);
    bool (*reload)(void* existing, const std::string& path);
    bool (*validate)(const std::string& path);
    void (*destroy)(void* data);
    std::vector<std::string> extensions;
};

struct AssetSlot {
    std::string path;
    uint32_t generation = 0;
    AssetKind kind{};
    void* data = nullptr;
    bool loaded = false;
};

class AssetRegistry {
public:
    static AssetRegistry& instance();

    void registerLoader(AssetLoader loader);

    AssetHandle load(const std::string& path);

    template<typename T>
    T* get(AssetHandle handle) {
        if (!handle.valid()) return nullptr;
        uint32_t idx = handle.index();
        if (idx >= slots_.size()) return nullptr;
        auto& slot = slots_[idx];
        if (!slot.loaded || slot.generation != handle.generation())
            return nullptr;
        return static_cast<T*>(slot.data);
    }

    void queueReload(const std::string& path);
    void processReloads(float currentTime);

    AssetHandle find(const std::string& path) const;
    void clear();

    // Debug info
    size_t assetCount() const;

private:
    AssetRegistry();

    std::vector<AssetSlot> slots_;       // slot 0 reserved
    std::vector<uint32_t> freeList_;
    std::unordered_map<std::string, uint32_t> pathToIndex_;
    std::vector<AssetLoader> loaders_;

    std::mutex reloadQueueMutex_;
    struct PendingReload {
        std::string path;
        float lastEventTime = 0.0f;
    };
    std::vector<PendingReload> pendingReloads_;
    static constexpr float kDebounceDelay = 0.3f;

    const AssetLoader* findLoader(const std::string& path) const;
    uint32_t allocSlot();
};

} // namespace fate
```

- [ ] **Step 4: Write the AssetRegistry implementation**

Create `engine/asset/asset_registry.cpp`:

```cpp
#include "engine/asset/asset_registry.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <filesystem>

namespace fate {

AssetRegistry& AssetRegistry::instance() {
    static AssetRegistry s_instance;
    return s_instance;
}

AssetRegistry::AssetRegistry() {
    // Reserve slot 0 as null sentinel
    slots_.push_back(AssetSlot{});
}

void AssetRegistry::registerLoader(AssetLoader loader) {
    loaders_.push_back(std::move(loader));
}

// Canonicalize path for consistent lookups (forward slashes, absolute)
static std::string canonicalizePath(const std::string& path) {
    namespace fs = std::filesystem;
    auto canon = fs::weakly_canonical(fs::path(path)).string();
    std::replace(canon.begin(), canon.end(), '\\', '/');
    return canon;
}

AssetHandle AssetRegistry::load(const std::string& path) {
    std::string canon = canonicalizePath(path);

    // Check if already loaded
    auto it = pathToIndex_.find(canon);
    if (it != pathToIndex_.end()) {
        auto& slot = slots_[it->second];
        return AssetHandle::make(it->second, slot.generation);
    }

    // Find loader by extension
    const AssetLoader* loader = findLoader(canon);
    if (!loader) {
        LOG_ERROR("AssetRegistry", "No loader for: %s", canon.c_str());
        return AssetHandle{};
    }

    // Allocate slot
    uint32_t idx = allocSlot();
    auto& slot = slots_[idx];
    slot.path = canon;
    slot.kind = loader->kind;
    slot.data = loader->load(canon);
    slot.loaded = (slot.data != nullptr);

    if (!slot.loaded) {
        LOG_ERROR("AssetRegistry", "Failed to load: %s", canon.c_str());
        freeList_.push_back(idx);
        return AssetHandle{};
    }

    pathToIndex_[canon] = idx;

    // For shaders: register partner path as alias so either file change triggers reload
    if (loader->kind == AssetKind::Shader) {
        namespace fs = std::filesystem;
        auto ext = fs::path(canon).extension().string();
        std::string partner;
        auto stem = fs::path(canon).parent_path() / fs::path(canon).stem();
        if (ext == ".vert") partner = stem.string() + ".frag";
        else if (ext == ".frag") partner = stem.string() + ".vert";
        if (!partner.empty()) {
            std::replace(partner.begin(), partner.end(), '\\', '/');
            pathToIndex_[partner] = idx; // alias points to same slot
        }
    }

    return AssetHandle::make(idx, slot.generation);
}

void AssetRegistry::queueReload(const std::string& path) {
    std::string canon = canonicalizePath(path);
    std::lock_guard lock(reloadQueueMutex_);
    for (auto& pending : pendingReloads_) {
        if (pending.path == canon) {
            pending.lastEventTime = -1.0f; // mark for timestamp update
            return;
        }
    }
    pendingReloads_.push_back({canon, -1.0f});
}

void AssetRegistry::processReloads(float currentTime) {
    std::vector<PendingReload> toProcess;
    {
        std::lock_guard lock(reloadQueueMutex_);
        // Stamp any newly queued entries
        for (auto& p : pendingReloads_) {
            if (p.lastEventTime < 0.0f) p.lastEventTime = currentTime;
        }
        // Copy out entries past debounce delay, keep the rest
        std::vector<PendingReload> remaining;
        for (auto& p : pendingReloads_) {
            if ((currentTime - p.lastEventTime) >= kDebounceDelay) {
                toProcess.push_back(std::move(p));
            } else {
                remaining.push_back(std::move(p));
            }
        }
        pendingReloads_ = std::move(remaining);
    }

    // Process reloads on main thread
    for (const auto& pending : toProcess) {
        auto it = pathToIndex_.find(pending.path);
        if (it == pathToIndex_.end()) continue;

        auto& slot = slots_[it->second];
        const AssetLoader* loader = findLoader(slot.path);
        if (!loader) continue;

        if (!loader->validate(slot.path)) {
            LOG_WARN("AssetRegistry", "Validation failed, skipping reload: %s", slot.path.c_str());
            continue;
        }

        if (loader->reload(slot.data, slot.path)) {
            slot.generation++;
            LOG_INFO("AssetRegistry", "Reloaded: %s (gen %u)", slot.path.c_str(), slot.generation);
        } else {
            LOG_WARN("AssetRegistry", "Reload failed: %s", slot.path.c_str());
        }
    }
}

AssetHandle AssetRegistry::find(const std::string& path) const {
    std::string canon = canonicalizePath(path);
    auto it = pathToIndex_.find(canon);
    if (it == pathToIndex_.end()) return AssetHandle{};
    return AssetHandle::make(it->second, slots_[it->second].generation);
}

void AssetRegistry::clear() {
    for (size_t i = 1; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (slot.loaded && slot.data) {
            const AssetLoader* loader = findLoader(slot.path);
            if (loader) loader->destroy(slot.data);
            slot.data = nullptr;
            slot.loaded = false;
        }
    }
    slots_.resize(1); // keep slot 0
    slots_[0] = AssetSlot{};
    freeList_.clear();
    pathToIndex_.clear();
    loaders_.clear();
    {
        std::lock_guard lock(reloadQueueMutex_);
        pendingReloads_.clear();
    }
}

size_t AssetRegistry::assetCount() const {
    size_t count = 0;
    for (size_t i = 1; i < slots_.size(); ++i) {
        if (slots_[i].loaded) ++count;
    }
    return count;
}

const AssetLoader* AssetRegistry::findLoader(const std::string& path) const {
    namespace fs = std::filesystem;
    std::string ext = fs::path(path).extension().string();
    for (const auto& loader : loaders_) {
        for (const auto& e : loader.extensions) {
            if (e == ext) return &loader;
        }
    }
    return nullptr;
}

uint32_t AssetRegistry::allocSlot() {
    if (!freeList_.empty()) {
        uint32_t idx = freeList_.back();
        freeList_.pop_back();
        return idx;
    }
    uint32_t idx = static_cast<uint32_t>(slots_.size());
    slots_.push_back(AssetSlot{});
    return idx;
}

} // namespace fate
```

- [ ] **Step 5: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="AssetRegistry*"`
Expected: All 6 AssetRegistry tests pass

- [ ] **Step 6: Commit**

```bash
git add engine/asset/asset_registry.h engine/asset/asset_registry.cpp tests/test_asset_registry.cpp
git commit -m "feat(asset): AssetRegistry with type-erased loaders and generational slots"
```

---

## Task 3: Texture Reload Method

**Files:**
- Modify: `engine/render/texture.h:8-29` — add `reloadFromFile()` declaration
- Modify: `engine/render/texture.cpp` — implement `reloadFromFile()` using `glTexImage2D` on existing GL name

- [ ] **Step 1: Add `reloadFromFile()` to Texture**

In `engine/render/texture.h`, add after line 14 (`bool loadFromFile`):

```cpp
    bool reloadFromFile(const std::string& path);
```

In `engine/render/texture.cpp`, add after `loadFromFile()` (after line 31):

```cpp
bool Texture::reloadFromFile(const std::string& path) {
    stbi_set_flip_vertically_on_load(true);
    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) {
        LOG_ERROR("Texture", "Reload failed: %s (%s)", path.c_str(), stbi_failure_reason());
        return false;
    }

    width_ = w;
    height_ = h;
    path_ = path;

    // Reuse existing GL texture name — glTexImage2D respecifies storage
    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    LOG_INFO("Texture", "Reloaded %s (%dx%d)", path.c_str(), w, h);
    return true;
}
```

- [ ] **Step 2: Build to verify no compile errors**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add engine/render/texture.h engine/render/texture.cpp
git commit -m "feat(texture): add reloadFromFile using existing GL texture name"
```

---

## Task 4: Shader Reload Method

**Files:**
- Modify: `engine/render/shader.h:8-34` — add `reloadFromFile()`, `vertPath_`, `fragPath_`
- Modify: `engine/render/shader.cpp` — implement `reloadFromFile()` with atomic swap

- [ ] **Step 1: Add reload support to Shader**

In `engine/render/shader.h`, add after line 13 (`bool loadFromFile`):

```cpp
    bool reloadFromFile(const std::string& vertPath, const std::string& fragPath);
    const std::string& vertPath() const { return vertPath_; }
    const std::string& fragPath() const { return fragPath_; }
```

Add to private section (after line 30 `int getUniformLocation`):

```cpp
    std::string vertPath_;
    std::string fragPath_;
```

In `engine/render/shader.cpp`, modify `loadFromFile()` to store paths (after line 28, before the `return`):

```cpp
    vertPath_ = vertPath;
    fragPath_ = fragPath;
```

Add `reloadFromFile()` after `loadFromFile()`. This delegates to the existing `loadFromSource()` to avoid duplicating compile/link logic:

```cpp
bool Shader::reloadFromFile(const std::string& vertPath, const std::string& fragPath) {
    std::ifstream vertFile(vertPath);
    std::ifstream fragFile(fragPath);
    if (!vertFile.is_open() || !fragFile.is_open()) {
        LOG_ERROR("Shader", "Reload: cannot open shader files");
        return false;
    }

    std::stringstream vertStream, fragStream;
    vertStream << vertFile.rdbuf();
    fragStream << fragFile.rdbuf();

    // Save old program in case loadFromSource fails
    unsigned int oldProgram = programId_;
    programId_ = 0;

    if (!loadFromSource(vertStream.str(), fragStream.str())) {
        // Restore old program — loadFromSource already logged the error
        programId_ = oldProgram;
        LOG_WARN("Shader", "Reload failed — keeping old program %u", programId_);
        return false;
    }

    // Success: delete old program, clear uniform cache (locations may differ)
    glDeleteProgram(oldProgram);
    uniformCache_.clear();
    vertPath_ = vertPath;
    fragPath_ = fragPath;
    LOG_INFO("Shader", "Reloaded shader program %u", programId_);
    return true;
}
```

- [ ] **Step 2: Build to verify no compile errors**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 3: Commit**

```bash
git add engine/render/shader.h engine/render/shader.cpp
git commit -m "feat(shader): add reloadFromFile with atomic program swap"
```

---

## Task 5: Asset Loaders (Texture, JSON, Shader)

**Files:**
- Create: `engine/asset/loaders.h`
- Create: `engine/asset/loaders.cpp`

- [ ] **Step 1: Write the loader declarations**

Create `engine/asset/loaders.h`:

```cpp
#pragma once
#include "engine/asset/asset_registry.h"

namespace fate {

AssetLoader makeTextureLoader();
AssetLoader makeJsonLoader();
AssetLoader makeShaderLoader();

} // namespace fate
```

- [ ] **Step 2: Write the loader implementations**

Create `engine/asset/loaders.cpp`:

```cpp
#include "engine/asset/loaders.h"
#include "engine/render/texture.h"
#include "engine/render/shader.h"
#include "engine/core/logger.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include "stb_image.h"

namespace fate {

// ============================================================================
// Texture Loader
// ============================================================================

static void* textureLoad(const std::string& path) {
    auto* tex = new Texture();
    if (!tex->loadFromFile(path)) {
        delete tex;
        return nullptr;
    }
    return tex;
}

static bool textureReload(void* existing, const std::string& path) {
    return static_cast<Texture*>(existing)->reloadFromFile(path);
}

static bool textureValidate(const std::string& path) {
    int w, h, c;
    return stbi_info(path.c_str(), &w, &h, &c) != 0;
}

static void textureDestroy(void* data) {
    delete static_cast<Texture*>(data);
}

AssetLoader makeTextureLoader() {
    return {
        .kind = AssetKind::Texture,
        .load = textureLoad,
        .reload = textureReload,
        .validate = textureValidate,
        .destroy = textureDestroy,
        .extensions = {".png", ".jpg", ".bmp"}
    };
}

// ============================================================================
// JSON Loader
// ============================================================================

static void* jsonLoad(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return nullptr;
        auto* j = new nlohmann::json();
        *j = nlohmann::json::parse(file);
        return j;
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR("JsonLoader", "Parse failed: %s — %s", path.c_str(), e.what());
        return nullptr;
    }
}

static bool jsonReload(void* existing, const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        auto temp = nlohmann::json::parse(file);
        *static_cast<nlohmann::json*>(existing) = std::move(temp);
        return true;
    } catch (...) {
        return false;
    }
}

static bool jsonValidate(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        nlohmann::json::parse(file);
        return true;
    } catch (...) {
        return false;
    }
}

static void jsonDestroy(void* data) {
    delete static_cast<nlohmann::json*>(data);
}

AssetLoader makeJsonLoader() {
    return {
        .kind = AssetKind::Json,
        .load = jsonLoad,
        .reload = jsonReload,
        .validate = jsonValidate,
        .destroy = jsonDestroy,
        .extensions = {".json"}
    };
}

// ============================================================================
// Shader Loader
// ============================================================================

static std::string inferPartnerPath(const std::string& path) {
    namespace fs = std::filesystem;
    auto ext = fs::path(path).extension().string();
    auto stem = fs::path(path).parent_path() / fs::path(path).stem();
    if (ext == ".vert") return stem.string() + ".frag";
    if (ext == ".frag") return stem.string() + ".vert";
    return "";
}

static void* shaderLoad(const std::string& path) {
    namespace fs = std::filesystem;
    auto ext = fs::path(path).extension().string();
    std::string vertPath, fragPath;
    if (ext == ".vert") {
        vertPath = path;
        fragPath = inferPartnerPath(path);
    } else if (ext == ".frag") {
        fragPath = path;
        vertPath = inferPartnerPath(path);
    } else {
        // .glsl: assume paired .vert/.frag with same stem
        auto stem = fs::path(path).parent_path() / fs::path(path).stem();
        vertPath = stem.string() + ".vert";
        fragPath = stem.string() + ".frag";
    }

    auto* shader = new Shader();
    if (!shader->loadFromFile(vertPath, fragPath)) {
        delete shader;
        return nullptr;
    }
    return shader;
}

static bool shaderReload(void* existing, const std::string& path) {
    auto* shader = static_cast<Shader*>(existing);
    // Use stored paths for the pair
    return shader->reloadFromFile(shader->vertPath(), shader->fragPath());
}

static bool shaderValidate(const std::string& path) {
    std::ifstream file(path);
    return file.is_open() && file.peek() != std::ifstream::traits_type::eof();
}

static void shaderDestroy(void* data) {
    delete static_cast<Shader*>(data);
}

AssetLoader makeShaderLoader() {
    return {
        .kind = AssetKind::Shader,
        .load = shaderLoad,
        .reload = shaderReload,
        .validate = shaderValidate,
        .destroy = shaderDestroy,
        .extensions = {".vert", ".frag", ".glsl"}
    };
}

} // namespace fate
```

- [ ] **Step 3: Build to verify no compile errors**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add engine/asset/loaders.h engine/asset/loaders.cpp
git commit -m "feat(asset): texture, JSON, and shader loaders for AssetRegistry"
```

---

## Task 6: FileWatcher (Windows ReadDirectoryChangesW)

**Files:**
- Create: `engine/asset/file_watcher.h`
- Create: `engine/asset/file_watcher.cpp`

- [ ] **Step 1: Write the FileWatcher header**

Create `engine/asset/file_watcher.h`:

```cpp
#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fate {

class FileWatcher {
public:
    using Callback = std::function<void(const std::string& path)>;

    FileWatcher() = default;
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    void start(const std::string& directory, Callback onFileChanged);
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    std::jthread watchThread_;
    std::atomic<bool> running_{false};
    Callback callback_;
    std::string watchDir_;

#ifdef _WIN32
    HANDLE dirHandle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
#endif
};

} // namespace fate
```

- [ ] **Step 2: Write the FileWatcher implementation**

Create `engine/asset/file_watcher.cpp`:

```cpp
#include "engine/asset/file_watcher.h"
#include "engine/core/logger.h"
#include <algorithm>
#include <filesystem>

namespace fate {

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::start(const std::string& directory, Callback onFileChanged) {
    if (running_.load()) return;

    watchDir_ = directory;
    callback_ = std::move(onFileChanged);

#ifdef _WIN32
    // Convert UTF-8 to UTF-16 for Windows API
    int wlen = MultiByteToWideChar(CP_UTF8, 0, directory.c_str(), -1, nullptr, 0);
    std::wstring wdir(wlen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, directory.c_str(), -1, wdir.data(), wlen);
    dirHandle_ = CreateFileW(
        wdir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );

    if (dirHandle_ == INVALID_HANDLE_VALUE) {
        LOG_ERROR("FileWatcher", "Failed to open directory: %s", directory.c_str());
        return;
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    running_.store(true);

    watchThread_ = std::jthread([this](std::stop_token) {
        alignas(DWORD) char buffer[4096];

        while (running_.load()) {
            OVERLAPPED overlapped = {};
            overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

            BOOL result = ReadDirectoryChangesW(
                dirHandle_,
                buffer,
                sizeof(buffer),
                TRUE, // recursive
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
                nullptr,
                &overlapped,
                nullptr
            );

            if (!result) {
                CloseHandle(overlapped.hEvent);
                break;
            }

            HANDLE handles[] = { overlapped.hEvent, stopEvent_ };
            DWORD waitResult = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            CloseHandle(overlapped.hEvent);

            if (waitResult != WAIT_OBJECT_0) {
                // Stop event signaled or error
                CancelIo(dirHandle_);
                break;
            }

            DWORD bytesReturned = 0;
            GetOverlappedResult(dirHandle_, &overlapped, &bytesReturned, FALSE);

            if (bytesReturned == 0) continue;

            // Parse notification buffer
            auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
            while (true) {
                if (info->Action == FILE_ACTION_MODIFIED ||
                    info->Action == FILE_ACTION_ADDED) {
                    // Convert UTF-16 filename to UTF-8
                    std::wstring wname(info->FileName, info->FileNameLength / sizeof(WCHAR));
                    int utf8len = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), (int)wname.size(), nullptr, 0, nullptr, nullptr);
                    std::string name(utf8len, 0);
                    WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), (int)wname.size(), name.data(), utf8len, nullptr, nullptr);
                    // Normalize to forward slashes
                    std::replace(name.begin(), name.end(), '\\', '/');

                    if (callback_) callback_(name);
                }

                if (info->NextEntryOffset == 0) break;
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<char*>(info) + info->NextEntryOffset);
            }
        }
    });

    LOG_INFO("FileWatcher", "Watching: %s", directory.c_str());
#else
    LOG_WARN("FileWatcher", "File watching not implemented on this platform");
#endif
}

void FileWatcher::stop() {
    if (!running_.load()) return;
    running_.store(false);

#ifdef _WIN32
    if (stopEvent_) {
        SetEvent(stopEvent_);
    }
    if (watchThread_.joinable()) {
        watchThread_.join();
    }
    if (dirHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(dirHandle_);
        dirHandle_ = INVALID_HANDLE_VALUE;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
#endif

    LOG_INFO("FileWatcher", "Stopped watching: %s", watchDir_.c_str());
}

} // namespace fate
```

- [ ] **Step 3: Build to verify no compile errors**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -5`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add engine/asset/file_watcher.h engine/asset/file_watcher.cpp
git commit -m "feat(asset): FileWatcher using Windows ReadDirectoryChangesW"
```

---

## Task 7: Wire Hot-Reload into App Lifecycle

**Files:**
- Modify: `engine/app.h:1-72`
- Modify: `engine/app.cpp:268-297` (update method), `engine/app.cpp:331-350` (shutdown)

- [ ] **Step 1: Add FileWatcher to App**

In `engine/app.h`, add include after line 6 (`#include "engine/input/input.h"`):

```cpp
#include "engine/asset/file_watcher.h"
#include "engine/asset/asset_registry.h"
#include "engine/asset/loaders.h"
```

Add member after line 64 (`FrameArena frameArena_;`):

```cpp
    FileWatcher fileWatcher_;
    float elapsedTime_ = 0.0f;  // for reload debounce timestamps
```

- [ ] **Step 2: Register loaders and start watcher in init**

In `engine/app.h`, add a field to `AppConfig` (after `float fixedTimestep`):

```cpp
    std::string assetsDir;  // absolute path to assets/ directory (set by game)
```

In `engine/app.h`, add a member after `FrameArena frameArena_;`:

```cpp
    std::string assetsDir_;  // cached from config
```

In `engine/app.cpp`, in the `init()` method, after existing initialization but before `return true`, add:

```cpp
    assetsDir_ = config.assetsDir;

    // Register asset loaders
    AssetRegistry::instance().registerLoader(makeTextureLoader());
    AssetRegistry::instance().registerLoader(makeJsonLoader());
    AssetRegistry::instance().registerLoader(makeShaderLoader());

    // Start file watcher on assets directory
    fileWatcher_.start(assetsDir_, [this](const std::string& relativePath) {
        std::string fullPath = assetsDir_ + "/" + relativePath;
        AssetRegistry::instance().queueReload(fullPath);
    });
```

In `game/game_app.cpp` (or wherever the game creates AppConfig), set:

```cpp
    config.assetsDir = std::string(FATE_SOURCE_DIR) + "/assets";
```

This keeps `FATE_SOURCE_DIR` in the game executable (where it's already defined) and passes it to the engine via config.

- [ ] **Step 3: Call processReloads before pause guard in update**

In `engine/app.cpp`, in `App::update()` (line 268), add before the `onUpdate` call:

```cpp
    // Process asset reloads unconditionally (hot-reload works while editing)
    elapsedTime_ += deltaTime_;
    AssetRegistry::instance().processReloads(elapsedTime_);
```

- [ ] **Step 4: Fix shutdown order**

In `engine/app.cpp`, in `App::shutdown()` (line 331), add `fileWatcher_.stop()` before `TextureCache::instance().clear()`, and add `AssetRegistry::instance().clear()` **after** it. The order matters — TextureCache holds non-owning shared_ptrs to AssetRegistry-owned Textures, so TextureCache must drop its pointers first:

```cpp
    fileWatcher_.stop();
    // TextureCache holds non-owning shared_ptrs — clear first to avoid dangling refs
    TextureCache::instance().clear();
    AssetRegistry::instance().clear();
```

Remove the existing standalone `TextureCache::instance().clear()` call (currently at line 337) since we now handle it above.

- [ ] **Step 5: Build and run full test suite**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All existing + new tests pass

- [ ] **Step 6: Commit**

```bash
git add engine/app.h engine/app.cpp
git commit -m "feat(app): wire asset hot-reload into init/update/shutdown lifecycle"
```

---

## Task 8: TextureCache Migration (Compatibility Bridge)

**Files:**
- Modify: `engine/render/texture.cpp:62-81` — `TextureCache::load()` delegates to `AssetRegistry`
- Modify: `game/register_components.h:190-192` — update SpriteComponent deserializer

- [ ] **Step 1: Redirect TextureCache through AssetRegistry**

In `engine/render/texture.cpp`, add include at top:

```cpp
#include "engine/asset/asset_registry.h"
```

Replace the `TextureCache::load()` body (lines 63-72):

```cpp
std::shared_ptr<Texture> TextureCache::load(const std::string& path) {
    auto it = cache_.find(path);
    if (it != cache_.end()) return it->second;

    // Delegate to AssetRegistry for actual loading
    AssetHandle h = AssetRegistry::instance().load(path);
    Texture* raw = AssetRegistry::instance().get<Texture>(h);
    if (!raw) return nullptr;

    // Non-owning shared_ptr — AssetRegistry owns the lifetime
    auto tex = std::shared_ptr<Texture>(raw, [](Texture*){});
    cache_[path] = tex;
    return tex;
}
```

- [ ] **Step 2: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All tests pass (TextureCache now goes through AssetRegistry)

- [ ] **Step 3: Commit**

```bash
git add engine/render/texture.cpp
git commit -m "refactor(texture): TextureCache delegates loading to AssetRegistry"
```

---

## Task 9: CMake — ImPlot & ENGINE_MEMORY_DEBUG

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Vendor ImPlot**

Download ImPlot files into the project. Check if ImPlot is available via FetchContent or needs manual vendoring. Add to CMakeLists.txt after the `imgui_lib` target (after line 107):

In `CMakeLists.txt`, add FetchContent for ImPlot after the imgui FetchContent block (around line 60):

```cmake
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        v0.16
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(implot)
```

Add ImPlot sources to the `imgui_lib` target. Replace lines 94-107 (the entire imgui_lib definition including target_link_libraries) with:

```cmake
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)
target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static ${OPENGL_LIB})
```

- [ ] **Step 2: Add ENGINE_MEMORY_DEBUG option**

After the Tracy options (around line 80), add:

```cmake
option(ENGINE_MEMORY_DEBUG "Enable allocator debug visualization panels" ON)
```

After the `fate_engine` target_link_libraries (after line 129), add:

```cmake
if(ENGINE_MEMORY_DEBUG)
    target_compile_definitions(fate_engine PUBLIC ENGINE_MEMORY_DEBUG)
endif()
```

- [ ] **Step 3: Build to verify ImPlot links**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -10`
Expected: Build succeeds with ImPlot included

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add ImPlot dependency and ENGINE_MEMORY_DEBUG option"
```

---

## Task 10: Pool Occupancy Bitmap (Debug-Only)

**Files:**
- Modify: `engine/memory/pool.h:1-65`
- Create: `tests/test_pool_bitmap.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/test_pool_bitmap.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/memory/pool.h"
#include "engine/memory/arena.h"

#if defined(ENGINE_MEMORY_DEBUG)

TEST_CASE("Pool occupancy bitmap tracks alloc/free") {
    fate::Arena arena(1024 * 1024);
    fate::PoolAllocator pool;
    pool.init(arena, 64, 16); // 16 blocks of 64 bytes

    const uint8_t* bitmap = pool.occupancyBitmap();
    REQUIRE(bitmap != nullptr);

    // Initially all free
    CHECK(bitmap[0] == 0x00);
    CHECK(bitmap[1] == 0x00);

    // Alloc block 0
    void* b0 = pool.alloc();
    CHECK((bitmap[0] & 0x01) != 0);

    // Alloc block 1
    void* b1 = pool.alloc();
    CHECK((bitmap[0] & 0x02) != 0);

    // Free block 0
    pool.free(b0);
    CHECK((bitmap[0] & 0x01) == 0);
    CHECK((bitmap[0] & 0x02) != 0); // block 1 still set

    pool.free(b1);
    CHECK(bitmap[0] == 0x00);
}

#endif // ENGINE_MEMORY_DEBUG
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -20`
Expected: Compile error — `occupancyBitmap()` not found

- [ ] **Step 3: Add bitmap to PoolAllocator**

In `engine/memory/pool.h`, add at the top after includes:

```cpp
#include <cstring>
```

In the `init()` method, after the free list construction (after line 33), add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
        size_t bitmapBytes = (blockCount_ + 7) / 8;
        occupancyBitmap_ = static_cast<uint8_t*>(arena.push(bitmapBytes, 1));
        std::memset(occupancyBitmap_, 0, bitmapBytes);
#endif
```

In `alloc()`, after `++activeCount_` (after line 40), add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
        size_t idx = (static_cast<uint8_t*>(block) - memory_) / blockSize_;
        occupancyBitmap_[idx / 8] |= (1 << (idx % 8));
#endif
```

In `free()`, after `--activeCount_` (after line 49), add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
        size_t idx = (static_cast<uint8_t*>(block) - memory_) / blockSize_;
        occupancyBitmap_[idx / 8] &= ~(1 << (idx % 8));
#endif
```

Add to private section (after line 62):

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    uint8_t* occupancyBitmap_ = nullptr;
public:
    const uint8_t* occupancyBitmap() const { return occupancyBitmap_; }
private:
#endif
```

- [ ] **Step 4: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="Pool occupancy*"`
Expected: Pool bitmap test passes

- [ ] **Step 5: Commit**

```bash
git add engine/memory/pool.h tests/test_pool_bitmap.cpp
git commit -m "feat(memory): debug-only occupancy bitmap for PoolAllocator"
```

---

## Task 11: AllocatorRegistry

**Files:**
- Create: `engine/memory/allocator_registry.h`
- Create: `tests/test_allocator_registry.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_allocator_registry.cpp`:

```cpp
#include <doctest/doctest.h>

#if defined(ENGINE_MEMORY_DEBUG)
#include "engine/memory/allocator_registry.h"

TEST_CASE("AllocatorRegistry add and query") {
    auto& reg = fate::AllocatorRegistry::instance();
    // Clear any prior state
    while (!reg.all().empty()) {
        reg.remove(reg.all().front().name);
    }

    size_t testVal = 42;
    reg.add({
        .name = "TestArena",
        .type = fate::AllocatorType::Arena,
        .getUsed = [&]() -> size_t { return testVal; },
        .getCommitted = [&]() -> size_t { return 100; },
        .getReserved = [&]() -> size_t { return 1000; },
    });

    CHECK(reg.all().size() == 1);
    CHECK(std::string(reg.all()[0].name) == "TestArena");
    CHECK(reg.all()[0].getUsed() == 42);

    testVal = 99;
    CHECK(reg.all()[0].getUsed() == 99);
}

TEST_CASE("AllocatorRegistry remove by name") {
    auto& reg = fate::AllocatorRegistry::instance();
    while (!reg.all().empty()) {
        reg.remove(reg.all().front().name);
    }

    reg.add({ .name = "A", .type = fate::AllocatorType::Arena });
    reg.add({ .name = "B", .type = fate::AllocatorType::Pool });
    CHECK(reg.all().size() == 2);

    reg.remove("A");
    CHECK(reg.all().size() == 1);
    CHECK(std::string(reg.all()[0].name) == "B");

    reg.remove("nonexistent"); // no-op
    CHECK(reg.all().size() == 1);
}

#endif // ENGINE_MEMORY_DEBUG
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -20`
Expected: Compile error — `engine/memory/allocator_registry.h` not found

- [ ] **Step 3: Write the AllocatorRegistry**

Create `engine/memory/allocator_registry.h`:

```cpp
#pragma once

#if defined(ENGINE_MEMORY_DEBUG)

#include <vector>
#include <string>
#include <functional>
#include <cstring>
#include <algorithm>

namespace fate {

enum class AllocatorType : uint8_t { Arena, FrameArena, Pool };

struct AllocatorInfo {
    const char* name = "";
    AllocatorType type = AllocatorType::Arena;
    std::function<size_t()> getUsed;
    std::function<size_t()> getCommitted;
    std::function<size_t()> getReserved;
    // Pool-specific (empty for arenas)
    std::function<size_t()> getActiveBlocks;
    std::function<size_t()> getTotalBlocks;
    std::function<const uint8_t*()> getOccupancyBitmap;
};

class AllocatorRegistry {
public:
    static AllocatorRegistry& instance() {
        static AllocatorRegistry s_instance;
        return s_instance;
    }

    void add(AllocatorInfo info) {
        allocators_.push_back(std::move(info));
    }

    void remove(const char* name) {
        allocators_.erase(
            std::remove_if(allocators_.begin(), allocators_.end(),
                [name](const AllocatorInfo& a) { return std::strcmp(a.name, name) == 0; }),
            allocators_.end()
        );
    }

    const std::vector<AllocatorInfo>& all() const { return allocators_; }

private:
    AllocatorRegistry() = default;
    std::vector<AllocatorInfo> allocators_;
};

} // namespace fate

#endif // ENGINE_MEMORY_DEBUG
```

- [ ] **Step 4: Build and run tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe -tc="AllocatorRegistry*"`
Expected: Both AllocatorRegistry tests pass

- [ ] **Step 5: Commit**

```bash
git add engine/memory/allocator_registry.h tests/test_allocator_registry.cpp
git commit -m "feat(memory): AllocatorRegistry for debug visualization"
```

---

## Task 12: Memory Debug Panel (ImGui/ImPlot)

**Files:**
- Create: `engine/editor/memory_panel.h`
- Create: `engine/editor/memory_panel.cpp`

- [ ] **Step 1: Write the panel header**

Create `engine/editor/memory_panel.h`:

```cpp
#pragma once

#if defined(ENGINE_MEMORY_DEBUG)

namespace fate {

class FrameArena;

// Call once per frame from Editor::renderUI()
void drawMemoryPanel(bool* open, FrameArena* frameArena);

} // namespace fate

#endif // ENGINE_MEMORY_DEBUG
```

- [ ] **Step 2: Write the panel implementation**

Create `engine/editor/memory_panel.cpp`:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)

#include "engine/editor/memory_panel.h"
#include "engine/memory/allocator_registry.h"
#include "engine/memory/arena.h"
#include <imgui.h>
#include <implot.h>
#include <cstdio>
#include <algorithm>

namespace fate {

// Ring buffer for frame arena timeline
static constexpr int kTimelineSamples = 300;
static float s_timelineBuf[kTimelineSamples] = {};
static int s_timelineHead = 0;
static float s_highWaterMark = 0.0f;

static const char* formatBytes(size_t bytes, char* buf, size_t bufSize) {
    if (bytes >= 1024 * 1024)
        snprintf(buf, bufSize, "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, bufSize, "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, bufSize, "%zu B", bytes);
    return buf;
}

static void drawArenaTab() {
    const auto& allocators = AllocatorRegistry::instance().all();
    for (const auto& a : allocators) {
        if (a.type == AllocatorType::Pool) continue;
        if (!a.getUsed || !a.getReserved) continue;

        size_t used = a.getUsed();
        size_t reserved = a.getReserved();
        size_t committed = a.getCommitted ? a.getCommitted() : 0;
        float ratio = reserved > 0 ? static_cast<float>(used) / reserved : 0.0f;

        // Color-coded progress bar
        ImVec4 color;
        if (ratio < 0.7f)      color = ImVec4(0.2f, 0.8f, 0.3f, 1.0f); // green
        else if (ratio < 0.9f) color = ImVec4(0.9f, 0.8f, 0.1f, 1.0f); // yellow
        else                   color = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // red

        char usedBuf[32], reservedBuf[32];
        formatBytes(used, usedBuf, sizeof(usedBuf));
        formatBytes(reserved, reservedBuf, sizeof(reservedBuf));

        char overlay[128];
        snprintf(overlay, sizeof(overlay), "%s: %s / %s (%.1f%%)",
                 a.name, usedBuf, reservedBuf, ratio * 100.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
        ImGui::ProgressBar(ratio, ImVec2(-1, 0), overlay);
        ImGui::PopStyleColor();

        // Committed bar (dimmer)
        if (committed > 0 && reserved > 0) {
            float commitRatio = static_cast<float>(committed) / reserved;
            char commitBuf[32];
            formatBytes(committed, commitBuf, sizeof(commitBuf));
            char commitOverlay[128];
            snprintf(commitOverlay, sizeof(commitOverlay), "  Committed: %s", commitBuf);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.4f, 0.4f, 0.5f, 0.6f));
            ImGui::ProgressBar(commitRatio, ImVec2(-1, 0), commitOverlay);
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
    }
}

static void drawPoolTab() {
    const auto& allocators = AllocatorRegistry::instance().all();
    for (const auto& a : allocators) {
        if (a.type != AllocatorType::Pool) continue;
        if (!a.getActiveBlocks || !a.getTotalBlocks) continue;

        size_t active = a.getActiveBlocks();
        size_t total = a.getTotalBlocks();

        char summary[128];
        snprintf(summary, sizeof(summary), "%s: %zu/%zu active (%.1f%%)",
                 a.name, active, total,
                 total > 0 ? (active * 100.0 / total) : 0.0);
        ImGui::Text("%s", summary);

        // Heat map grid
        if (a.getOccupancyBitmap) {
            const uint8_t* bitmap = a.getOccupancyBitmap();
            if (bitmap) {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                float cellSize = total > 1024 ? 3.0f : 6.0f;
                int cols = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cellSize + 1)));

                for (size_t i = 0; i < total; ++i) {
                    int col = static_cast<int>(i % cols);
                    int row = static_cast<int>(i / cols);
                    ImVec2 p0(cursor.x + col * (cellSize + 1), cursor.y + row * (cellSize + 1));
                    ImVec2 p1(p0.x + cellSize, p0.y + cellSize);

                    bool occupied = (bitmap[i / 8] & (1 << (i % 8))) != 0;
                    ImU32 c = occupied ? IM_COL32(220, 50, 50, 255) : IM_COL32(80, 80, 80, 255);
                    draw->AddRectFilled(p0, p1, c);

                    if (ImGui::IsMouseHoveringRect(p0, p1)) {
                        ImGui::SetTooltip("Block %zu: %s", i, occupied ? "occupied" : "free");
                    }
                }

                int rows = static_cast<int>((total + cols - 1) / cols);
                ImGui::Dummy(ImVec2(0, rows * (cellSize + 1) + 4));
            }
        }

        ImGui::Separator();
    }
}

static void drawTimelineTab(FrameArena* frameArena) {
    if (!frameArena) {
        ImGui::Text("No FrameArena available");
        return;
    }

    // Sample current frame's allocation
    float used = static_cast<float>(frameArena->current().position());
    s_timelineBuf[s_timelineHead] = used;
    s_timelineHead = (s_timelineHead + 1) % kTimelineSamples;
    s_highWaterMark = std::max(s_highWaterMark, used);

    if (ImPlot::BeginPlot("Frame Arena Usage", ImVec2(-1, 200))) {
        ImPlot::SetupAxes("Frame", "Bytes", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);

        // Build ordered array from ring buffer
        float ordered[kTimelineSamples];
        for (int i = 0; i < kTimelineSamples; ++i) {
            ordered[i] = s_timelineBuf[(s_timelineHead + i) % kTimelineSamples];
        }

        ImPlot::PlotLine("Usage", ordered, kTimelineSamples);

        // High water mark line
        float hwm[kTimelineSamples];
        for (int i = 0; i < kTimelineSamples; ++i) hwm[i] = s_highWaterMark;
        ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(1, 0.3f, 0.3f, 0.7f));
        ImPlot::PlotLine("High Water", hwm, kTimelineSamples);
        ImPlot::PopStyleColor();

        ImPlot::EndPlot();
    }

    char hwmBuf[32];
    formatBytes(static_cast<size_t>(s_highWaterMark), hwmBuf, sizeof(hwmBuf));
    ImGui::Text("High Water Mark: %s", hwmBuf);

    if (ImGui::Button("Reset High Water Mark")) {
        s_highWaterMark = 0.0f;
    }
}

void drawMemoryPanel(bool* open, FrameArena* frameArena) {
    if (!*open) return;

    if (ImGui::Begin("Memory", open)) {
        if (ImGui::BeginTabBar("MemoryTabs")) {
            if (ImGui::BeginTabItem("Arenas")) {
                drawArenaTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Pools")) {
                drawPoolTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Frame Timeline")) {
                drawTimelineTab(frameArena);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace fate

#endif // ENGINE_MEMORY_DEBUG
```

- [ ] **Step 3: Build to verify no compile errors**

Run: `"$CMAKE" --build out/build --config Debug 2>&1 | tail -10`
Expected: Build succeeds

- [ ] **Step 4: Commit**

```bash
git add engine/editor/memory_panel.h engine/editor/memory_panel.cpp
git commit -m "feat(editor): memory debug panel with arena watermarks, pool heat maps, ImPlot timeline"
```

---

## Task 13: Wire Memory Panel into Editor

**Files:**
- Modify: `engine/editor/editor.h:141-236`
- Modify: `engine/editor/editor.cpp:167-190` (renderUI)

- [ ] **Step 1: Add panel toggle to Editor**

In `engine/editor/editor.h`, add include after line 8 (`#include "engine/render/framebuffer.h"`):

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
#include "engine/editor/memory_panel.h"
#include <implot.h>
#endif
```

In `engine/editor/editor.cpp`, in `Editor::init()`, after `ImGui::CreateContext()`, add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    ImPlot::CreateContext();
#endif
```

In `engine/editor/editor.cpp`, in `Editor::shutdown()`, before `ImGui::DestroyContext()`, add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    ImPlot::DestroyContext();
#endif
```

Add member after line 149 (`bool showDemoWindow_ = false;`):

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    bool showMemoryPanel_ = false;
#endif
```

- [ ] **Step 2: Add to renderUI and View menu**

In `engine/editor/editor.cpp`, in `renderUI()` (line 167), add before `if (showDemoWindow_)` (line 184):

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    if (showMemoryPanel_) {
        // Need FrameArena* — accessed through App, which Editor doesn't hold.
        // Pass nullptr for now; we'll wire it through renderUI's caller.
        drawMemoryPanel(&showMemoryPanel_, nullptr);
    }
#endif
```

For the View menu, find where menu items are drawn in `drawDockSpace()`. Add a menu item for the memory panel in the View submenu:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    ImGui::MenuItem("Memory", nullptr, &showMemoryPanel_);
#endif
```

- [ ] **Step 3: Pass FrameArena to the memory panel**

In `engine/editor/editor.h`, modify `renderUI` signature to accept a `FrameArena*`:

```cpp
void renderUI(World* world, Camera* camera, SpriteBatch* batch, FrameArena* frameArena = nullptr);
```

In `engine/editor/editor.cpp`, update the `renderUI` signature to match, and update the `drawMemoryPanel` call to pass the FrameArena:

```cpp
drawMemoryPanel(&showMemoryPanel_, frameArena);
```

In `engine/app.cpp`, update the `renderUI` call in `render()` to pass the frame arena:

```cpp
editor.renderUI(world, &camera_, &spriteBatch_, &frameArena_);
```

- [ ] **Step 4: Build and run to verify panel renders**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All tests pass. Run the game and check View > Memory panel appears.

- [ ] **Step 5: Commit**

```bash
git add engine/editor/editor.h engine/editor/editor.cpp engine/app.cpp
git commit -m "feat(editor): wire memory debug panel into View menu and renderUI"
```

---

## Task 14: Register Allocators with AllocatorRegistry

**Files:**
- Modify: `engine/ecs/world.h` / `engine/ecs/world.cpp` — register zone arena
- Modify: `engine/app.cpp` — register frame arena

- [ ] **Step 1: Register zone arena in World**

In `engine/ecs/world.h`, add include:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
#include "engine/memory/allocator_registry.h"
#endif
```

In `engine/ecs/world.cpp`, in the World constructor (after arena initialization), add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    AllocatorRegistry::instance().add({
        .name = "WorldArena",
        .type = AllocatorType::Arena,
        .getUsed = [this]() -> size_t { return arena_.position(); },
        .getCommitted = [this]() -> size_t { return arena_.committed(); },
        .getReserved = [this]() -> size_t { return arena_.reserved(); },
    });
#endif
```

In the World destructor, add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    AllocatorRegistry::instance().remove("WorldArena");
#endif
```

- [ ] **Step 2: Register frame arena in App**

In `engine/app.cpp`, in `App::init()` (after frame arena is constructed), add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    AllocatorRegistry::instance().add({
        .name = "FrameArena",
        .type = AllocatorType::FrameArena,
        .getUsed = [this]() -> size_t { return frameArena_.current().position(); },
        .getCommitted = [this]() -> size_t { return frameArena_.current().committed(); },
        .getReserved = [this]() -> size_t { return frameArena_.current().reserved(); },
    });
#endif
```

In `App::shutdown()`, add:

```cpp
#if defined(ENGINE_MEMORY_DEBUG)
    AllocatorRegistry::instance().remove("FrameArena");
#endif
```

- [ ] **Step 3: Build and run**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All tests pass. Memory panel should show WorldArena and FrameArena bars.

- [ ] **Step 4: Commit**

```bash
git add engine/ecs/world.h engine/ecs/world.cpp engine/app.cpp
git commit -m "feat(memory): register world and frame arenas with AllocatorRegistry"
```

---

## Task 15: Integration Test — Full Reload Cycle

**Files:**
- Modify: `tests/test_asset_registry.cpp` — add reload cycle test

- [ ] **Step 1: Add reload integration test**

Add to `tests/test_asset_registry.cpp`:

```cpp
TEST_CASE("AssetRegistry reload bumps generation") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    REQUIRE(h.valid());
    int* val = reg.get<int>(h);
    REQUIRE(val != nullptr);
    CHECK(*val == 42);

    // Queue and process reload
    reg.queueReload("test.mock");
    reg.processReloads(1.0f); // stamp time
    reg.processReloads(2.0f); // past 300ms debounce

    // Old handle is now stale
    CHECK(reg.get<int>(h) == nullptr);

    // Get fresh handle
    auto h2 = reg.find("test.mock");
    CHECK(h2.generation() == h.generation() + 1);
    int* val2 = reg.get<int>(h2);
    REQUIRE(val2 != nullptr);
    CHECK(*val2 == 99); // reload sets to 99
}

TEST_CASE("AssetRegistry debounce prevents premature reload") {
    auto& reg = fate::AssetRegistry::instance();
    reg.clear();
    reg.registerLoader(makeMockLoader());

    auto h = reg.load("test.mock");
    reg.queueReload("test.mock");

    // Process immediately (within debounce window)
    reg.processReloads(0.1f); // stamp
    reg.processReloads(0.2f); // only 0.1s elapsed, < 0.3s debounce

    // Handle should still be valid (not reloaded yet)
    CHECK(reg.get<int>(h) != nullptr);

    // Now past debounce
    reg.processReloads(0.5f); // 0.4s since stamp
    CHECK(reg.get<int>(h) == nullptr); // reloaded, old handle stale
}
```

- [ ] **Step 2: Build and run all tests**

Run: `"$CMAKE" --build out/build --config Debug && ./out/build/Debug/fate_tests.exe`
Expected: All tests pass including new reload cycle tests

- [ ] **Step 3: Commit**

```bash
git add tests/test_asset_registry.cpp
git commit -m "test(asset): integration tests for reload cycle and debounce"
```

---

## Task 16: Final Build Verification & Cleanup

- [ ] **Step 1: Full clean build**

Run: `"$CMAKE" --build out/build --config Debug --clean-first 2>&1 | tail -20`
Expected: Clean build succeeds with zero errors

- [ ] **Step 2: Run full test suite**

Run: `./out/build/Debug/fate_tests.exe`
Expected: All tests pass (existing 120 + new ~15 = ~135 total)

- [ ] **Step 3: Manual smoke test**

Launch the game. Verify:
1. Game loads normally (textures render, no crashes)
2. View > Memory panel opens and shows arena watermark bars
3. Frame Timeline tab shows a scrolling line chart
4. Edit `assets/shaders/sprite.frag` (add a comment) and save — check console for "Reloaded" log message
5. No performance regression (check FPS counter)

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "feat: Phase 3 complete — asset hot-reload and allocator visualization"
```
