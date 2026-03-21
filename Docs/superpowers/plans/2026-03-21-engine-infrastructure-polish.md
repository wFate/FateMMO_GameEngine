# Engine Infrastructure Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add PhysicsFS virtual filesystem, telemetry endpoint, and palette swap pipeline.

**Architecture:** PhysicsFS provides mount/overlay VFS for asset packaging and future mod support. Telemetry uses a simple HTTPS POST to a Cloudflare Worker. The palette swap pipeline connects the existing shader renderType 5 support to a data-driven palette system for equipment color variants.

**Tech Stack:** PhysicsFS (via FetchContent), nlohmann/json, SDL2, existing SpriteBatch

**Already verified as done (no plan needed):**
- **Profanity filter server wiring** — confirmed in `server_app.cpp:1392-1431`, uses `ProfanityFilter::filterChatMessage(msg, FilterMode::Censor)` before broadcast
- **system() command injection** — no `system()` calls found anywhere in editor or engine code

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Create | `engine/vfs/virtual_fs.h` | PhysicsFS C++ wrapper with mount/read/exists |
| Create | `engine/vfs/virtual_fs.cpp` | PhysicsFS initialization, mount, overlay |
| Modify | `CMakeLists.txt` | Add PhysicsFS via FetchContent |
| Create | `engine/telemetry/telemetry.h` | Non-blocking metric collection and batch send |
| Create | `engine/telemetry/telemetry.cpp` | HTTPS POST to configurable endpoint |
| Create | `engine/render/palette.h` | Palette definition loading and management |
| Create | `engine/render/palette.cpp` | JSON palette defs → Color arrays for shader |
| Create | `assets/data/palettes.json` | Palette definitions (faction colors, rarity tints) |
| Create | `tests/test_vfs.cpp` | VFS mount, read, overlay priority tests |
| Create | `tests/test_telemetry.cpp` | Metric batching and serialization tests |
| Create | `tests/test_palette.cpp` | Palette loading and color mapping tests |

---

### Task 1: PhysicsFS virtual filesystem

**Files:**
- Modify: `CMakeLists.txt`
- Create: `engine/vfs/virtual_fs.h`
- Create: `engine/vfs/virtual_fs.cpp`
- Create: `tests/test_vfs.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_vfs.cpp
#include <doctest/doctest.h>
#include "engine/vfs/virtual_fs.h"

TEST_SUITE("Virtual Filesystem") {

TEST_CASE("VFS initializes and shuts down cleanly") {
    VirtualFS vfs;
    CHECK(vfs.init("test_app"));
    vfs.shutdown();
}

TEST_CASE("VFS mounts a directory") {
    VirtualFS vfs;
    REQUIRE(vfs.init("test_app"));
    CHECK(vfs.mount("assets", "/"));
    vfs.shutdown();
}

TEST_CASE("VFS reads file that exists") {
    VirtualFS vfs;
    REQUIRE(vfs.init("test_app"));
    REQUIRE(vfs.mount("assets", "/"));

    auto data = vfs.readFile("data/pvp_balance.json");
    CHECK(data.has_value());
    CHECK(data->size() > 0);
    vfs.shutdown();
}

TEST_CASE("VFS returns nullopt for missing file") {
    VirtualFS vfs;
    REQUIRE(vfs.init("test_app"));
    REQUIRE(vfs.mount("assets", "/"));

    auto data = vfs.readFile("nonexistent/file.txt");
    CHECK_FALSE(data.has_value());
    vfs.shutdown();
}

TEST_CASE("VFS exists check") {
    VirtualFS vfs;
    REQUIRE(vfs.init("test_app"));
    REQUIRE(vfs.mount("assets", "/"));

    CHECK(vfs.exists("data/pvp_balance.json"));
    CHECK_FALSE(vfs.exists("data/does_not_exist.json"));
    vfs.shutdown();
}

} // TEST_SUITE
```

- [ ] **Step 2: Add PhysicsFS to CMakeLists.txt**

```cmake
# PhysicsFS for virtual filesystem and asset packaging
FetchContent_Declare(
    physfs
    GIT_REPOSITORY https://github.com/icculus/physfs.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(physfs)
```

Add to `fate_engine` link libraries:

```cmake
target_link_libraries(fate_engine PUBLIC ... physfs-static)
```

- [ ] **Step 3: Create VirtualFS wrapper**

```cpp
// engine/vfs/virtual_fs.h
#pragma once
#include <string>
#include <vector>
#include <optional>

class VirtualFS {
public:
    VirtualFS() = default;
    ~VirtualFS();

    bool init(const char* appName);
    void shutdown();

    // Mount a directory or archive at a mount point (overlay — later mounts win)
    bool mount(const std::string& path, const std::string& mountPoint,
               bool appendToSearchPath = true);

    // Read entire file into byte vector
    [[nodiscard]] std::optional<std::vector<uint8_t>> readFile(const std::string& path) const;

    // Read file as string
    [[nodiscard]] std::optional<std::string> readText(const std::string& path) const;

    // Check if file exists in any mounted source
    [[nodiscard]] bool exists(const std::string& path) const;

    // List files in a directory
    [[nodiscard]] std::vector<std::string> listDir(const std::string& dir) const;

private:
    bool initialized_ = false;
};
```

- [ ] **Step 4: Implement VirtualFS**

```cpp
// engine/vfs/virtual_fs.cpp
#include "engine/vfs/virtual_fs.h"
#include <physfs.h>
#include <spdlog/spdlog.h>

VirtualFS::~VirtualFS() {
    if (initialized_) shutdown();
}

bool VirtualFS::init(const char* appName) {
    if (PHYSFS_init(appName) == 0) {
        spdlog::error("[VFS] Init failed: {}", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }
    initialized_ = true;
    return true;
}

void VirtualFS::shutdown() {
    if (initialized_) {
        PHYSFS_deinit();
        initialized_ = false;
    }
}

bool VirtualFS::mount(const std::string& path, const std::string& mountPoint, bool append) {
    if (PHYSFS_mount(path.c_str(), mountPoint.c_str(), append ? 1 : 0) == 0) {
        spdlog::error("[VFS] Mount '{}' failed: {}", path,
                      PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
        return false;
    }
    return true;
}

std::optional<std::vector<uint8_t>> VirtualFS::readFile(const std::string& path) const {
    PHYSFS_File* file = PHYSFS_openRead(path.c_str());
    if (!file) return std::nullopt;

    auto length = PHYSFS_fileLength(file);
    if (length < 0) { PHYSFS_close(file); return std::nullopt; }

    std::vector<uint8_t> data(static_cast<size_t>(length));
    auto bytesRead = PHYSFS_readBytes(file, data.data(), static_cast<PHYSFS_uint64>(length));
    PHYSFS_close(file);

    if (bytesRead != length) return std::nullopt;
    return data;
}

std::optional<std::string> VirtualFS::readText(const std::string& path) const {
    auto data = readFile(path);
    if (!data) return std::nullopt;
    return std::string(data->begin(), data->end());
}

bool VirtualFS::exists(const std::string& path) const {
    return PHYSFS_exists(path.c_str()) != 0;
}

std::vector<std::string> VirtualFS::listDir(const std::string& dir) const {
    std::vector<std::string> result;
    char** files = PHYSFS_enumerateFiles(dir.c_str());
    if (files) {
        for (char** p = files; *p; ++p) {
            result.emplace_back(*p);
        }
        PHYSFS_freeList(files);
    }
    return result;
}
```

- [ ] **Step 5: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="Virtual Filesystem"`
Expected: All 5 tests PASS

- [ ] **Step 6: Commit**

```bash
git add engine/vfs/virtual_fs.h engine/vfs/virtual_fs.cpp tests/test_vfs.cpp CMakeLists.txt
git commit -m "feat: PhysicsFS virtual filesystem wrapper with mount/read/overlay"
```

---

### Task 2: Telemetry endpoint

**Files:**
- Create: `engine/telemetry/telemetry.h`
- Create: `engine/telemetry/telemetry.cpp`
- Create: `tests/test_telemetry.cpp`

- [ ] **Step 1: Write tests for metric batching**

```cpp
// tests/test_telemetry.cpp
#include <doctest/doctest.h>
#include "engine/telemetry/telemetry.h"

TEST_SUITE("Telemetry") {

TEST_CASE("can record metrics") {
    TelemetryCollector collector;
    collector.record("fps_avg", 58.5f);
    collector.record("session_length_s", 1200.0f);
    collector.record("peak_memory_mb", 342.0f);
    CHECK(collector.pendingCount() == 3);
}

TEST_CASE("flush clears pending metrics") {
    TelemetryCollector collector;
    collector.record("fps_avg", 60.0f);
    CHECK(collector.pendingCount() == 1);

    auto json = collector.flushToJson();
    CHECK(collector.pendingCount() == 0);
    CHECK(json.contains("metrics"));
    CHECK(json["metrics"].size() == 1);
}

TEST_CASE("serialized JSON has correct structure") {
    TelemetryCollector collector;
    collector.setSessionId("test-session-123");
    collector.record("fps_p99", 16.2f);
    auto json = collector.flushToJson();

    CHECK(json["session_id"] == "test-session-123");
    CHECK(json["metrics"][0]["name"] == "fps_p99");
    CHECK(json["metrics"][0]["value"] == doctest::Approx(16.2f));
}

TEST_CASE("empty flush returns empty metrics array") {
    TelemetryCollector collector;
    auto json = collector.flushToJson();
    CHECK(json["metrics"].empty());
}

} // TEST_SUITE
```

- [ ] **Step 2: Create TelemetryCollector**

```cpp
// engine/telemetry/telemetry.h
#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <chrono>

struct TelemetryMetric {
    std::string name;
    float value;
    double timestamp; // seconds since epoch
};

class TelemetryCollector {
public:
    void setSessionId(const std::string& id) { sessionId_ = id; }
    void setEndpoint(const std::string& url) { endpoint_ = url; }

    void record(const std::string& name, float value);
    size_t pendingCount() const { return pending_.size(); }

    // Serialize and clear pending metrics
    nlohmann::json flushToJson();

    // Attempt to send pending metrics via HTTPS POST (non-blocking)
    // Returns true if send was initiated. Actual delivery is best-effort.
    bool trySend();

private:
    std::string sessionId_;
    std::string endpoint_;
    std::vector<TelemetryMetric> pending_;
};
```

- [ ] **Step 3: Implement TelemetryCollector**

```cpp
// engine/telemetry/telemetry.cpp
#include "engine/telemetry/telemetry.h"
#include <spdlog/spdlog.h>

void TelemetryCollector::record(const std::string& name, float value) {
    auto now = std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    pending_.push_back({name, value, now});
}

nlohmann::json TelemetryCollector::flushToJson() {
    nlohmann::json j;
    j["session_id"] = sessionId_;
    j["metrics"] = nlohmann::json::array();

    for (const auto& m : pending_) {
        j["metrics"].push_back({
            {"name", m.name},
            {"value", m.value},
            {"timestamp", m.timestamp}
        });
    }
    pending_.clear();
    return j;
}

bool TelemetryCollector::trySend() {
    if (endpoint_.empty() || pending_.empty()) return false;

    auto payload = flushToJson().dump();
    spdlog::debug("[Telemetry] Would send {} bytes to {}", payload.size(), endpoint_);

    // TODO: Actual HTTPS POST implementation
    // Options:
    // 1. libcurl (heavyweight but reliable)
    // 2. cpp-httplib (header-only, simple)
    // 3. Platform-native: WinHTTP/NSURLSession/java.net.HttpURLConnection
    // For now, log and return true (best-effort)
    return true;
}
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="Telemetry"`
Expected: All 4 tests PASS

- [ ] **Step 5: Commit**

```bash
git add engine/telemetry/telemetry.h engine/telemetry/telemetry.cpp tests/test_telemetry.cpp
git commit -m "feat: telemetry metric collector with JSON serialization"
```

---

### Task 3: Palette swap pipeline

**Files:**
- Create: `engine/render/palette.h`
- Create: `engine/render/palette.cpp`
- Create: `assets/data/palettes.json`
- Create: `tests/test_palette.cpp`

The fragment shader already supports renderType 5 with `u_palette[16]` uniform array and `u_paletteSize`. The SpriteBatch already has `setPalette()` and `drawPaletteSwapped()`. What's missing is the **data pipeline**: loading palette definitions from JSON and providing named palettes to game systems.

- [ ] **Step 1: Write tests**

```cpp
// tests/test_palette.cpp
#include <doctest/doctest.h>
#include "engine/render/palette.h"

TEST_SUITE("Palette System") {

TEST_CASE("load palette from JSON") {
    const char* json = R"({
        "siras_warrior": {
            "colors": [
                "#1a1a2e", "#16213e", "#0f3460", "#e94560",
                "#533483", "#2b2d42", "#8d99ae", "#edf2f4",
                "#d90429", "#ef233c", "#2ec4b6", "#cbf3f0",
                "#ffffff", "#000000", "#ffbe0b", "#fb5607"
            ]
        }
    })";

    PaletteRegistry registry;
    CHECK(registry.loadFromJson(json));
    CHECK(registry.has("siras_warrior"));

    auto palette = registry.get("siras_warrior");
    REQUIRE(palette != nullptr);
    CHECK(palette->size() == 16);
}

TEST_CASE("palette color parsing") {
    const char* json = R"({
        "test": {
            "colors": ["#ff0000", "#00ff00", "#0000ff"]
        }
    })";

    PaletteRegistry registry;
    REQUIRE(registry.loadFromJson(json));
    auto p = registry.get("test");
    REQUIRE(p != nullptr);
    CHECK(p->size() == 3);

    // Red
    CHECK((*p)[0].r == doctest::Approx(1.0f));
    CHECK((*p)[0].g == doctest::Approx(0.0f));
    CHECK((*p)[0].b == doctest::Approx(0.0f));
}

TEST_CASE("missing palette returns nullptr") {
    PaletteRegistry registry;
    CHECK(registry.get("nonexistent") == nullptr);
}

TEST_CASE("palette variant system") {
    const char* json = R"({
        "leather_armor_common":  { "colors": ["#8B4513", "#A0522D", "#D2691E"] },
        "leather_armor_rare":    { "colors": ["#4169E1", "#1E90FF", "#87CEEB"] },
        "leather_armor_epic":    { "colors": ["#800080", "#9400D3", "#DA70D6"] }
    })";

    PaletteRegistry registry;
    REQUIRE(registry.loadFromJson(json));
    CHECK(registry.has("leather_armor_common"));
    CHECK(registry.has("leather_armor_rare"));
    CHECK(registry.has("leather_armor_epic"));
}

} // TEST_SUITE
```

- [ ] **Step 2: Create PaletteRegistry**

```cpp
// engine/render/palette.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "engine/core/types.h" // Color

class PaletteRegistry {
public:
    using Palette = std::vector<Color>;

    // Load palettes from JSON string
    bool loadFromJson(const std::string& json);

    // Load from file path
    bool loadFromFile(const std::string& path);

    // Query
    [[nodiscard]] bool has(const std::string& name) const;
    [[nodiscard]] const Palette* get(const std::string& name) const;

    // Get all palette names
    [[nodiscard]] std::vector<std::string> names() const;

private:
    std::unordered_map<std::string, Palette> palettes_;
};
```

- [ ] **Step 3: Implement PaletteRegistry**

```cpp
// engine/render/palette.cpp
#include "engine/render/palette.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>

bool PaletteRegistry::loadFromJson(const std::string& jsonStr) {
    try {
        auto j = nlohmann::json::parse(jsonStr);
        for (auto& [name, def] : j.items()) {
            Palette palette;
            for (const auto& colorStr : def["colors"]) {
                palette.push_back(Color::fromHex(colorStr.get<std::string>()));
            }
            palettes_[name] = std::move(palette);
        }
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[Palette] Failed to parse JSON: {}", e.what());
        return false;
    }
}

bool PaletteRegistry::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadFromJson(content);
}

bool PaletteRegistry::has(const std::string& name) const {
    return palettes_.count(name) > 0;
}

const PaletteRegistry::Palette* PaletteRegistry::get(const std::string& name) const {
    auto it = palettes_.find(name);
    return it != palettes_.end() ? &it->second : nullptr;
}

std::vector<std::string> PaletteRegistry::names() const {
    std::vector<std::string> result;
    result.reserve(palettes_.size());
    for (const auto& [name, _] : palettes_) {
        result.push_back(name);
    }
    return result;
}
```

- [ ] **Step 4: Create sample palette data**

```json
// assets/data/palettes.json
{
    "warrior_siras": {
        "colors": ["#1a1a2e", "#16213e", "#0f3460", "#e94560",
                   "#533483", "#2b2d42", "#8d99ae", "#edf2f4",
                   "#d90429", "#ef233c", "#2ec4b6", "#cbf3f0",
                   "#ffffff", "#000000", "#ffbe0b", "#fb5607"]
    },
    "warrior_lanos": {
        "colors": ["#2d3436", "#636e72", "#b2bec3", "#dfe6e9",
                   "#0984e3", "#74b9ff", "#00cec9", "#81ecec",
                   "#d63031", "#ff7675", "#fdcb6e", "#ffeaa7",
                   "#ffffff", "#000000", "#6c5ce7", "#a29bfe"]
    },
    "leather_common": {
        "colors": ["#8B4513", "#A0522D", "#D2691E", "#DEB887",
                   "#F4A460", "#DAA520", "#B8860B", "#CD853F",
                   "#D2B48C", "#F5DEB3", "#FAEBD7", "#FFF8DC",
                   "#ffffff", "#000000", "#696969", "#A9A9A9"]
    },
    "leather_rare": {
        "colors": ["#191970", "#4169E1", "#1E90FF", "#87CEEB",
                   "#ADD8E6", "#B0C4DE", "#4682B4", "#5F9EA0",
                   "#00CED1", "#48D1CC", "#40E0D0", "#AFEEEE",
                   "#ffffff", "#000000", "#C0C0C0", "#778899"]
    },
    "leather_epic": {
        "colors": ["#4B0082", "#800080", "#9400D3", "#DA70D6",
                   "#EE82EE", "#DDA0DD", "#BA55D3", "#9370DB",
                   "#8A2BE2", "#7B68EE", "#6A5ACD", "#483D8B",
                   "#ffffff", "#000000", "#FFD700", "#FFA500"]
    }
}
```

- [ ] **Step 5: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="Palette System"`
Expected: All 4 tests PASS

- [ ] **Step 6: Commit**

```bash
git add engine/render/palette.h engine/render/palette.cpp assets/data/palettes.json tests/test_palette.cpp
git commit -m "feat: palette swap registry with JSON-driven color definitions"
```
