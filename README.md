# FateMMO Game Engine

Custom 2D MMORPG engine built in C++23, inspired by The World of Magic. Features a data-oriented archetype ECS, zone-based arena memory, spatial grid with zero-hash cell lookup, 7-state chunk streaming, and a built-in Unity-style editor.

## Tech Stack

- **Language:** C++23 (MSVC)
- **Graphics:** SDL2 + OpenGL 3.3 Core
- **Editor:** Dear ImGui (docking branch)
- **Profiler:** Tracy (on-demand)
- **Tests:** doctest
- **Build:** CMake 3.20+ with FetchContent (zero manual dependency setup)

## Architecture

```
Engine (fate_engine static library)
├── ECS          Archetype storage, contiguous SoA columns, generational handles
├── Memory       Zone arenas (O(1) reset), frame arena, scratch arenas, pool allocator
├── Spatial      Power-of-two grid (bitshift lookup) + Mueller hash fallback
├── Tilemap      7-state chunk lifecycle, ticket system, rate-limited streaming
├── Rendering    SpriteBatch (depth+texture sorted, dirty-flag skip), camera, FBO
├── Scene        Zone arena lifecycle, snapshot persistence, loading state
├── Editor       ImGui dockspace, hierarchy, inspector, tile palette, undo/redo
├── Net          AOI visibility sets, ghost entity scaffold (multiplayer groundwork)
└── Profiling    Tracy zones, arena tracking, frame marks

Game (FateEngine executable)
├── Components   27 game components (Warm/Cold tier, no RTTI)
├── Systems      Movement, Combat, MobAI (DEAR tick scaling), Spawning, Zones
├── Shared       Combat formulas, inventory, skills, guilds, parties, trade, market
└── UI           HUD bars, inventory panel, skill bar
```

## Building

All dependencies are fetched automatically via CMake FetchContent.

```bash
cmake -S . -B out/build
cmake --build out/build --config Debug
```

## Running Tests

```bash
out/build/Debug/fate_tests.exe
```

## Key Design Decisions

- **Archetype ECS** over per-entity hashmaps — contiguous memory, O(matching) queries
- **Zone arenas** — all zone memory freed in O(1) on scene exit, no fragmentation
- **Spatial grid** — power-of-two bitshift cell lookup, zero hash computation for bounded maps
- **Ticket-based chunks** — multiple systems hold chunks alive, natural multiplayer extension
- **Entity\* pointer stability** — heap-allocated entities, safe to store across frames
- **Server-authoritative design** — all game logic structured for future multiplayer validation
