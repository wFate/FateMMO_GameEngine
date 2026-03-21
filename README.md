# FateMMO Game Engine

Custom 2D MMORPG engine built in C++23. Features a data-oriented archetype ECS, zone-based arena memory, spatial grid with zero-hash cell lookup, server-authoritative netcode with AOI replication, AEAD-encrypted UDP transport, and a built-in Unity-style editor.

## Tech Stack

- **Language:** C++23 (MSVC, GCC 13, Clang 17)
- **Graphics:** SDL2 + OpenGL 3.3 Core (GLES 3.0 on iOS/Android)
- **Editor:** Dear ImGui (docking) + ImGuizmo + ImPlot
- **Networking:** Custom reliable UDP, AEAD encryption (XChaCha20-Poly1305 via libsodium), IPv6 dual-stack
- **Auth:** TLS (OpenSSL), bcrypt password hashing, session tokens
- **Database:** PostgreSQL (libpqxx), connection pool with circuit breaker, write-ahead log
- **Profiler:** Tracy (on-demand)
- **Logging:** spdlog (per-subsystem, rotating file sink)
- **VFS:** PhysicsFS (mount/overlay for asset packaging)
- **Tests:** doctest (573 tests, 3732 assertions across 75 test files)
- **Build:** CMake 3.20+ with FetchContent (zero manual dependency setup)
- **CI/CD:** GitHub Actions (MSVC + GCC 13 + Clang 17, headless OpenGL via Xvfb)

## Architecture

```
Engine (fate_engine static library)
├── ECS          Archetype SoA storage, generational handles, reflection, command buffer
├── Memory       Zone arenas (256 MB, O(1) reset), frame arena, scratch arenas, pool allocator
├── Spatial      Power-of-two grid (bitshift lookup) + Mueller hash fallback
├── Tilemap      7-state chunk lifecycle, ticket system, rate-limited streaming
├── Rendering    SpriteBatch (palette swap), 10-pass render graph, SDF text (MTSDF), lighting, bloom
├── Scene        Zone arena lifecycle, snapshot persistence, loading state
├── Editor       ImGui dockspace, hierarchy, inspector, tile palette, undo/redo, console
├── Net          Reliable UDP, AEAD encryption, IPv6 dual-stack, AOI replication, delta compression
│                Auto-reconnect (exponential backoff), rate limiting, connection cookies
├── Input        ActionMap (23 actions), 6-frame buffer, touch injection API
├── Asset        Hot-reload (file watcher, 300ms debounce), generational handles, LRU texture cache
├── VFS          PhysicsFS wrapper (mount/overlay/read for asset packaging and mods)
├── Job          Win32 fiber / minicoro (cross-platform), lock-free MPMC queue
├── Platform     Device info (RAM tiers, VRAM budgets, thermal state), SDL lifecycle
├── Telemetry    Metric collector with JSON serialization
├── Particle     CPU emitters, gravity, lifetime, color lerp
└── Profiling    Tracy zones, arena tracking, frame marks

Game (FateEngine executable)
├── Components   27 game components (Hot/Warm/Cold tier, zero RTTI)
├── Systems      Movement, Combat, MobAI, Spawning, Quests, Zones, NPC interaction
├── Shared       Combat formulas, inventory, skills, parties, guilds, trade, market, chat
├── Prediction   Optimistic combat feedback (immediate attack animations, prediction buffer)
└── UI           HUD, inventory, skill bar, shop, quest log, chat, login, death overlay, touch controls

Server (FateServer headless)
├── Auth         Account login/registration, bcrypt, session tokens, AEAD key exchange
├── DB           13 PostgreSQL repositories, connection pool, circuit breaker, write-ahead log
├── Security     Rate limiting, nonce manager, target validation (AOI + faction + party + PK)
├── Cache        ItemDefinition + LootTable + MobDef + SkillDef + Scene hot-path caches
└── App          20 Hz tick loop, two-tick death lifecycle, mob spawning, zone replication

Tests (573 test cases, 3732 assertions)
├── Unit         Combat, networking, protocol, inventory, death lifecycle, PvP validation
├── Integration  Replication, server integration, gameplay
└── Scenario     End-to-end TestBot (auth + UDP, 10 scenario tests against live server)
```

## Building

All dependencies are fetched automatically via CMake FetchContent. vcpkg provides OpenSSL, libpq, and libsodium on Windows.

```bash
# First time only (Windows):
C:\vcpkg\vcpkg.exe install openssl:x64-windows libpq:x64-windows libsodium:x64-windows

# Build:
cmake -S . -B build
cmake --build build --config Debug
```

### Output

| Target | Path |
|--------|------|
| Client | `build/Debug/FateEngine.exe` |
| Server | `build/Debug/FateServer.exe` |
| Tests  | `build/Debug/fate_tests.exe` |

### Platforms

| Platform | Status |
|----------|--------|
| Windows  | Primary development target |
| iOS      | Build pipeline ready (CMake Xcode generator, GLES 3.0, TestFlight script) |
| Android  | Build pipeline ready (Gradle + NDK r27, SDLActivity, `./gradlew installDebug`) |
| Linux    | CI builds (GCC 13, Clang 17), server target |
| macOS    | Supported (CMake, minicoro fibers) |

### iOS

```bash
cd ios && ./build.sh debug build      # Generate Xcode project + build
cd ios && ./build.sh debug device     # Build + deploy to connected iPhone
cd ios && ./build.sh release testflight  # Archive + upload to TestFlight
```

### Android

```bash
cd android && ./gradlew installDebug  # Build + install on connected device
cd android && ./gradlew bundleRelease # Build AAB for Google Play
```

## Running Tests

```bash
# Unit tests (headless, no server required):
build/Debug/fate_tests.exe

# Specific test suite:
build/Debug/fate_tests.exe -tc="Death Lifecycle"
build/Debug/fate_tests.exe -tc="PvP Target Validation"
build/Debug/fate_tests.exe -tc="PacketCrypto"
```

## Key Design Decisions

- **Archetype ECS** over per-entity hashmaps — contiguous memory, O(matching) queries
- **Zone arenas** — all zone memory freed in O(1) on scene exit, no fragmentation
- **Spatial grid** — power-of-two bitshift cell lookup, zero hash computation for bounded maps
- **Server-authoritative** — client sends intent only, server validates and applies all state changes
- **Two-tick death** — Alive → Dying (on-death procs fire) → Dead (next tick), prevents lost kill credit
- **AEAD encryption** — XChaCha20-Poly1305 on all game traffic, per-session keys, sequence-as-nonce
- **IPv6 dual-stack** — sockaddr_storage, AF_INET6 with IPv4 fallback, iOS App Store compliant
- **AOI replication** — delta-compressed entity updates scoped to player visibility, 14-field bitmask
- **Custom reliable UDP** — 3 channels (unreliable/reliable-ordered/reliable-unordered), no TCP overhead
- **Fiber job system** — Win32 fibers / minicoro (cross-platform), lock-free work stealing
- **Optimistic combat** — attack animations play immediately, server confirms damage numbers

## Contributor

**Caleb Kious** — Engine architecture, networking, server systems, cross-platform tooling
