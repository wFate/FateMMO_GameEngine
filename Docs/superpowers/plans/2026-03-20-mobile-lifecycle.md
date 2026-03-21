# SDL2 Mobile Lifecycle Handling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Handle SDL2 mobile lifecycle events (background/foreground/low-memory) so the game saves state, pauses cleanly, and resumes without data loss on iOS/Android.

**Architecture:** An event filter registered via `SDL_SetEventFilter()` catches lifecycle events before the main loop. The `App` class gets new virtual callbacks (`onEnterBackground`, `onEnterForeground`, `onLowMemory`) that game code can override. The base App implementation pauses the game loop, flushes network, and adjusts texture cache on low memory. Platform defines (`FATEMMO_MOBILE`, `FATEMMO_PLATFORM_IOS`) gate mobile-specific behavior.

**Tech Stack:** C++20, SDL2 lifecycle events, existing App/Input/Camera classes

**Build command:** `"C:/Program Files/Microsoft Visual Studio/2025/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build`

**Test command:** `./build/Debug/fate_tests.exe`

**IMPORTANT:** Before building, `touch` every edited `.cpp` file (CMake misses changes silently on this setup).

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `CMakeLists.txt` | Add platform defines (FATEMMO_MOBILE, FATEMMO_PLATFORM_IOS, etc.) |
| Modify | `engine/app.h` | Add lifecycle state, virtual callbacks, event filter |
| Modify | `engine/app.cpp` | Register event filter, implement lifecycle transitions, pause/resume |
| Create | `tests/test_lifecycle.cpp` | Unit tests for lifecycle state machine |

---

### Task 1: Add platform defines to CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add platform detection block**

In `CMakeLists.txt`, after the C++ standard settings (around line 13) and before the FetchContent section, add:

```cmake
# =============================================================================
# Platform Detection
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_compile_definitions(FATEMMO_PLATFORM_IOS FATEMMO_MOBILE)
elseif(ANDROID)
    add_compile_definitions(FATEMMO_PLATFORM_ANDROID FATEMMO_MOBILE)
elseif(WIN32)
    add_compile_definitions(FATEMMO_PLATFORM_WINDOWS)
elseif(APPLE)
    add_compile_definitions(FATEMMO_PLATFORM_MACOS)
elseif(UNIX)
    add_compile_definitions(FATEMMO_PLATFORM_LINUX)
endif()
```

- [ ] **Step 2: Touch, build, verify**

```bash
touch CMakeLists.txt
```
Build. Expected: compiles with `FATEMMO_PLATFORM_WINDOWS` defined on Windows.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add platform detection defines for mobile/desktop"
```

---

### Task 2: Add lifecycle state and callbacks to App

**Files:**
- Modify: `engine/app.h`
- Modify: `engine/app.cpp`

- [ ] **Step 1: Add lifecycle enum and state to app.h**

In `engine/app.h`, after the `AppConfig` struct and before the `App` class, add:

```cpp
enum class AppLifecycleState {
    Active,         // Game is running normally
    Background,     // App is backgrounded (mobile) or minimized (desktop)
    Suspended       // App is about to be terminated (iOS only)
};
```

In the `App` class public section, after the existing virtual callbacks (`onShutdown`), add:

```cpp
    // Mobile lifecycle callbacks — override in game for custom behavior
    virtual void onEnterBackground() {}   // Save state, pause network
    virtual void onEnterForeground() {}   // Resume, reconnect
    virtual void onLowMemory() {}         // Flush caches

    // Lifecycle state
    AppLifecycleState lifecycleState() const { return lifecycleState_; }
    bool isBackgrounded() const { return lifecycleState_ != AppLifecycleState::Active; }
```

In the private section, add after `std::string assetsDir_`:

```cpp
    AppLifecycleState lifecycleState_ = AppLifecycleState::Active;

    // SDL event filter for lifecycle events (fires before main loop processes them)
    static int SDLCALL lifecycleEventFilter(void* userdata, SDL_Event* event);
    void handleLifecycleEvent(const SDL_Event& event);
```

- [ ] **Step 2: Implement lifecycle event filter in app.cpp**

In `engine/app.cpp`, add the static event filter and handler. Place after the `shutdown()` function or at the end of the file:

```cpp
int SDLCALL App::lifecycleEventFilter(void* userdata, SDL_Event* event) {
    App* app = static_cast<App*>(userdata);
    if (!app) return 1;

    switch (event->type) {
        case SDL_APP_WILLENTERBACKGROUND:
            LOG_INFO("App", "Lifecycle: entering background");
            app->handleLifecycleEvent(*event);
            return 0; // consumed

        case SDL_APP_DIDENTERBACKGROUND:
            LOG_INFO("App", "Lifecycle: entered background");
            return 0;

        case SDL_APP_WILLENTERFOREGROUND:
            LOG_INFO("App", "Lifecycle: entering foreground");
            return 0;

        case SDL_APP_DIDENTERFOREGROUND:
            LOG_INFO("App", "Lifecycle: entered foreground");
            app->handleLifecycleEvent(*event);
            return 0;

        case SDL_APP_LOWMEMORY:
            LOG_WARN("App", "Lifecycle: low memory warning");
            app->handleLifecycleEvent(*event);
            return 0;

        default:
            return 1; // pass through to normal event processing
    }
}

void App::handleLifecycleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_APP_WILLENTERBACKGROUND:
            lifecycleState_ = AppLifecycleState::Background;
            onEnterBackground();
            break;

        case SDL_APP_DIDENTERFOREGROUND:
            lifecycleState_ = AppLifecycleState::Active;
            onEnterForeground();
            break;

        case SDL_APP_LOWMEMORY:
            onLowMemory();
            break;
    }
}
```

- [ ] **Step 3: Register the event filter in init()**

In `App::init()`, add right after `SDL_Init()` succeeds (after line 35, before the GL attribute setup):

```cpp
    // Register lifecycle event filter — catches mobile background/foreground
    // events before the main event loop, critical for iOS (5-second deadline)
    SDL_SetEventFilter(lifecycleEventFilter, this);
```

- [ ] **Step 4: Skip game update when backgrounded**

In `App::run()`, add a check at the top of the while loop body. After `deltaTime_` clamping (after line 148):

```cpp
        // Skip update/render when backgrounded (mobile power saving)
        if (lifecycleState_ != AppLifecycleState::Active) {
            SDL_Delay(100); // sleep to avoid spinning
            continue;
        }
```

- [ ] **Step 5: Also handle lifecycle events in processEvents()**

In `App::processEvents()`, add cases to the main switch statement (after the SDL_WINDOWEVENT case, around line 204):

```cpp
            case SDL_APP_WILLENTERBACKGROUND:
            case SDL_APP_DIDENTERBACKGROUND:
            case SDL_APP_WILLENTERFOREGROUND:
            case SDL_APP_DIDENTERFOREGROUND:
            case SDL_APP_LOWMEMORY:
                // Handled by event filter, but process here too for desktop simulation
                handleLifecycleEvent(event);
                break;
```

- [ ] **Step 6: Touch, build, verify**

```bash
touch engine/app.h engine/app.cpp
```
Build. Expected: compiles cleanly. On desktop, lifecycle state stays `Active`.

- [ ] **Step 7: Commit**

```bash
git add engine/app.h engine/app.cpp
git commit -m "feat: SDL2 mobile lifecycle handling with background/foreground/low-memory callbacks"
```

---

### Task 3: Add lifecycle unit tests

**Files:**
- Create: `tests/test_lifecycle.cpp`

- [ ] **Step 1: Write lifecycle state tests**

Create `tests/test_lifecycle.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/app.h"

// Minimal testable App subclass
class TestApp : public fate::App {
public:
    int bgCount = 0;
    int fgCount = 0;
    int lowMemCount = 0;

    void onEnterBackground() override { bgCount++; }
    void onEnterForeground() override { fgCount++; }
    void onLowMemory() override { lowMemCount++; }
};

TEST_CASE("App lifecycle state starts Active") {
    TestApp app;
    CHECK(app.lifecycleState() == fate::AppLifecycleState::Active);
    CHECK_FALSE(app.isBackgrounded());
}

TEST_CASE("App handleLifecycleEvent transitions to Background") {
    TestApp app;
    SDL_Event e{};
    e.type = SDL_APP_WILLENTERBACKGROUND;
    app.handleLifecycleEvent(e);

    CHECK(app.lifecycleState() == fate::AppLifecycleState::Background);
    CHECK(app.isBackgrounded());
    CHECK(app.bgCount == 1);
}

TEST_CASE("App handleLifecycleEvent transitions back to Active") {
    TestApp app;

    // Go background
    SDL_Event bg{};
    bg.type = SDL_APP_WILLENTERBACKGROUND;
    app.handleLifecycleEvent(bg);
    CHECK(app.isBackgrounded());

    // Come foreground
    SDL_Event fg{};
    fg.type = SDL_APP_DIDENTERFOREGROUND;
    app.handleLifecycleEvent(fg);
    CHECK_FALSE(app.isBackgrounded());
    CHECK(app.lifecycleState() == fate::AppLifecycleState::Active);
    CHECK(app.fgCount == 1);
}

TEST_CASE("App low memory callback fires") {
    TestApp app;
    SDL_Event e{};
    e.type = SDL_APP_LOWMEMORY;
    app.handleLifecycleEvent(e);

    CHECK(app.lowMemCount == 1);
    // Low memory doesn't change lifecycle state
    CHECK(app.lifecycleState() == fate::AppLifecycleState::Active);
}
```

NOTE: The `handleLifecycleEvent` method needs to be public (or the test class needs friend access). Check that the method is accessible. If it's private, the implementer should either make it `protected` (so TestApp inherits access) or add a public `simulateLifecycleEvent()` test helper. The simplest fix: make `handleLifecycleEvent` public since it's just a dispatcher.

- [ ] **Step 2: Touch, build, run tests**

```bash
touch engine/app.h engine/app.cpp tests/test_lifecycle.cpp
```
Build, then: `./build/Debug/fate_tests.exe -tc="App lifecycle*"`
Expected: All 4 tests pass.

- [ ] **Step 3: Run full test suite**

`./build/Debug/fate_tests.exe`
Expected: All tests pass (376 + 4 new = 380).

- [ ] **Step 4: Commit**

```bash
git add tests/test_lifecycle.cpp engine/app.h
git commit -m "test: add lifecycle state machine unit tests"
```
