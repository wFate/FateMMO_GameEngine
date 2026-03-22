# Taking FateMMO from prototype to production across every platform

**FateMMO's custom C++ engine already solves the hard server-authoritative problems — but shipping to iOS, Android, and PC as a polished product requires roughly 20 distinct engineering workstreams that touch every layer of the stack.** The path from desktop prototype to production-grade cross-platform engine centers on three pillars: mobile deployment (the largest single block of work at ~12–16 weeks), PC release packaging (~3–4 weeks), and engine infrastructure polish (~8–12 weeks in parallel). The good news: SDL2 natively supports iOS and Android, OpenGL ES 3.0 is a near-subset of OpenGL 3.3 for a 2D sprite batcher, and the 480×270 native resolution makes the entire mobile scaling story trivial. The total effort for a focused solo developer is **6–9 months of dedicated work** before the first cross-platform beta.

---

## Part I — Mobile deployment: SDL2 already does the heavy lifting

### SDL2's mobile backends abstract the platform nightmares

SDL2 provides native iOS and Android backends that handle window creation, GL context management, input, and application lifecycle. On **iOS**, SDL2 creates a `UIWindow` with a `UIViewController` containing an `SDL_uikitview` (a `UIView` subclass) that hosts either an OpenGL ES or Metal rendering surface. The `SDL_main` function is invoked from SDL's own `UIApplicationDelegate`, meaning the standard C/C++ `main()` entry point works transparently. On **Android**, SDL2 ships a Java class `SDLActivity` that extends `Activity`, creates a `SurfaceView` for GL rendering, and bridges to native code via JNI. Your game compiles into `libmain.so`, which `SDLActivity` loads at startup.

The critical mobile-specific behavior lives in **lifecycle events**. SDL2 maps platform lifecycle callbacks to six events that the engine must handle:

| SDL Event | iOS Trigger | Android Trigger | Required Engine Response |
|-----------|-------------|-----------------|------------------------|
| `SDL_APP_WILLENTERBACKGROUND` | `applicationWillResignActive` | `onPause()` | Save state, pause game loop, flush network |
| `SDL_APP_DIDENTERBACKGROUND` | `applicationDidEnterBackground` | `onStop()` | Release GL resources if needed, stop audio |
| `SDL_APP_WILLENTERFOREGROUND` | `applicationWillEnterForeground` | `onRestart()` | Prepare to restore GL context |
| `SDL_APP_DIDENTERFOREGROUND` | `applicationDidBecomeActive` | `onResume()` | Recreate GL resources, resume audio, reconnect |
| `SDL_APP_LOWMEMORY` | `didReceiveMemoryWarning` | `onTrimMemory()` | Flush texture LRU, release audio buffers |
| `SDL_QUIT` | App terminated | Process killed | N/A (already dead) |

**Register these via `SDL_SetEventFilter()` before the main loop** — not in the event poll — because some events fire before the loop starts. The engine's existing texture cache LRU with VRAM budget is perfectly positioned to handle `SDL_APP_LOWMEMORY`: drop the budget by 50%, evict, then restore on foreground.

The CMake configuration for cross-compilation uses platform-specific toolchain files. For iOS:

```cmake
cmake -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_CXX_STANDARD=23 \
  -S . -B build-ios
```

For Android, the NDK provides its own toolchain file, integrated through Gradle's `externalNativeBuild` block. The engine's `CMakeLists.txt` uses platform detection to conditionally compile:

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(engine PUBLIC FATEMMO_PLATFORM_IOS FATEMMO_MOBILE)
elseif(ANDROID)
    target_compile_definitions(engine PUBLIC FATEMMO_PLATFORM_ANDROID FATEMMO_MOBILE)
elseif(WIN32)
    target_compile_definitions(engine PUBLIC FATEMMO_PLATFORM_WINDOWS)
endif()
```

**Priority: Ship-blocking. Effort: 2–3 weeks** for initial SDL2 mobile lifecycle integration and CMake toolchain setup.

### OpenGL 3.3 to ES 3.0 requires only surgical changes for a 2D batcher

The migration from desktop GL 3.3 to mobile GLES 3.0 is **far simpler than it appears** for a 2D sprite-batch renderer that uses no geometry shaders, no compute, and no tessellation. GLES 3.0 is roughly equivalent to a subset of desktop GL 3.3, and both use the modern `in`/`out` qualifier syntax in shaders. The actual changes fall into four categories.

**Shader version directives** must change from `#version 330 core` to `#version 300 es`. GLES 3.0 mandates `precision mediump float;` in fragment shaders, while desktop GL ignores precision qualifiers but accepts them silently. The solution is a shared shader preamble injected at load time:

```glsl
// Injected by engine based on platform
#ifdef FATEMMO_GLES
    #version 300 es
    precision mediump float;
    precision mediump sampler2D;
#else
    #version 330 core
#endif
```

**Fragment output** differs subtly: desktop GL uses `glBindFragDataLocation` to map outputs to color attachments, while GLES 3.0 uses `layout(location = 0) out vec4 fragColor;` — but this `layout` syntax also works on desktop GL 3.3+, so adopt it universally.

**Texture formats** are the main divergence. Desktop uses S3TC/BC compression; mobile mandates **ETC2** (guaranteed on all GLES 3.0 devices) and **ASTC** (available on ~80% of active Android devices, all iOS A8+ devices). For a 2D pixel art game using small spritesheets, **uncompressed RGBA8 works fine on mobile** — the 480×270 render target means texture bandwidth is minimal. If optimization becomes necessary, ASTC 4×4 at **8 bits/pixel** provides the best quality-to-size ratio.

**Buffer mapping** requires attention: `glMapBuffer` does not exist in GLES 3.0; use `glMapBufferRange` instead (available in both GL 3.3 and GLES 3.0). The engine's SpriteBatch likely already uses `glMapBufferRange` or `glBufferSubData` for dynamic vertex updates — verify and standardize on `glMapBufferRange` with `GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT`.

**The custom GL loader** needs adaptation. On mobile, GL function pointers come from `eglGetProcAddress` (Android) or are linked directly (iOS links GLES framework statically). The simplest approach: on desktop, keep the custom loader; on mobile, `#include <GLES3/gl3.h>` and link against the platform's GLES library. Wrap this in a `gl_platform.h` header that resolves per-platform.

One important caveat: **OpenGL ES is deprecated on iOS since iOS 12.** Apple still supports it as of 2026, but the writing is on the wall. For future-proofing, consider **ANGLE** — Google's OpenGL ES-to-Metal/Vulkan translation layer — which would let the engine's GLES 3.0 code run on Metal underneath. ANGLE is used by Chrome, Flutter, and numerous production apps. However, for an MVP mobile port, native GLES 3.0 on iOS works today and is the fastest path.

**Priority: Ship-blocking. Effort: 1–2 weeks** for shader abstraction, GL loader adaptation, and validation across platforms.

### Touch controls need an input abstraction layer over SDL2 finger events

SDL2 exposes touch input through `SDL_FINGERDOWN`, `SDL_FINGERUP`, and `SDL_FINGERMOTION` events, each carrying a `fingerId`, normalized coordinates (0.0–1.0), and pressure. Multi-touch is inherently supported — each concurrent finger gets a unique `fingerId`.

For a top-down 2D MMORPG, the standard mobile control scheme consists of three elements: a **dpad** for movement on the left side of the screen, **action buttons** (attack, interact, skill slots) on the right side, and **tap-to-target** on the game world. Games like Tree of World Mobile (TWOM), Ragnarok M, and MapleStory M all follow this pattern.

The architecture centers on an **input action system** that decouples physical input from game logic:

```cpp
enum class InputAction { MoveX, MoveY, Attack, Interact, OpenInventory, TargetEntity };

class InputManager {
public:
    float getAxis(InputAction axis) const;    // -1.0 to 1.0 for movement
    bool isPressed(InputAction action) const;
    bool wasJustPressed(InputAction action) const;
    EntityID getTargetedEntity() const;

    void updateFromKeyboard(const SDL_Event& e);  // Desktop
    void updateFromTouch(const SDL_Event& e);      // Mobile
    void updateFromController(const SDL_Event& e); // Gamepad
};
```

Desktop keyboard maps W/A/S/D to MoveX/MoveY axes; mobile touch maps a dpad to same a



For control layout across screen sizes, define positions **relative to screen edges with safe-area offsets**. SDL2 does not expose iOS safe area insets natively (SDL3 adds `SDL_GetWindowSafeArea()`), so on SDL2 you need an Objective-C++ bridge to read `UIView.safeAreaInsets`. On Android, `WindowInsets` provides equivalent data via JNI. Anchor action buttons to the bottom-right corner inset by the safe area, and the joystick zone to the bottom-left.

**Priority: Ship-blocking. Effort: 2–3 weeks** for input abstraction, virtual controls rendering and tuning, and per-device layout.

### iOS builds: from CMake to TestFlight in one script

The iOS build pipeline compiles C++ via CMake's Xcode generator, produces an `.xcarchive`, exports an IPA, and uploads to TestFlight. The full flow:

**CMake generates the Xcode project** with code signing properties set via `XCODE_ATTRIBUTE_*`:

```cmake
set_target_properties(FateMMO PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.fatemmo.game"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "YOUR_TEAM_ID"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
)
```

**Info.plist** must include landscape-only orientation lock (`UISupportedInterfaceOrientations` with only `UIInterfaceOrientationLandscapeLeft` and `Right`), `UIStatusBarHidden = true`, `UIRequiresFullScreen = true`, `UILaunchStoryboardName = LaunchScreen`, and `ITSAppUsesNonExemptEncryption = false` (unless your AEAD encryption triggers export compliance — game encryption for data protection is generally exempt, but check Apple's classification).

**App Store requirements for an MMO with chat** include: age rating of at minimum **12+** (for unrestricted web access/chat), a privacy policy URL in App Store Connect, content moderation for user-generated content (chat), and `NSAppTransportSecurity` exceptions if connecting to non-HTTPS endpoints.

**C++23 requires Xcode 16+** (Apple Clang based on LLVM 17). Xcode 16.3 adds broader C++23 library support including `std::expected` and `std::print`.

**Assets** are bundled using CMake's `set_source_files_properties(... PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/...")` to place files in the .app bundle, then accessed at runtime via `SDL_GetBasePath()`.

A **one-click deploy script** chains these steps: `cmake -G Xcode` → `cmake --build` → `xcodebuild archive` → `xcodebuild -exportArchive` → `xcrun altool --upload-app`. The entire flow from source to TestFlight runs in about 5 minutes.

**Priority: Ship-blocking. Effort: 2–3 weeks** for initial pipeline, signing, and first TestFlight build.

### Android builds: Gradle wraps CMake with SDLActivity as the bridge

Android's build pipeline uses **Gradle** as the outer build system, which invokes **CMake via the NDK toolchain** to compile native code. SDL2's `android-project/` template provides the scaffolding:

The project structure places SDL2 Java sources (`SDLActivity.java`, `SDLSurface.java`) in `app/src/main/java/org/libsdl/app/` and your CMakeLists.txt under `app/src/main/jni/`. Gradle's `build.gradle.kts` wires it together:

```kotlin
android {
    compileSdk = 35
    ndkVersion = "27.1.12297006"  // NDK r27 for C++23
    defaultConfig {
        minSdk = 24
        targetSdk = 35
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a") }
        externalNativeBuild {
            cmake { cppFlags += "-std=c++23" }
        }
    }
    externalNativeBuild {
        cmake { path = file("src/main/jni/CMakeLists.txt") }
    }
}
```

**NDK r27** (LLVM 18) is the recommended minimum for C++23 support. Set `ANDROID_PLATFORM` to at minimum **API 24** (Android 7.0) — GLES 3.0 is available from API 18, but API 24 is the practical floor for modern toolchain support and covers **98%+ of active devices**. NDK r27 also requires `ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` for devices shipping with 16KB page alignment (some Android 15 devices).

**GPU fragmentation** is Android's worst headache. Qualcomm Adreno GPUs have historically the worst spec compliance — shader compilation bugs, floating-point precision issues, and `for`-loop handling problems. ARM Mali drivers on some devices (notably Pixel 6 with Mali r46) have known crash-inducing bugs. PowerVR is generally most compliant but has declining market share. Practical defenses: keep shaders simple (critical for a 2D batcher — yours are already simple), use `mediump`/`highp` precision explicitly, avoid preprocessor macros in shaders, keep loop bounds as compile-time constants, and query `GL_RENDERER` at startup to maintain a workaround database.

**Google Play requires AAB** (Android App Bundle) format — APK is only for sideloading/ADB testing. Build with `./gradlew bundleRelease`. For games exceeding **150MB**, use **Play Asset Delivery (PAD)** with install-time, fast-follow, or on-demand delivery modes. PAD also supports **Texture Compression Format Targeting** — ship both ASTC and ETC2 variants, and Google Play delivers the correct format per device GPU.

**One-click deployment** for testing: `./gradlew installDebug` builds, signs with debug key, and installs via ADB in one command.

**Priority: Ship-blocking. Effort: 3–4 weeks** (more than iOS due to fragmentation testing).

### Screen scaling at 480×270 is perfectly suited for mobile

The engine's **480×270 native resolution** (exactly 16:9) makes the scaling story remarkably clean. The approach: render the entire game world to a **480×270 FBO**, then blit that FBO to the screen backbuffer with **nearest-neighbor filtering** for crisp pixel art.

For **16:9 displays** (most phones): 480×270 scales to an exact integer multiple — **4× = 1920×1080**, **6× = 2880×1620**, **8× = 3840×2160**. Use the largest integer scale that fits, then center with black letterboxing if there's leftover space.

For **wider displays** (19.5:9, 20:9 modern phones): two options exist. **Letterboxing** renders the 480×270 at integer scale with vertical pillarboxing — simple but wastes ~15% of screen width. **Viewport extension** renders a wider FBO (e.g., 533×270 for 19.77:9) so players see more of the game world horizontally — better UX but requires the game world and camera to handle variable widths. For an MMO where seeing more of the world is advantageous, **viewport extension is strongly recommended**: compute the FBO width as `ceil(270 * screenAspect)`, keeping height fixed at 270.

For **narrower displays** (4:3 tablets): extend vertically: FBO becomes 480×360, showing more world vertically. Again, keep the canonical dimension (width) fixed and adjust the other.

**UI elements** (chat box, inventory, virtual controls, health bars) must render at **screen resolution, not game resolution**, to remain crisp and readable. Use a two-layer approach: render game world to the low-res FBO, upscale it, then overlay UI rendered directly to the backbuffer at native resolution. This is the standard approach used by Celeste, Shovel Knight, and virtually all modern pixel art games.

SDL2's high-DPI support matters here: use `SDL_WINDOW_ALLOW_HIGHDPI` and call `SDL_GL_GetDrawableSize()` (not `SDL_GetWindowSize()`) to get the actual pixel dimensions for the backbuffer blit. On Retina iOS devices, drawable size is 2–3× the window size.

**Priority: Ship-blocking. Effort: 1 week** — the FBO pipeline is likely already in place; add aspect-ratio logic and UI layer separation.

---

## Part II — PC production builds and live-service infrastructure

### Release builds: LTO, symbol stripping, and platform installers

A production release build configuration in CMake centers on **Link-Time Optimization** and **debug symbol separation**:

```cmake
# LTO — typically 5-15% perf improvement, significant link-time increase
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Static linking for self-contained distribution
set(BUILD_SHARED_LIBS OFF)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# RPATH for Linux/macOS
if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN")
endif()
```

**Symbol stripping** preserves debug symbols in companion files for crash symbolication while shipping a small binary. On Linux: `objcopy --only-keep-debug binary binary.debug` then `strip binary`. On macOS: `dsymutil` generates `.dSYM` bundles. Windows `.pdb` files are naturally separate.

**Profile-Guided Optimization** (PGO) adds a two-pass build: instrument with `-fprofile-generate`, run the game through representative gameplay, then rebuild with `-fprofile-use`. This yields **10–20% additional performance** in hot paths. For a solo dev, PGO is high-effort but worth enabling for major releases.

For **Windows distribution**, use CPack's NSIS generator or WiX for MSI installers. For **macOS**, CPack's DragNDrop generator creates DMGs, followed by `codesign --options runtime` and `xcrun notarytool submit --wait` for notarization (required for Gatekeeper on macOS). For **Linux**, `linuxdeploy` creates AppImages — a single portable binary. Use `-static-libgcc -static-libstdc++` on Linux to avoid glibc version conflicts.

**Priority: Pre-launch. Effort: 2–3 weeks** across all three desktop platforms.

### Crash reporting via Sentry gives a solo dev a professional safety net

**Sentry Native SDK** (sentry-native) is the recommended crash reporting solution. It bundles Crashpad as the crash capture backend, generates minidumps, uploads them to Sentry's dashboard, and provides issue grouping, release tracking, and crash-free session rates — all on a **free tier of 5K errors/month**.

Integration is straightforward via FetchContent:

```cmake
FetchContent_Declare(sentry
    GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
    GIT_TAG 0.7.17)
set(SENTRY_BACKEND "crashpad" CACHE STRING "" FORCE)
FetchContent_MakeAvailable(sentry)
target_link_libraries(FateMMO PRIVATE sentry::sentry)
```

Initialize at startup with `sentry_init()`, passing the DSN, release version, and environment. **Ship `crashpad_handler` alongside the game executable** — it's a separate process that captures crashes even when the main process is completely corrupted. After every release build, upload debug symbols via `sentry-cli debug-files upload` in CI.

For **lightweight telemetry** beyond crashes, collect FPS P50/P95/P99, session length, zone load times, peak memory, GPU vendor, and OS version. Send this via a simple HTTPS POST to a **Cloudflare Worker** writing to **D1** (Cloudflare's SQLite-at-the-edge). Cost: effectively **$0** at indie scale (Workers free tier = 100K requests/day, D1 free tier = 5GB storage). Alternatively, **PostHog** offers **1 million free events/month** with dashboards included.

**Priority: Pre-launch. Effort: 1 week** for Sentry integration, 1 week for telemetry endpoint.

### Auto-update uses version manifests and delta patches over Cloudflare R2

For PC distribution outside Steam, the auto-updater checks a **JSON manifest** listing every file with SHA-256 checksums and sizes, compares against local files, downloads only changed files or delta patches, verifies integrity, and swaps atomically on restart.

**Delta patching** with **xdelta3** (Apache-2 licensed, VCDIFF format) reduces patch sizes by **80–95%** compared to full downloads. Generate patches in CI: `xdelta3 -e -s old_archive new_archive patch.xdelta`. Apply client-side: `xdelta3 -d -s local_archive patch.xdelta new_archive`. Alternative: **HDiffPatch** (MIT licensed) handles directory-level diffing with lower memory usage.

Host files on **Cloudflare R2** — S3-compatible object storage with **zero egress fees**. At $0.015/GB-month for storage, a 500MB game with 5 historical versions costs ~$0.04/month. Even at 10,000 players downloading 50MB patches monthly, egress is completely free. This is an order of magnitude cheaper than S3 or any traditional CDN.

The update flow runs as a separate process (so it can replace the game executable): check manifest → download delta → verify SHA-256 → apply to temp directory → restart → atomic swap → verify launch → clean up old version. **Sign the manifest with Ed25519** to prevent MITM tampering.

Mobile updates go through their respective app stores — no custom auto-update needed or permitted.

**Priority: Pre-launch (PC only). Effort: 2–3 weeks** for manifest generation, delta patching, and updater UI.

---

## Part III — Engine infrastructure that separates toys from tools

### PhysicsFS provides a battle-tested virtual filesystem with mod support

For asset packaging, **PhysicsFS** (zlib license) is the clear winner for a solo developer. It provides a Unix-like mount/overlay VFS that reads from directories during development and ZIP/7Z archives in release builds. Mount `base.pak` at `/`, then mount `patch_v2.pak` at `/` — files in the patch archive automatically override base files. This overlay system directly enables hot-patching individual assets and future mod support.

```cmake
FetchContent_Declare(physfs
    GIT_REPOSITORY https://github.com/icculus/physfs.git
    GIT_TAG main  GIT_SHALLOW TRUE)
set(PHYSFS_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(physfs)
target_link_libraries(engine PRIVATE physfs-static)
```

Wrap PhysicsFS in a C++ abstraction using `std::expected<T, VFSError>` for error handling. Replace all raw `fopen`/`ifstream` calls with VFS reads. At build time, use CMake custom commands to package assets into ZIP archives with SHA-256 manifests for integrity checking at load time.

**One threading caveat**: PhysFS uses a global mutex for all API calls. After initialization, call `PHYSFS_permitSymbolicLinks()` once and avoid config changes during gameplay to minimize contention with the fiber workers.

**Priority: Pre-launch. Effort: 1–2 weeks.**

### Layered config with nlohmann/json merge_patch handles platform differences cleanly

The engine already has hot-reloadable JSON config. Extend it to a **four-layer system**: compiled-in defaults → `platform.json` (mobile gets lower quality defaults, touch controls enabled) → `user_prefs.json` → command-line overrides. Implementation uses `nlohmann/json::merge_patch()` (RFC 7396) — each layer merge-patches onto the previous:

```cpp
config_ = compiled_defaults;
if (auto f = std::ifstream(platformConfigPath()); f.good())
    config_.merge_patch(json::parse(f));
if (auto f = std::ifstream(SDL_GetPrefPath("FateMMO","FateMMO") + "prefs.json"); f.good())
    config_.merge_patch(json::parse(f));
```

Add **config validation** with valijson (header-only, supports JSON Schema v7 via `NlohmannJsonAdapter`). Define a schema for each config section; on validation failure, log a warning and use the default value rather than crashing.

**Config migration** between versions uses a sequential migration chain: store a `config_version` integer, define migration lambdas `{from_version, to_version, transform_fn}`, and apply them in order at startup. Always back up the old config before migrating.

**Runtime feature flags** (`features.new_combat_system: false`) allow gradual rollout and A/B testing without recompilation.

**Priority: Pre-launch. Effort: 1 week.**

### The CMake build system must cleanly separate five targets

The engine needs a **multi-target CMake structure** that produces: (1) a static engine library (core, shared by all), (2) a game executable (desktop), (3) an editor executable (desktop, links ImGui), (4) iOS app, and (5) Android native library. The root CMakeLists.txt uses options to control what's built:

```cmake
option(FATEMMO_BUILD_EDITOR "Build editor with ImGui" ON)
option(FATEMMO_BUILD_MOBILE "Build for mobile" OFF)
option(FATEMMO_ENABLE_TRACY "Enable Tracy profiler" OFF)

add_subdirectory(engine)       # Always
add_subdirectory(game)         # Always
if(FATEMMO_BUILD_EDITOR AND NOT FATEMMO_BUILD_MOBILE)
    add_subdirectory(editor)   # Desktop only
endif()
```

For dependency management, **vcpkg in manifest mode** is the strongest recommendation for a solo C++ developer. It requires no Python (unlike Conan), integrates seamlessly with CMake via a single toolchain file, supports 2000+ packages, and provides reproducible builds through `vcpkg.json` baselines committed to the repo. Use **FetchContent** as a supplement for small libraries or your own subprojects.

Enable **ccache** (or **sccache** for MSVC) via `CMAKE_CXX_COMPILER_LAUNCHER` — this alone cuts incremental build times by **60–90%**. Add **precompiled headers** via `target_precompile_headers()` for stable, frequently-included headers (STL containers, SDL2, nlohmann/json, glm) — typically **20–50% compile time reduction**. Share PCH across targets with `REUSE_FROM`.

**Priority: Ship-blocking (basic structure) / Pre-launch (hardening). Effort: 2–3 weeks total.**

### Tracy profiler integration gives frame-level performance visibility

Tracy is the gold standard for C++ game profiling — nanosecond-granularity CPU zones, GPU timing via OpenGL timer queries, memory allocation tracking, and real-time visualization in a separate viewer application.

Integration via FetchContent:

```cmake
FetchContent_Declare(tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.11.1  GIT_SHALLOW TRUE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)  # No overhead when profiler not connected
FetchContent_MakeAvailable(tracy)
target_link_libraries(engine PUBLIC TracyClient)
target_compile_definitions(engine PUBLIC TRACY_ENABLE)
```

Instrument the engine with `ZoneScoped` at the top of every major function (physics update, ECS tick, SpriteBatch flush, network poll), `FrameMark` after `SDL_GL_SwapWindow()`, and `TracyGpuZone("SpriteDraw")` around GL draw calls. For GPU profiling, call `TracyGpuContext` after GL context creation and `TracyGpuCollect` after swap. Track memory with `TracyAlloc`/`TracyFree` per subsystem using named pools (`TracyAllocN(ptr, size, "TexturePool")`).

Build a **frame budget system** that reports to Tracy via `TracyPlot`: at 60fps the budget is **16.67ms** — allocate ~4ms to physics/ECS, ~8ms to rendering/GPU, ~2ms to AI/networking, with ~2.67ms headroom. When any subsystem exceeds its budget, emit `TracyMessageL("RENDER BUDGET EXCEEDED")` for immediate visual flagging in the profiler timeline.

Create a **"Release+Tracy" build configuration** via CMake presets: `CMAKE_BUILD_TYPE=RelWithDebInfo` with `-O2 -g -fno-omit-frame-pointer` and `TRACY_ENABLE=ON`. This runs at near-release speed with profiling overhead of **<1%** when Tracy's on-demand mode is enabled.

**Priority: Pre-launch. Effort: 1 week** for integration, ongoing for instrumentation.

### Mobile memory, threading, and audio each need platform-specific adaptation

**Memory management** on mobile is fundamentally different from desktop due to the absence of swap space on iOS and aggressive process killing on both platforms. iOS's **Jetsam** enforces per-process limits at roughly **50% of physical RAM** (e.g., ~2GB on a 4GB device). Android's **Low Memory Killer daemon** (lmkd) assigns OOM adjustment scores and kills by priority. The engine should define **three device tiers** based on detected RAM (use `[NSProcessInfo processInfo].physicalMemory` on iOS, `/proc/meminfo` on Android): Low (≤3GB, budget **150–250MB**), Medium (4–6GB, budget **250–400MB**), High (8GB+, budget **400–600MB**). The existing LRU texture cache budget should be set per-tier, and pool allocators for ECS components should have tier-dependent capacities.

For **per-subsystem tracking**, wrap each allocator with counters for current/peak allocation sizes and counts, tagged by subsystem (TEXTURE, AUDIO, ENTITY, NETWORK, UI). Report these via Tracy's named allocation tracking or a diagnostics overlay. Use **pool allocators** for fixed-size ECS components (O(1) alloc/dealloc via embedded free-lists) and **frame/bump allocators** (pointer-bump with per-frame reset) for temporary render commands and collision results — one per thread to avoid synchronization.

**Threading** must adapt to ARM's big.LITTLE architecture. Mobile CPUs have 4–8 cores, but only 2–4 are performance cores. **Cap the fiber worker pool at 2–3 threads on mobile** regardless of `hardware_concurrency()`: `std::min(std::thread::hardware_concurrency() - 2, 3u)`. Don't manually set thread affinity — use QoS classes on iOS (`QOS_CLASS_USER_INTERACTIVE` for render, `QOS_CLASS_UTILITY` for workers) and let the OS scheduler handle core placement. Monitor thermal state via iOS's `ProcessInfo.thermalState` and Android's `AThermal_getCurrentThermalStatus()` (API 30+). When thermal state reaches "serious," **drop to 30fps and reduce particle counts and draw distance**. On `SDL_APP_DIDENTERBACKGROUND`, signal all worker threads to complete and sleep — iOS freezes all threads within seconds of backgrounding.

**SoLoud audio** uses CoreAudio on iOS and OpenSL ES (or AAudio via miniaudio backend) on Android. The critical iOS-specific requirement is **AVAudioSession category configuration** — set `.ambient` (respects silent switch, mixes with other apps) or `.soloAmbient` (stops other audio) via an Objective-C++ bridge *before* SoLoud initialization. Register for `AVAudioSessionInterruptionNotification` to pause/resume SoLoud on phone calls. Use `SoLoud::Wav` for SFX (fully loaded, zero-latency playback) and `SoLoud::WavStream` for music (streamed from disk, minimal memory). Detect Bluetooth output via `AVAudioSession.currentRoute.outputs` — when BT is active, increase audio buffer size to reduce crackling and accept higher latency.

**Priority: Ship-blocking (lifecycle and memory), Pre-launch (thermal throttling, audio sessions). Effort: 3–4 weeks combined.**

### Mobile networking demands a reconnection state machine and IPv6

The engine's existing AEAD-encrypted, server-authoritative networking must handle mobile's hostile network environment. **WiFi-to-cellular transitions** change the device's IP address, instantly killing TCP connections and stalling UDP. Implement a **reconnection state machine**: `Connected → Disconnected (detect within 2–3 missed heartbeats) → Reconnecting (show overlay, buffer inputs, exponential backoff 1s/2s/4s/8s/max 30s) → Reconnected (re-authenticate via session token, server sends state delta) → or Failed (return to login after 60s)`.

**iOS kills background sockets aggressively** — typically within 30 seconds of suspension. The engine should accept this: on `SDL_APP_WILLENTERBACKGROUND`, send a "going away" message to the server, cleanly close the socket, and save state. On foreground restore, reconnect and request a full state sync. The session token design avoids repeating the full authentication handshake.

**IPv6 is mandatory for iOS App Store** since June 2016. Ensure the networking stack uses `getaddrinfo()` (not `gethostbyname()`), `AF_UNSPEC` (not `AF_INET`), and `sockaddr_storage` (not `sockaddr_in`). Server-side can remain IPv4-only — iOS DNS64/NAT64 translates transparently. Test on an IPv6-only network by creating one via macOS Internet Sharing with "Create NAT64 Network" enabled.

For **battery efficiency**, batch small packets into sends at **50–100ms intervals** rather than sending per-tick. Reduce server update rate for mobile clients to **5–10 Hz** (vs 20 Hz on desktop). The game's existing client-side prediction handles this gracefully — increase the interpolation buffer to **150–200ms** on mobile (vs ~100ms on desktop) to smooth over higher jitter.

**Priority: Ship-blocking. Effort: 2–3 weeks.**

### Structured logging with spdlog and SQLite saves complete the client stack

Replace the singleton logger with **spdlog** — async, fmt-based, with built-in sinks for Android (`android_sink_mt` routes to `__android_log_print`), rotating files, and stdout. Create named loggers per subsystem (`spdlog::get("net")`, `"ecs"`, `"render"`) with independently configurable levels. In release builds, compile-time strip trace/debug calls via `#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO`. Enable spdlog's **backtrace mode** (`spdlog::enable_backtrace(64)`) to maintain a ring buffer of the last 64 log entries — dump these as crash breadcrumbs when Sentry captures an error.

For **mobile remote logging**, buffer structured JSON log lines in memory and upload batches to the telemetry endpoint on a timer or when an error occurs. Cap local log storage to **5MB rotating** in `SDL_GetPrefPath()`.

For **client-side persistence**, use `SDL_GetPrefPath("FateMMO", "FateMMO")` to get the platform-correct writable directory on all six target platforms. Store **user preferences as JSON** (simple, human-readable, easy to debug) and use **SQLite** for structured data that benefits from transactions and queries (cached character data, chat history, friend list). SQLite's amalgamation is a single .c file, trivial to integrate via CMake, and its WAL journaling provides crash-safe writes on all platforms.

Schema versioning for SQLite uses `PRAGMA user_version`: read on startup, apply sequential migrations in a transaction, and set the new version. For JSON config, store a `config_version` field and run migration lambdas. Always create a backup before migrating.

**Priority: Pre-launch (logging), Post-launch (remote log collection). Effort: 1–2 weeks.**

---

## Priority matrix and effort estimates for the solo developer

| Topic | Priority | Effort | Dependencies |
|-------|----------|--------|--------------|
| SDL2 mobile lifecycle | Ship-blocking | 2–3 weeks | None |
| GL 3.3 → GLES 3.0 shaders | Ship-blocking | 1–2 weeks | None |
| Touch input / virtual controls | Ship-blocking | 2–3 weeks | SDL2 mobile |
| iOS build pipeline (CMake → TestFlight) | Ship-blocking | 2–3 weeks | GL migration |
| Android build pipeline (Gradle + NDK) | Ship-blocking | 3–4 weeks | GL migration |
| Screen scaling / aspect ratios | Ship-blocking | 1 week | None |
| Mobile networking (reconnect, IPv6) | Ship-blocking | 2–3 weeks | None |
| CMake multi-target build structure | Ship-blocking | 2–3 weeks | None |
| Mobile memory / threading / audio | Ship-blocking | 3–4 weeks | SDL2 mobile |
| Asset packaging (mobile bundles) | Ship-blocking | 1–2 weeks | Build pipelines |
| Release build config (LTO, installers) | Pre-launch | 2–3 weeks | None |
| Crash reporting (Sentry) | Pre-launch | 1 week | Release builds |
| Telemetry endpoint | Pre-launch | 1 week | Sentry |
| Auto-update system | Pre-launch | 2–3 weeks | Release builds, CDN |
| PhysicsFS VFS | Pre-launch | 1–2 weeks | None |
| Config layering / validation | Pre-launch | 1 week | None |
| Tracy profiler integration | Pre-launch | 1 week | None |
| spdlog structured logging | Pre-launch | 1 week | None |
| Save system (SDL_GetPrefPath + SQLite) | Pre-launch | 1 week | None |
| PGO builds, advanced CI | Post-launch | 1–2 weeks | Release builds |

## Conclusion: the critical path runs through mobile GL and build pipelines

The single highest-risk, highest-effort workstream is **mobile build pipeline setup** — not because the code changes are difficult, but because the toolchain complexity (Xcode signing, Gradle/NDK integration, GPU fragmentation testing) creates an unpredictable debugging surface. Start here. The GL 3.3-to-GLES 3.0 migration is deceptively simple for a 2D sprite batcher — most of the engine's rendering code will work with only shader preamble changes and a `glMapBufferRange` standardization.

The 480×270 native resolution is a strategic advantage: it makes pixel-perfect mobile scaling trivial, keeps VRAM usage minimal on constrained devices, and means texture compression is optional rather than mandatory. The existing architecture (LRU texture cache, fiber workers, server-authoritative networking, delta compression) maps directly to mobile requirements with only parameter tuning — reduce cache budgets, cap thread pools, increase interpolation buffers.

The infrastructure work (PhysicsFS, spdlog, Tracy, Sentry, layered config) consists of well-defined, low-risk integrations that individually take days, not weeks. Parallelizing them with mobile bringup during build-system wait times is the efficient path. A focused solo developer shipping the mobile pipeline first, then PC packaging, then infrastructure polish should reach a cross-platform beta in **6–7 months** — with the most uncertain work front-loaded where it belongs.