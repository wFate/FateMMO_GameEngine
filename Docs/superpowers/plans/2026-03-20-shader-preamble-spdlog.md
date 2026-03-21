# Shader Preamble Injection + spdlog Integration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make all shaders GL/GLES portable via compile-time preamble injection, and replace the custom Logger with spdlog for per-subsystem logging, async mode, and Android logcat support.

**Architecture:** Shader files drop their `#version` line; `Shader::loadFromFile()` prepends a platform preamble. The custom Logger singleton is replaced with spdlog wrapped behind the same `LOG_INFO(cat, fmt, ...)` macro interface — zero call-site changes across 51 files.

**Tech Stack:** C++20, spdlog (FetchContent), OpenGL 3.3 / GLES 3.0 GLSL, existing Shader class

**Build command:** `"C:/Program Files/Microsoft Visual Studio/2025/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build`

**Test command:** `./build/Debug/fate_tests.exe`

**IMPORTANT:** Before building, `touch` every edited `.cpp` file (CMake misses changes silently on this setup).

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `engine/render/shader.cpp` | Inject preamble in loadFromFile() and reloadFromFile() |
| Modify | `assets/shaders/sprite.vert` | Remove `#version 330 core` line |
| Modify | `assets/shaders/sprite.frag` | Remove `#version 330 core` line |
| Modify | `assets/shaders/fullscreen_quad.vert` | Remove `#version 330 core` line |
| Modify | `assets/shaders/blit.frag` | Remove `#version 330 core` line |
| Modify | `assets/shaders/grid.frag` | Remove `#version 330 core` line |
| Modify | `assets/shaders/light.frag` | Remove `#version 330 core` line |
| Modify | `assets/shaders/postprocess.frag` | Remove `#version 330 core` line |
| Modify | `assets/shaders/bloom_extract.frag` | Remove `#version 330 core` line |
| Modify | `assets/shaders/blur.frag` | Remove `#version 330 core` line |
| Modify | `CMakeLists.txt` | Add spdlog FetchContent |
| Rewrite | `engine/core/logger.h` | spdlog wrapper with same LOG_* macro interface |
| Modify | `engine/app.cpp` | Update Logger::instance().init() → spdlog init |
| Modify | `engine/editor/log_viewer.h` | Adapt to spdlog custom sink |

---

## Part A: Shader Preamble Injection

### Task 1: Add preamble injection to Shader::loadFromFile()

**Files:**
- Modify: `engine/render/shader.cpp:16-36` (loadFromFile) and `engine/render/shader.cpp:38-73` (reloadFromFile)

- [ ] **Step 1: Add preamble helper function**

At the top of `engine/render/shader.cpp`, after the includes and inside the `fate` namespace, add:

```cpp
namespace {
    const char* getShaderPreamble(bool isFragment) {
#ifdef FATEMMO_GLES
        if (isFragment) {
            return "#version 300 es\nprecision mediump float;\nprecision mediump sampler2D;\n";
        } else {
            return "#version 300 es\n";
        }
#else
        (void)isFragment;
        return "#version 330 core\n";
#endif
    }
} // anonymous namespace
```

- [ ] **Step 2: Inject preamble in loadFromFile()**

Replace the body of `loadFromFile()` (lines 16-36):

```cpp
bool Shader::loadFromFile(const std::string& vertPath, const std::string& fragPath) {
    std::ifstream vertFile(vertPath);
    std::ifstream fragFile(fragPath);

    if (!vertFile.is_open()) {
        LOG_ERROR("Shader", "Cannot open vertex shader: %s", vertPath.c_str());
        return false;
    }
    if (!fragFile.is_open()) {
        LOG_ERROR("Shader", "Cannot open fragment shader: %s", fragPath.c_str());
        return false;
    }

    std::stringstream vertStream, fragStream;
    vertStream << vertFile.rdbuf();
    fragStream << fragFile.rdbuf();

    // Prepend platform-specific preamble
    std::string vertSrc = std::string(getShaderPreamble(false)) + vertStream.str();
    std::string fragSrc = std::string(getShaderPreamble(true)) + fragStream.str();

    vertPath_ = vertPath;
    fragPath_ = fragPath;
    return loadFromSource(vertSrc, fragSrc);
}
```

- [ ] **Step 3: Inject preamble in reloadFromFile()**

In `reloadFromFile()`, after reading the file streams, add the same preamble injection before calling `loadFromSource()`. Replace lines 46-56:

```cpp
    std::stringstream vertStream, fragStream;
    vertStream << vertFile.rdbuf();
    fragStream << fragFile.rdbuf();

    // Prepend platform-specific preamble
    std::string vertSrc = std::string(getShaderPreamble(false)) + vertStream.str();
    std::string fragSrc = std::string(getShaderPreamble(true)) + fragStream.str();

    // Save old handle in case loadFromSource fails
    gfx::ShaderHandle oldHandle = gfxHandle_;
    unsigned int oldProgram = programId_;
    gfxHandle_ = {};
    programId_ = 0;

    if (!loadFromSource(vertSrc, fragSrc)) {
```

- [ ] **Step 4: Touch, build to verify compilation (shaders will fail — they now get double #version)**

This is expected to break because shader files still have `#version 330 core`. That's fine — next task strips them.

---

### Task 2: Strip #version lines from all shader files

**Files:** All 9 shader files in `assets/shaders/`

- [ ] **Step 1: Remove `#version 330 core` from all shader files**

Remove the first line (`#version 330 core`) from each of these files. If the line is followed by a blank line, remove both to keep formatting clean:

- `assets/shaders/sprite.vert` — remove line 1
- `assets/shaders/sprite.frag` — remove line 1
- `assets/shaders/fullscreen_quad.vert` — remove line 1 and the blank line 2
- `assets/shaders/blit.frag` — remove line 1 and the blank line 2
- `assets/shaders/grid.frag` — remove line 1 and the blank line 2
- `assets/shaders/light.frag` — remove line 1 and the blank line 2
- `assets/shaders/postprocess.frag` — remove line 1 and the blank line 2
- `assets/shaders/bloom_extract.frag` — remove line 1 and the blank line 2
- `assets/shaders/blur.frag` — remove line 1 and the blank line 2

- [ ] **Step 2: Touch, build, run the game briefly to verify shaders compile**

```bash
touch engine/render/shader.cpp
```
Build. Expected: compiles and shaders load correctly (preamble injects `#version 330 core` on desktop).

- [ ] **Step 3: Run full test suite**

`./build/Debug/fate_tests.exe`
Expected: All tests pass (371).

- [ ] **Step 4: Commit**

```bash
git add engine/render/shader.cpp assets/shaders/
git commit -m "feat: shader preamble injection for GL/GLES portability"
```

---

## Part B: spdlog Integration

### Task 3: Add spdlog to CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt` (FetchContent section)

- [ ] **Step 1: Add spdlog FetchContent**

In `CMakeLists.txt`, after the existing FetchContent declarations (after the nlohmann/json block, around line 44), add:

```cmake
# spdlog (fast, header-only-capable logging)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
    GIT_SHALLOW    TRUE
)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(spdlog)
```

- [ ] **Step 2: Link spdlog to the engine library**

Find the `target_link_libraries` for the engine library (fate_engine) in CMakeLists.txt. Add `spdlog::spdlog` to it. The implementer should search for `target_link_libraries(fate_engine` and add `spdlog::spdlog` to the list.

Also link it to `FateServer` target if it exists separately, and to `fate_tests`.

- [ ] **Step 3: Touch CMakeLists.txt, build to verify spdlog downloads and links**

Build. Expected: spdlog downloads, compiles, links. May have warnings — that's fine.

---

### Task 4: Rewrite logger.h as spdlog wrapper

**Files:**
- Rewrite: `engine/core/logger.h`

- [ ] **Step 1: Replace logger.h entirely**

Replace the entire contents of `engine/core/logger.h` with:

```cpp
#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/callback_sink.h>
#include <string>
#include <memory>
#include <mutex>
#include <functional>

namespace fate {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

class Logger {
public:
    static Logger& instance() {
        static Logger s_instance;
        return s_instance;
    }

    void init(const std::string& logFilePath = "fate_engine.log") {
        if (initialized_) return;

        // Create sinks
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logFilePath, 5 * 1024 * 1024, 3); // 5MB, 3 rotated files

        callbackSink_ = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [this](const spdlog::details::log_msg& msg) {
                if (logCallback_) {
                    std::string formatted;
                    // Format: [HH:MM:SS.mmm] [LEVEL] [category] message
                    auto tm = fmt::localtime(std::chrono::system_clock::to_time_t(msg.time));
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        msg.time.time_since_epoch()) % 1000;
                    formatted = fmt::format("[{:%H:%M:%S}.{:03d}] [{}] {}\n",
                        tm, ms.count(), spdlog::level::to_string_view(msg.level),
                        std::string_view(msg.payload.data(), msg.payload.size()));
                    logCallback_(formatted, static_cast<int>(msg.level));
                }
            });

        spdlog::sinks_init_list sinks = {consoleSink, fileSink, callbackSink_};
        defaultLogger_ = std::make_shared<spdlog::logger>("fate", sinks);
        defaultLogger_->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
        defaultLogger_->set_level(spdlog::level::debug);
        defaultLogger_->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(defaultLogger_);

        // Enable backtrace ring buffer (64 entries for crash breadcrumbs)
        spdlog::enable_backtrace(64);

        initialized_ = true;
    }

    void shutdown() {
        spdlog::shutdown();
        initialized_ = false;
    }

    void setMinLevel(LogLevel level) {
        spdlog::level::level_enum spdLevel;
        switch (level) {
            case LogLevel::Debug: spdLevel = spdlog::level::debug; break;
            case LogLevel::Info:  spdLevel = spdlog::level::info; break;
            case LogLevel::Warn:  spdLevel = spdlog::level::warn; break;
            case LogLevel::Error: spdLevel = spdlog::level::err; break;
            case LogLevel::Fatal: spdLevel = spdlog::level::critical; break;
            default: spdLevel = spdlog::level::debug; break;
        }
        spdlog::set_level(spdLevel);
    }

    void log(LogLevel level, const char* category, const char* fmt, ...) {
        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        // Get or create per-category logger (shares sinks with default)
        auto logger = spdlog::get(category);
        if (!logger) {
            if (defaultLogger_) {
                logger = std::make_shared<spdlog::logger>(category, defaultLogger_->sinks().begin(), defaultLogger_->sinks().end());
                logger->set_level(defaultLogger_->level());
                logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
                spdlog::register_logger(logger);
            } else {
                // Fallback before init
                fprintf(stderr, "[%s] %s\n", category, buffer);
                return;
            }
        }

        switch (level) {
            case LogLevel::Debug: logger->debug("{}", buffer); break;
            case LogLevel::Info:  logger->info("{}", buffer); break;
            case LogLevel::Warn:  logger->warn("{}", buffer); break;
            case LogLevel::Error: logger->error("{}", buffer); break;
            case LogLevel::Fatal: logger->critical("{}", buffer); break;
        }
    }

    // Set callback for log viewer integration (same interface as before)
    using LogCallback = std::function<void(const std::string&, int)>;
    void setLogCallback(LogCallback cb) { logCallback_ = std::move(cb); }

private:
    Logger() = default;
    bool initialized_ = false;
    std::shared_ptr<spdlog::logger> defaultLogger_;
    std::shared_ptr<spdlog::sinks::callback_sink_mt> callbackSink_;
    LogCallback logCallback_;
};

// Same macros as before — zero call-site changes
#define LOG_DEBUG(cat, ...) fate::Logger::instance().log(fate::LogLevel::Debug, cat, __VA_ARGS__)
#define LOG_INFO(cat, ...)  fate::Logger::instance().log(fate::LogLevel::Info,  cat, __VA_ARGS__)
#define LOG_WARN(cat, ...)  fate::Logger::instance().log(fate::LogLevel::Warn,  cat, __VA_ARGS__)
#define LOG_ERROR(cat, ...) fate::Logger::instance().log(fate::LogLevel::Error, cat, __VA_ARGS__)
#define LOG_FATAL(cat, ...) fate::Logger::instance().log(fate::LogLevel::Fatal, cat, __VA_ARGS__)

} // namespace fate
```

Key design decisions:
- **Same `LOG_INFO(cat, fmt, ...)` macro interface** — all 51 calling files unchanged
- **Per-category loggers** created on first use, sharing sinks with the default logger
- **Callback sink** preserves the `LogViewer` integration (same `setLogCallback` interface)
- **Rotating file sink** — 5MB max, 3 rotated files (replaces the truncating ofstream)
- **Backtrace ring buffer** — 64 entries for crash breadcrumbs
- **No async mode yet** — synchronous is safer for initial migration; can add later

- [ ] **Step 2: Verify app.cpp init/shutdown still compiles**

The existing calls in `engine/app.cpp` are:
```cpp
Logger::instance().init("fate_engine.log");  // line 26
Logger::instance().setLogCallback(...);       // line 96
Logger::instance().shutdown();                // line 464
```

These all match the new interface exactly — no changes needed in `app.cpp`.

- [ ] **Step 3: Touch, build**

```bash
touch engine/core/logger.h engine/app.cpp
```
Build. Expected: compiles. All LOG_* macros resolve to the new spdlog wrapper.

- [ ] **Step 4: Run full test suite**

`./build/Debug/fate_tests.exe`
Expected: All tests pass (371). Log output now comes through spdlog (colored in terminal, timestamped, category-tagged).

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt engine/core/logger.h
git commit -m "feat: replace custom Logger with spdlog for per-subsystem logging and mobile support"
```

---

### Task 5: Final regression check

- [ ] **Step 1: Touch all modified files and rebuild**

```bash
touch engine/render/shader.cpp engine/core/logger.h engine/app.cpp
```
Build.

- [ ] **Step 2: Run full test suite**

`./build/Debug/fate_tests.exe`
Expected: All 371 tests pass.

- [ ] **Step 3: Verify log output format**

Run the game or tests and confirm log lines look like:
```
[15:30:45.123] [info] [App] === FateEngine v0.1.0 starting ===
[15:30:45.125] [info] [Shader] Reloaded shader program 3
```

Each category now has its own named spdlog logger visible in the output.
