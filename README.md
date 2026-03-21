# FateMMO Game Engine

Custom 2D MMORPG engine built in C++23. Features a data-oriented archetype ECS, zone-based arena memory, spatial grid with zero-hash cell lookup, server-authoritative netcode with AOI replication, and a built-in Unity-style editor.

## Tech Stack

- **Language:** C++23 (MSVC)
- **Graphics:** SDL2 + OpenGL 3.3 Core (GLES 3.0 on iOS)
- **Editor:** Dear ImGui (docking) + ImGuizmo + ImPlot
- **Networking:** Custom reliable UDP (Winsock2), TLS auth (OpenSSL)
- **Database:** PostgreSQL (libpqxx)
- **Profiler:** Tracy (on-demand)
- **Logging:** spdlog (per-subsystem, rotating file sink)
- **Tests:** doctest (400+ tests, 53 test files)
- **Build:** CMake 3.20+ with FetchContent (zero manual dependency setup)

## Architecture

```
Engine (fate_engine static library — ~18,300 LOC)
├── ECS          Archetype SoA storage, generational handles, reflection, command buffer
├── Memory       Zone arenas (256 MB, O(1) reset), frame arena, scratch arenas, pool allocator
├── Spatial      Power-of-two grid (bitshift lookup) + Mueller hash fallback
├── Tilemap      7-state chunk lifecycle, ticket system, rate-limited streaming
├── Rendering    SpriteBatch, 10-pass render graph, SDF text (MTSDF), 2D lighting, bloom
├── Scene        Zone arena lifecycle, snapshot persistence, loading state
├── Editor       ImGui dockspace, hierarchy, inspector, tile palette, undo/redo, console
├── Net          Reliable UDP, AOI replication, delta compression, auth client, 30+ messages
├── Input        ActionMap (23 actions), 6-frame buffer, touch injection API
├── Asset        Hot-reload (file watcher, 300ms debounce), generational handles
├── Job          Win32 fiber system (4 workers, lock-free MPMC queue)
├── Particle     CPU emitters, gravity, lifetime, color lerp
└── Profiling    Tracy zones, arena tracking, frame marks

Game (FateEngine executable — ~23,600 LOC)
├── Components   27 game components (Hot/Warm/Cold tier, zero RTTI)
├── Systems      Movement, Combat, MobAI, Spawning, Quests, Zones, NPC interaction
├── Shared       Combat formulas, inventory, skills, parties, guilds, trade, market, chat
└── UI           HUD, inventory, skill bar, shop, quest log, chat, login, touch controls

Server (FateServer headless — ~9,000 LOC)
├── Auth         Account login/registration, bcrypt, session tokens
├── DB           13 PostgreSQL repositories (characters, inventory, skills, quests, guilds...)
├── Cache        ItemDefinition + LootTable hot-path caches
└── App          20 Hz tick loop, region-based mob spawning, zone replication

Tests (~8,400 LOC, 400+ test cases)
```

## Building

All dependencies are fetched automatically via CMake FetchContent.

```bash
cmake -S . -B out/build
cmake --build out/build --config Debug
```

### Output

| Target | Path |
|--------|------|
| Client | `out/build/x64-Debug/FateEngine.exe` |
| Server | `out/build/x64-Debug/FateServer.exe` |
| Tests  | `out/build/x64-Debug/fate_tests.exe` |

### Platforms

| Platform | Status |
|----------|--------|
| Windows  | Primary development target |
| iOS      | Build pipeline ready (CMake Xcode generator, GLES 3.0) |
| Android  | Future |
| Linux    | Future (server) |

## Running Tests

```bash
out/build/x64-Debug/fate_tests.exe
```

## Key Design Decisions

- **Archetype ECS** over per-entity hashmaps — contiguous memory, O(matching) queries
- **Zone arenas** — all zone memory freed in O(1) on scene exit, no fragmentation
- **Spatial grid** — power-of-two bitshift cell lookup, zero hash computation for bounded maps
- **Ticket-based chunks** — multiple systems hold chunks alive, natural multiplayer extension
- **Server-authoritative** — client predicts locally, server validates and applies all state changes
- **AOI replication** — delta-compressed entity updates scoped to player visibility radius
- **Custom reliable UDP** — 3 channels (unreliable/reliable-ordered/reliable-unordered), no TCP overhead
- **Fiber job system** — Win32 fibers with lock-free work stealing, fiber-local scratch arenas
