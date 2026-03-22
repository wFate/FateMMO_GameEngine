# Shipping a C++20 engine on three platforms at once

A custom 56K-line MMORPG engine built on SDL2, OpenGL 3.3, and Win32 can reach simultaneous Windows/iOS/Android production with **roughly 4–6 weeks of focused work** — far less than most developers expect. The core insight is that the engine's existing tech stack (SDL2, OpenGL, standard C++20) already abstracts 80% of platform differences. What remains is a structured platform abstraction layer, a GL-to-GLES shader adaptation, and build-system plumbing. This report covers every topic at implementation depth, with concrete code, exact commands, and battle-tested tool recommendations tailored to a solo developer shipping a chibi-style 2D MMO at 480×270 resolution.

---

## 1. One CMake project, three platform targets

The foundational requirement is a single CMake source tree producing Windows executables, iOS app bundles, and Android shared libraries. CMake 3.14+ natively supports `CMAKE_SYSTEM_NAME=iOS`, and the Android NDK ships its own toolchain file. Platform detection uses three clean conditionals:

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(TARGET_IOS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(TARGET_ANDROID TRUE)
elseif(WIN32)
    set(TARGET_WINDOWS TRUE)
endif()
```

**For iOS**, the native CMake approach (`cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0`) is mature and sufficient. The community toolchain `ios.toolchain.cmake` by leetal (~2.1K GitHub stars) offers finer control — setting `PLATFORM=OS64` for arm64 device builds, handling sysroot detection, and configuring libtool. Either works; the native approach is simpler. **For Android**, the NDK's `android.toolchain.cmake` (at `$ANDROID_NDK/build/cmake/android.toolchain.cmake`) sets key variables: `ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-24` (97%+ device coverage), and `ANDROID_STL=c++_shared`. Crucially, Android targets must be shared libraries (`add_library(main SHARED ...)`), not executables — SDL2's `SDLActivity.java` loads `libmain.so` by name via JNI.

The recommended directory structure keeps platform-specific code isolated under `src/platform/{windows,ios,android}/`, with a shared `src/core/` containing 95% of the engine. An `android/` directory at the project root holds the Gradle wrapper project, and an `ios/Info.plist.in` template serves CMake's `MACOSX_BUNDLE_INFO_PLIST` substitution. Platform-specific `.cpp` files are conditionally added via `list(APPEND PLATFORM_SOURCES ...)` based on the target.

### SDL2 bootstraps differently on each platform

SDL2 builds cleanly via `FetchContent` or `add_subdirectory()` on all three platforms. The key platform differences are in bootstrapping. On iOS, SDL2's `SDL_main.h` macro renames your `main()` to `SDL_main()`, and SDL provides `SDL_uikit_main.c` containing the real `main()` that starts the UIKit run loop. Your game code runs from within UIKit's event loop — **you cannot use an infinite `while(true)` main loop on iOS**. SDL maps UIKit lifecycle events to `SDL_APP_WILLENTERBACKGROUND`, `SDL_APP_DIDENTERFOREGROUND`, etc.

On Android, `SDLActivity.java` acts as the Java entry point, loading `libSDL2.so` and `libmain.so` via `System.loadLibrary()`. Your custom Activity extends `SDLActivity` and overrides `getLibraries()` to return these names. The Gradle `build.gradle` integrates CMake via `externalNativeBuild { cmake { path "jni/CMakeLists.txt" } }`, and assets are configured with `sourceSets.main { assets.srcDirs = ['../../assets'] }` pointing to the shared asset directory.

### Dependency classification drives the build strategy

Header-only libraries (stb_image, stb_truetype, nlohmann/json, Dear ImGui, Tracy) vendor directly into the source tree — zero build system complexity. Libraries requiring platform-specific builds need individual strategies:

- **SDL2**: `add_subdirectory()` from vendored source. Works on all three platforms.
- **OpenSSL**: Use pre-built binaries per platform. The `openssl-cmake` project or pre-built archives from `openssl_for_ios_and_android` repos simplify this. Alternatively, **BoringSSL** has superior CMake integration.
- **libpq/libpqxx**: **Exclude entirely from mobile builds.** A mobile MMORPG client must never connect directly to PostgreSQL — this exposes credentials, bypasses validation, and fails App Store security review. All database access goes through the server. Gate it with `if(NOT TARGET_IOS AND NOT TARGET_ANDROID)`.
- **SoLoud**: Vendor source and compile as a static library. Select audio backend via defines: `WITH_SDL2_STATIC` (desktop), `WITH_COREAUDIO` (iOS), `WITH_MINIAUDIO` (Android — wraps AAudio/OpenSL ES).

vcpkg's `arm64-ios` triplet **does not exist as a built-in** and must be custom-created. The `arm64-android` triplet is a fragile community offering. For a solo developer, vendoring dependencies and building from source via CMake is more reliable than package manager cross-compilation.

### Asset packaging uses three different mechanisms

Windows assets live next to the executable, copied via `add_custom_command(POST_BUILD ...)`. iOS assets go into the app bundle via `set_source_files_properties(... PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/...")`. Android assets reside in `src/main/assets/` inside the Gradle project.

**The critical Android difference**: assets inside the APK are compressed and **cannot be read via `fopen()`**. All file I/O for bundled assets must go through `SDL_RWFromFile()`, which internally uses Android's `AAssetManager`. Additionally, `SDL_GetBasePath()` returns `NULL` on Android SDL2. The portable solution is to use `SDL_RWFromFile("assets/path", "rb")` everywhere — SDL handles platform-specific asset access internally on all three platforms.

---

## 2. The platform abstraction layer takes two weeks

The PAL encapsulates five Win32-specific subsystems behind portable interfaces. The architecture follows Godot's pattern: a common header (`Platform.h`) with free functions, and platform-specific `.cpp` implementations selected by CMake conditionals.

### Fiber portability through minicoro, not coroutines

The Win32 fiber-based job system (`CreateFiber`/`SwitchToFiber`) needs a cross-platform replacement. Three options exist, with clear tradeoffs:

**minicoro** (single-header C library, ~650 SLOC) is the recommended choice. It supports Windows x86_64, macOS/iOS ARM64, Android ARM64, and Linux via hand-written assembly for context switching. The migration is mechanical: replace `CreateFiber` → `mco_create`, `SwitchToFiber` → `mco_resume`/`mco_yield`. The existing fiber-based job system architecture and async DB dispatcher semantics remain intact. **Estimated effort: 1–2 days.**

C++20 coroutines (`co_await`/`co_yield`) are fully supported on all three compilers (MSVC, Apple Clang, NDK Clang), but they are **stackless** — suspension only works at explicit `co_await` points, not from arbitrary call depth. This fundamental limitation means the async DB dispatcher (which likely suspends from within deeply-nested function calls) would require significant restructuring. C++20 coroutines are the cleaner long-term architecture but require **1–2 weeks** of migration work.

`ucontext_t` is deprecated on macOS/iOS since 10.6. Boost.Context works but is a heavy dependency. **For a solo developer who needs minimal disruption, minicoro is the clear winner.**

### Socket adaptation is a thin wrapper

The custom reliable UDP transport needs surprisingly few changes. The core protocol logic (sequence numbers, acknowledgments, retransmission, ordering) is entirely platform-agnostic. Only the socket interface layer differs:

| Winsock2 | POSIX (iOS/Android) |
|---|---|
| `SOCKET` (unsigned) | `int` |
| `closesocket(s)` | `close(s)` |
| `WSAGetLastError()` | `errno` |
| `WSAEWOULDBLOCK` | `EAGAIN`/`EWOULDBLOCK` |
| `ioctlsocket(s, FIONBIO, &mode)` | `fcntl(s, F_SETFL, O_NONBLOCK)` |
| `WSAStartup`/`WSACleanup` required | Not needed |

A `NetSocket.h` header with `#ifdef _WIN32` branches for `socket_t`, `CloseSocket()`, `GetSocketError()`, `IsWouldBlock()`, and `SetNonBlocking()` handles all differences. The POSIX side also needs `EINTR` handling (retry interrupted calls). **Total changes: ~50–100 lines** in the networking code. iOS allows UDP sockets without restriction when the app is in the foreground. Android requires `<uses-permission android:name="android.permission.INTERNET" />` in the manifest (auto-granted, no runtime prompt).

### Threading is already portable, with one exception

`std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`, `std::latch`, `std::barrier`, and `std::counting_semaphore` all work identically across MSVC, Apple Clang, and NDK Clang. However, **`std::jthread` and `std::stop_token` are NOT available** in Apple's or Android's libc++ (as of Xcode 16 / NDK r28). Use a simple RAII wrapper around `std::thread` with an `std::atomic<bool>` stop flag instead.

For timers, replace `QueryPerformanceCounter` with **`std::chrono::steady_clock`** — on MSVC it wraps QPC directly, on Apple it uses `mach_absolute_time()`, on Android it uses `clock_gettime(CLOCK_MONOTONIC)`. All provide nanosecond precision. `timeBeginPeriod(1)` is Windows-only (fixes the 15.6ms sleep granularity problem); POSIX systems don't need it.

**Android threading gotcha**: native threads don't have a `JNIEnv`. Before any JNI call, call `AttachCurrentThread()` and ensure `DetachCurrentThread()` runs before the thread exits — use `pthread_key_create` with a destructor for automatic cleanup.

---

## 3. OpenGL ES 3.0 is almost identical for 2D rendering

The rendering port is less work than expected because GL 3.3 Core and ES 3.0 share **nearly identical feature sets** for 2D rendering. Every feature the engine's 10-pass render graph relies on — FBOs, instanced rendering, VAOs, MRT (minimum 4 color attachments), UBOs with `std140` layout, `glBlitFramebuffer`, and `glMapBufferRange` — is core in ES 3.0.

### Shader changes are mechanical

Both GLSL 3.30 and GLSL ES 3.00 use `in`/`out` qualifiers and `layout(location=N) out` for fragment outputs. The only mandatory changes are the version directive and precision qualifiers:

```glsl
#ifdef GL_ES
#version 300 es
precision highp float;
precision highp sampler2D;
#else
#version 330 core
#endif
```

The **recommended approach for <20 shaders** is runtime version header injection — the engine prepends the correct header based on the active GL context. This avoids duplicate shader files and is the simplest path. A SPIRV-Cross pipeline (glslang → SPIR-V → SPIRV-Cross → target GLSL) is powerful but overkill for GL 3.3↔ES 3.0 where the languages differ by ~5%. Reserve that investment for when Metal or Vulkan backends are added.

**highp float in fragment shaders** is technically optional in ES 3.0, but all Apple A7+ (2013+) and all modern Android GPUs support it. For SDF text rendering, highp is required — mediump causes visible banding on distance field edges. Check `GL_FRAGMENT_PRECISION_HIGH` as a safety net.

### Features that need workarounds

The engine must avoid four desktop-only features: **`glPolygonMode(GL_LINE)`** for wireframe debug (draw `GL_LINES` geometry instead), **`GL_QUADS`** (use `GL_TRIANGLES` with index buffers), **`glMapBuffer`** without range (use `glMapBufferRange` always), and **geometry/compute shaders** (not available until ES 3.1/3.2). None of these are likely in a 2D sprite engine's critical path — wireframe is debug-only and should be `#ifdef`'d out of release builds.

### The custom GL function loader works unchanged

**SDL_GL_GetProcAddress works on all three platforms.** On iOS, SDL internally uses `dlsym` on OpenGLES.framework. On Android, it uses `dlsym` on `libGLESv2.so` and `eglGetProcAddress`. The existing custom loader that calls `SDL_GL_GetProcAddress` for every GL function pointer needs zero changes. Just request an ES context on mobile via `SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES)`.

### At 480×270, mobile performance is a non-issue

The total pixel throughput is **129,600 pixels per frame** — even with 10 full-screen passes, that's ~1.3M fragments. Modern mobile GPUs process billions of fragments per second. The entire frame's pixel work is **<0.001% of GPU capacity**. The real costs are FBO switch overhead on tile-based deferred rendering (TBDR) GPUs. Optimization strategy:

- **Merge passes sharing the same FBO**: passes 1–5 (GroundTiles → Entities → Particles → SDFText → DebugOverlays) should render to a single scene FBO without switching.
- **`glInvalidateFramebuffer`** at the end of each pass for unneeded attachments (depth/stencil), and **`glClear`** at the start of each pass — both give the TBDR GPU free load/store optimizations.
- **Quality tiers**: High (all passes), Medium (skip BloomExtract + BloomBlur), Low (skip bloom + PostProcess, simplified lighting baked into scene pass).

For texture compression, **uncompressed RGBA8 is viable** at this resolution. A 2048×2048 atlas is 16MB; 3–5 atlases total 50–80MB — well within mobile VRAM. If compression is needed, ASTC 4×4 offers the best quality for sprite art on mobile, with ETC2 as fallback. Both are core in ES 3.0.

---

## 4. Asset pipeline from development to shipping

The transition from raw development assets to optimized platform-specific bundles is what separates a hobby engine from a shippable product. The pipeline has five components: a manifest system, a cook tool, binary scene serialization, font management, and shader variants.

### A JSON manifest with xxHash content hashes

The asset manifest lists every cooked asset with its type, size, content hash, and dependencies. Use **xxHash (XXH3_128bits)** for content hashing — it runs at **~31 GB/s** on modern CPUs, compared to SHA-256's ~3 GB/s, and provides excellent collision resistance for the ~10K assets a 2D MMO requires. The manifest is generated at cook time and enables delta patching: only download assets whose hashes differ from the client's local manifest.

The manifest maps string paths → stable integer asset IDs at cook time. At runtime, the AssetRegistry assigns generational handles (generation counter + index) when loading, providing stale-reference detection.

### The cook tool: a standalone C++ executable

A cook tool transforms raw development assets into platform-optimized formats. Implement it as a separate CMake target sharing serialization code with the engine:

- **Textures**: PNG sprite sheets → platform-specific format (RGBA8 or ASTC/ETC2 via `astcenc` or `etc2comp`)
- **Atlases**: stb_rect_pack (single-header, Skyline algorithm) or rectpack2D for bin-packing sprite sheets into atlases
- **Scenes**: JSON → MessagePack via `nlohmann::json::to_msgpack()` — a one-line change yielding **30–50% smaller files** with ~2× faster parsing
- **Audio**: WAV → OGG Vorbis for music (80–90% size reduction); SoLoud decodes OGG at runtime
- **Fonts**: msdf-atlas-gen to produce MTSDF atlases from bundled TTF files

CMake integrates the cook step via `add_custom_target(cook_assets COMMAND asset_cooker --input ... --output ... --platform ...)` with the game depending on cooked assets.

### Binary scenes via MessagePack is the practical choice

JSON parsing for a 200KB scene takes ~1–2ms even on mobile — not a bottleneck for load screens. The real value of binary serialization is **file size reduction for mobile bundles**. nlohmann/json's built-in `to_msgpack()`/`from_msgpack()` provides this with zero deserialization code changes. FlatBuffers offers zero-copy deserialization (~10× faster than nlohmann JSON) but requires schema files and code generation — overkill unless scene loading becomes a measured bottleneck.

### Font bundling and CJK strategy

Mobile platforms don't have Consolas. Bundle **JetBrains Mono** (OFL license, 300KB) for debug/UI text. For CJK internationalization, the glyph count problem is real: 20,000+ Chinese characters at 32×32 pixels per MTSDF glyph would require ~80MB of atlas data. The practical solution is **hybrid rendering**: pre-bake MTSDF atlases for Latin characters and the ~3,000–4,000 CJK characters actually used in translation files (scan localized strings at cook time), then use a **dynamic glyph atlas with LRU eviction** for user-generated content (chat, player names).

---

## 5. One-command builds from source to device

The developer experience goal is `./build.sh ios debug` producing a running app on a connected iPhone within seconds of an incremental change.

### iOS workflow: CMake → Xcode → device

```bash
cmake -B build/ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="YOUR_TEAM_ID"
```

This generates an Xcode project. Open it, enable automatic signing, and hit Cmd+R to build and deploy to a connected device. For command-line deployment: `xcodebuild` for building, `ios-deploy --bundle path/to/mymmo.app` for device installation. iOS **requires** a LaunchScreen (use `UILaunchScreen` dict in Info.plist for simplicity) and a `MACOSX_BUNDLE` target with `Info.plist` specifying `CFBundleShortVersionString` and `CFBundleVersion`.

TestFlight deployment chains three commands: `xcodebuild archive`, `xcodebuild -exportArchive`, and `xcrun altool --upload-app`. The full cycle from code change to TestFlight build takes ~5–10 minutes.

### Android workflow: Gradle wraps CMake

The Android project structure nests an `android/` directory at the project root containing the Gradle wrapper. The `app/build.gradle` specifies `externalNativeBuild { cmake { path "jni/CMakeLists.txt" } }`, linking back to the engine's CMake. Key settings: `minSdkVersion 24` (Android 7.0, 97%+ coverage), `targetSdkVersion 34` (Google Play requirement), and `ndkVersion "26.1.10909125"`. Build and install with:

```bash
cd android && ./gradlew installDebug  # builds, installs, launches
```

Gradle incremental builds work — if only C++ changed, only the native library recompiles (~5–15 seconds). Google Play requires AAB format (`./gradlew bundleRelease`); APK is fine for sideloading during development.

### CI/CD on GitHub Actions builds all three in parallel

A single workflow file runs three parallel jobs: `windows-latest` for MSVC, `macos-latest` for Xcode/iOS, and `ubuntu-latest` for Android/Gradle. **iOS code signing in CI is the hardest part** — it requires storing a .p12 certificate and .mobileprovision profile as base64-encoded GitHub Secrets, creating a temporary keychain, importing the certificate, and cleaning up afterward. The workflow installs the certificate into a temporary keychain, builds an archive with `CODE_SIGN_STYLE=Manual`, exports an IPA, and uploads via `xcrun altool`.

**Fastlane is worth the ~2–4 hour setup investment** for a solo developer. `fastlane match` manages iOS certificates/profiles in a private git repo (solving the signing nightmare), `fastlane pilot` uploads to TestFlight in one command, and `fastlane supply` publishes AABs to Google Play.

### Wrapper scripts make the experience seamless

A `build.sh` script accepts platform and config arguments: `./build.sh windows debug`, `./build.sh ios release`, `./build.sh android debug`. The script checks for prerequisite tools (`cmake`, `xcodebuild`, `$ANDROID_HOME`), invokes the correct CMake generator or Gradle command, and for debug builds, automatically installs and launches on a connected device or simulator.

---

## 6. Production code quality separates shippable from prototype

### Precompiled headers deliver the largest compile-time improvement

For 56K lines across 327 files, PCH alone can reduce full rebuild times by **40–70%**. CMake 3.16+ provides `target_precompile_headers` that works identically across MSVC, Clang, and GCC:

```cmake
target_precompile_headers(MyEngine PRIVATE
    <vector> <string> <unordered_map> <memory> <functional>
    <optional> <variant> <cstdint> <algorithm>
    <nlohmann/json.hpp>  # ~25K lines expanded
    <SDL2/SDL.h>
)
```

Only include headers that are (a) used in >30% of translation units, (b) rarely change, and (c) expensive to parse. Target: **full rebuild under 30 seconds, incremental under 5 seconds** — achievable with PCH plus parallel compilation (`cmake --build . -j$(nproc)`).

**C++20 modules are not viable for cross-platform engines in 2025–2026.** Apple Clang still lacks C++20 module support, and Android NDK's CMake integration is untested for modules. Unity builds (`CMAKE_UNITY_BUILD`) accelerate CI full rebuilds but can cause static variable conflicts — use them in CI only.

### Static analysis catches real bugs

A `.clang-tidy` configuration enabling `bugprone-*`, `performance-*`, `modernize-*`, and `readability-*` while **disabling `readability-magic-numbers`** (game code is full of tuning constants), **`bugprone-easily-swappable-parameters`** (too many false positives), and **`readability-identifier-length`** (short names like `x`, `y`, `dt` are idiomatic) provides high signal-to-noise. Promote `bugprone-use-after-move` and `performance-unnecessary-copy-initialization` to errors. In CI, run clang-tidy on changed files only (`git diff --name-only | xargs clang-tidy`) to avoid bottlenecking PRs.

### Release hardening with LTO, stripping, and security flags

```cmake
# LTO (portable)
set_property(TARGET MyEngine PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# MSVC: /O2 /GL /GS /Gy /LTCG /OPT:REF /DYNAMICBASE /NXCOMPAT
# Clang: -O2 -ffunction-sections -fdata-sections -fstack-protector-strong -Wl,--gc-sections
# iOS: -Wl,-dead_strip, XCODE_ATTRIBUTE_STRIP_INSTALLED_PRODUCT=YES
```

**Never use `-ffast-math` for an MMO.** It breaks IEEE 754 compliance, causes floating-point non-determinism across platforms, and will cause desync between clients and server. The No Man's Sky team confirmed `-ffast-math` caused different procedural generation results on PS4 vs PC.

Binary size for a 2D engine: native code compiles to ~5–10MB stripped. Assets dominate total app size. For mobile, use `-Oz` (size optimization) and `--gc-sections` to eliminate unused code. The **Google Play download limit is 200MB**, Apple's cellular download limit is **200MB** — target under 50MB for the initial download by deferring large asset packs.

---

## 7. Editor polish that transforms daily workflow

Three features deliver the highest return on investment for transforming the editor from "usable for testing" to "usable for daily production": **comprehensive keyboard shortcuts, a command palette, and play-in-editor**.

### Keyboard shortcuts and command palette

Implement all standard bindings: Ctrl+Z/Y (undo/redo), Ctrl+S (save), Delete (remove entity), Ctrl+D (duplicate), F (focus selection), W/E/R (translate/rotate/scale gizmo), Space (play/stop). A Ctrl+Shift+P command palette using fuzzy search (the open-source `imgui-command-palette` library or a custom ~50-line implementation) makes every editor action discoverable.

**ImGui 1.91+** (merged July 2024) includes native multi-select via `BeginMultiSelect()`/`EndMultiSelect()` with standard Ctrl+Click, Shift+Click, and Ctrl+A idioms. This eliminates the need for custom multi-select infrastructure.

### Editor/runtime separation via CMake targets

The engine should produce two distinct binaries: `game_editor` (full ImGui editor, debug visualization, hot-reload, Tracy overlay) and `game_runtime` (zero editor code). Structure this as a shared `engine_core` static library plus an `engine_editor` library that depends on it:

```cmake
add_library(engine_core STATIC src/engine/...)  # Always built
add_library(engine_editor STATIC src/editor/...) # Editor-only
target_compile_definitions(engine_editor PUBLIC EDITOR_BUILD=1)
target_link_libraries(engine_editor PUBLIC engine_core imgui imguizmo)

add_executable(game_editor src/game/main.cpp)
target_link_libraries(game_editor PRIVATE engine_editor)

add_executable(game_runtime src/game/main.cpp)
target_link_libraries(game_runtime PRIVATE engine_core)
```

Use no-op inline functions for debug drawing (`inline void DebugDrawRect(Vec2, Vec2, Color) {}`) so call sites don't need `#ifdef` guards — the compiler eliminates them entirely in runtime builds.

### Play-in-editor with serialize/restore

The Unity-style approach: serialize the current scene state to a memory buffer before entering play mode, run the game in the same process, and restore the scene on stop. This preserves separate editor and game camera states and allows live tweaking of visual properties (colors, sprite references, transform) during play. Structural changes (adding/removing entities) during play should be blocked to prevent corruption of the saved state.

### Tracy profiler works remotely on mobile devices

Tracy uses a TCP client-server model. To profile a mobile build, both the device and PC must be on the same WiFi network — the Tracy GUI connects to the device's IP on port 8086. For Android, use ADB port forwarding: `adb forward tcp:8086 tcp:8086`. Compile with `TRACY_ENABLE` and `TRACY_ON_DEMAND` (only collects data when a viewer connects, zero overhead otherwise). GPU profiling works on Android via OpenGL timestamp queries but is **not supported on iOS** due to Apple GPU limitations.

---

## 8. Deployment pipeline for a live game

### Version numbering from git tags

Use `git describe --tags` in a CMake module to extract semantic version (from tags like `v1.2.3`) and build number (from `git rev-list --count HEAD`). Generate a `version.h` via `configure_file()` containing `GAME_VERSION_STRING` and `GAME_BUILD_NUMBER`. Embed per platform: Windows `.rc` resource file with `VS_VERSION_INFO`, iOS `CFBundleShortVersionString`/`CFBundleVersion` in Info.plist, Android `versionName`/`versionCode` in `build.gradle`.

A separate **protocol version integer** (bumped only when the wire format changes) enables version checking during the connection handshake. The server rejects clients with mismatched protocol versions and the client displays "Update required" with a deep link to the appropriate app store (`itms-apps://...` for iOS, `market://...` for Android).

### Sentry Native SDK for cross-platform crash reporting

**Sentry is the clear winner** for a solo developer shipping on three platforms. The `sentry-native` SDK (CMake-compatible, uses Crashpad as backend) captures crashes on Windows (.pdb symbolication), iOS (.dSYM symbolication), and Android (unstripped .so symbolication). The free tier provides 5K errors/month — sufficient through launch.

Integration is straightforward: `sentry_init()` with your DSN at startup, `sentry_shutdown()` at exit, and `sentry-cli debug-files upload` in CI to upload platform-specific debug symbols. Sentry automatically tracks **crash-free session rate** — the single most important quality metric. Target **>99.5%** before public launch.

For server-side, the same SDK works on the headless Linux server binary, providing unified crash reporting across all four deployment targets (Windows client, iOS client, Android client, Linux server).

### Docker for server deployment with negligible overhead

Per Hathora's 2024 benchmarks, Docker adds **<0.12% compute overhead** and **~5µs P99 network latency** for UDP — both negligible compared to internet latency. Use `--network=host` to bypass Docker's network bridge entirely. A `docker-compose.yml` running the game server alongside PostgreSQL 16 provides a reproducible single-command deployment.

The deployment workflow for a solo developer:
1. CI builds the server binary and Docker image, pushes to GitHub Container Registry
2. `ssh server "cd /opt/game && docker compose pull && docker compose up -d"` deploys
3. Database migrations run via `golang-migrate` before the server starts — plain SQL files in `db/migrations/` versioned in git

Blue/green zero-downtime deployment is impractical for a solo dev MMO. Instead: announce maintenance 15 minutes ahead, save all player state, gracefully disconnect, migrate DB, restart, let players reconnect. A 30-second window is acceptable for indie MMOs.

### Cloudflare R2 for content distribution at zero egress cost

For Windows PC patching and any future asset-bundle CDN, **Cloudflare R2** charges $0.015/GB stored per month with **zero egress fees**. Attach a custom domain for full Cloudflare CDN caching. A simple manifest-based patcher (compare file hashes, download only changed files via HTTPS) provides Windows auto-updating without the complexity of binary delta patching.

### iOS memory limits require attention

iOS uses the **Jetsam** system to enforce per-process memory limits. Exceeding the limit causes immediate termination — no save opportunity beyond `didReceiveMemoryWarning`. Approximate limits: **~1.0–1.4GB** on 3GB devices (iPhone SE 2nd gen), **~2.5–3.0GB** on 6GB devices (iPhone 15). For a 2D MMO at 480×270 with a handful of texture atlases, **target <1.0GB** to support the widest device range. Add the `com.apple.developer.kernel.increased-memory-limit` entitlement to request higher limits on supported hardware.

---

## Conclusion: a prioritized roadmap

The total estimated effort breaks down into three phases. **Phase 1 (2 weeks)** tackles the PAL: drop in minicoro for fibers, add the socket abstraction header, replace QPC with `std::chrono::steady_clock`, implement the VFS abstraction for Android asset loading, and gate file watching and editor code behind `#ifdef`. **Phase 2 (1–2 weeks)** handles the rendering port: add precision qualifiers and version header injection to shaders, verify ES 3.0 feature usage, add `glInvalidateFramebuffer` calls, and implement quality tiers. **Phase 3 (1–2 weeks)** builds the cross-platform build pipeline: configure CMake for iOS/Android, set up the Gradle wrapper project, create build scripts, and establish CI/CD.

The two most surprising findings: first, that `SDL_GL_GetProcAddress` and `SDL_RWFromFile` already abstract most platform differences in GL loading and asset I/O — the engine's existing code needs less change than expected. Second, that at 480×270 resolution, mobile GPU performance is so far from being a bottleneck that even the full 10-pass render graph runs comfortably without optimization. The real constraints on mobile are memory limits (iOS Jetsam), binary size (store download limits), and the bootstrapping differences (SDL lifecycle, code signing, APK asset access).

The engine's existing architecture — archetype ECS, arena memory, custom reliable UDP, render graph — is already production-grade in design. What makes it shippable is the infrastructure around it: one-command cross-platform builds, automated crash reporting via Sentry, version-gated client-server handshakes, and an editor workflow that supports play-test-iterate cycles measured in seconds rather than minutes.