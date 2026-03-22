# FateMMO Game Engine

[![CI](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue)
![Lines of Code](https://img.shields.io/badge/LOC-80%2C000%2B-brightgreen)
![Tests](https://img.shields.io/badge/tests-844-brightgreen)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20iOS%20%7C%20Android%20%7C%20Linux-orange)

Custom 2D MMORPG engine built in C++23. Features a data-oriented archetype ECS, zone-based arena memory, spatial grid with zero-hash cell lookup, server-authoritative netcode with AOI replication, AEAD-encrypted UDP transport, and a built-in editor with visual node editors, tile tools, and play-in-editor snapshots.

> **80,000+ lines** across **450+ files** — engine (25K), game (28K), server (13K), tests (14K)

---

## Tech Stack

| Category | Technology |
|----------|-----------|
| **Language** | C++23 (MSVC, GCC 13, Clang 17) |
| **Graphics** | SDL2 + OpenGL 3.3 Core / GLES 3.0 (iOS/Android) |
| **Editor** | Dear ImGui (docking) + ImGuizmo + ImPlot + imnodes |
| **Networking** | Custom reliable UDP, AEAD encryption (XChaCha20-Poly1305), IPv6 dual-stack |
| **Auth** | TLS (OpenSSL), bcrypt password hashing, session tokens |
| **Database** | PostgreSQL (libpqxx), connection pool + circuit breaker, write-ahead log, priority-based flush |
| **Audio** | SoLoud (SDL2 backend, 32 virtual voices, OGG streaming, 2D spatial) |
| **VFS** | PhysicsFS (mount/overlay for asset packaging) |
| **Profiler** | Tracy (on-demand zones, frame marks, arena tracking) |
| **Logging** | spdlog (per-subsystem, rotating file sink, Android logcat ready) |
| **Tests** | doctest — 844 unit tests + 10 end-to-end scenario tests |
| **Build** | CMake 3.20+ with FetchContent (zero manual dependency setup) |
| **CI/CD** | GitHub Actions — MSVC + GCC 13 + Clang 17 matrix, headless OpenGL via Xvfb |

---

## Game Systems

41 gameplay systems — all server-authoritative with priority-based DB persistence.

<details>
<summary><b>Combat & PvP</b></summary>

- **Combat System** — hit rate with coverage, spell resist, block, armor reduction, class advantage matrix
- **PK System** — status transitions (White → Purple → Red → Black), decay timers, cooldowns
- **Honor System** — PvP honor gain/loss tables, 5-kills/hour tracking per player pair
- **Arena** — 1v1 / 2v2 / 3v3 queue-based matchmaking, AFK detection, 3-min matches, honor rewards
- **Battlefield** — 4-faction PvP, per-faction kill tracking, EventScheduler-driven event cycles
- **Two-Tick Death** — Alive → Dying (on-death procs) → Dead (next tick), prevents lost kill credit
- **Optimistic Combat** — attack animations play immediately, server confirms damage numbers
- **Elemental Resists** — 6 types (Fire/Water/Poison/Lightning/Void/Magic), 75% cap

</details>

<details>
<summary><b>Character Progression</b></summary>

- **Character Stats** — HP/MP/XP/level, stat calc with VIT multiplier, damage formulas, fury/mana
- **Skill Manager** — 60 skills with learning, cooldowns, 4x5 skill bar, passive bonuses
- **Enchant System** — +1 to +15 enhancement with success rates, break mechanic, protection scrolls
- **Socket System** — accessory socketing with weighted probability rolls (+1: 25% ... +10: 0.5%)
- **Stat Enchant** — accessory enchanting, 6-tier roll table
- **Core Extraction** — equipment disassembly into 7-tier crafting cores
- **Consumables** — HP/MP potions with server-authoritative application, 5s cooldown, WAL logged
- **Bags** — nested containers (bag item in inventory slot holds up to 10 sub-items inside it)
- **XP Calculator** — gray-through-red level scaling, 0%-130% XP multipliers
- **HP/MP Regen** — server-authoritative tick (HP: 1%/10s + equip bonus, MP: WIS/5s for mana classes)

</details>

<details>
<summary><b>Economy & Social</b></summary>

- **Inventory** — 15 fixed slots, 10 equipment slots, drag-and-drop, soulbound, trade locking
- **Bank/Vault** — deposit/withdraw gold (2% fee) and items (stacking), 30 slots, DB-persisted
- **Market** — listings with 2% tax, jackpot pool, merchant pass, offline seller credit
- **Trade** — peer-to-peer with two-step security (Lock → Confirm → Execute)
- **Crafting** — recipe system with 4 book tiers, ingredient validation, level/class gates
- **Guild** — ranks, 16x16 pixel symbols, XP contribution, member management
- **Party** — 3-player parties, +10%/member XP bonus, loot modes (FreeForAll/Random)
- **Friends** — 50 friends, 100 blocks, online status tracking
- **Chat** — 7 channels (Map/Global/Trade/Party/Guild/Private/System), cross-faction garbling
- **Bounty** — PvE bounty board, 48hr expiry, 2% tax, guild-mate protection, party payout split
- **Rankings** — paginated leaderboards (global, per-class, honor, guild), 60s DB cache

</details>

<details>
<summary><b>World & AI</b></summary>

- **Mob AI** — cardinal movement, L-shaped chase, threat-based aggro (top damager holds)
- **Spawn System** — region-based with editor controls, death detection, respawn timers, zone containment
- **Quest System** — 5 objective types (Kill/Collect/Deliver/TalkTo/PvP), prerequisite chains
- **NPC System** — composable roles (quest/shop/trainer/bank/guild/teleporter/story), branching dialogue
- **Pet System** — leveling, rarity-tiered stats, XP sharing (50%), equip/unequip with stat bonuses, auto-loot
- **Gauntlet** — wave survival PvPvE with 3 divisions, event scheduler, matchmaking
- **Faction** — 4 factions (Xyros/Fenor/Zethos/Solis), same-faction PvP rules, chat garbling
- **Loot Pipeline** — server rolls loot → ground entities → replication → pickup → despawn (120s)
- **Instanced Dungeons** — per-party ECS worlds, 30-min timeout, boss rewards, honor

</details>

---

## Architecture

```
Engine (fate_engine static library)
+-- ECS          Archetype SoA storage, generational handles, reflection, prefab variants (JSON Patch)
+-- Memory       Zone arenas (256 MB, O(1) reset), frame arena, scratch arenas, pool allocator
+-- Spatial      Power-of-two grid (bitshift lookup) + Mueller hash fallback
+-- Tilemap      7-state chunk lifecycle, ChunkRenderer (pre-built VBOs), GL_TEXTURE_2D_ARRAY
+-- Rendering    SpriteBatch (palette swap), 10-pass render graph, SDF text (MTSDF), lighting, bloom
+-- Scene        Zone arena lifecycle, snapshot persistence, loading state
+-- Editor       ImGui dockspace, hierarchy, inspector, tile tools (fill/rect/line/stamp),
|                asset browser (thumbnails/search/navigation), dialogue node editor (imnodes),
|                play-in-editor (ECS state snapshot/restore), undo/redo (CompoundCommand)
+-- Net          Reliable UDP, AEAD encryption, IPv6 dual-stack, AOI replication, delta compression
|                Auto-reconnect (exponential backoff), rate limiting, connection cookies
+-- Input        ActionMap (23 actions), 6-frame buffer, touch injection API
+-- Asset        Hot-reload (file watcher, 300ms debounce), generational handles, LRU texture cache
+-- Audio        SoLoud (SFX preload, OGG music stream, crossfade, 2D spatial, volume buses)
+-- VFS          PhysicsFS wrapper (mount/overlay/read for asset packaging and mods)
+-- Job          Win32 fiber / minicoro (cross-platform), lock-free MPMC queue
+-- Platform     Device info (RAM tiers, VRAM budgets, thermal state), SDL lifecycle
+-- Particle     CPU emitters, gravity, lifetime, color lerp
+-- Profiling    Tracy zones, arena tracking, frame marks

Game (FateEngine executable, EDITOR_BUILD defined)
+-- Components   38+ game components (Hot/Warm/Cold tier, zero RTTI)
+-- Systems      Movement, Combat, MobAI, Spawning, Quests, Zones, NPC interaction
+-- Shared       41 game systems -- combat, skills, inventory, parties, guilds, trade, market,
|                arena, battlefield, crafting, pets, bounty, gauntlet, honor, PK, chat
+-- Prediction   Optimistic combat feedback (immediate attack animations, prediction buffer)
+-- UI           HUD, inventory, skill bar, shop, quest log, chat, login, death overlay, touch

Server (FateServer headless, 20 Hz)
+-- Auth         TLS login/registration, bcrypt, session tokens, AEAD key exchange
+-- DB           13 PostgreSQL repositories, fiber-based async dispatch, connection pool,
|                circuit breaker, write-ahead log, priority-based flush (IMMEDIATE/HIGH/NORMAL/LOW),
|                dirty-flag gating (95 mutation sites), 1s dedup enqueuePersist, staggered auto-save
+-- Security     Rate limiting, nonce manager, target validation (AOI + faction + party + PK)
+-- Cache        Item (748) + Loot (72) + Mob (73) + Skill (60) + Scene + Recipe + Pet caches
+-- Dungeons     Instanced per-party ECS worlds, boss rewards, 30-min timeout
+-- App          Two-tick death, mob spawning, loot pipeline, gauntlet, arena, battlefield

Tests (844 unit + 10 scenario)
+-- Unit         Combat, networking, protocol, inventory, death, PvP, arena, audio, spatial,
|                tile tools, prefab variants, persistence priority, dirty tracking
+-- Integration  Replication, server integration, gameplay
+-- Scenario     End-to-end TestBot (TLS auth + UDP, 10 tests against live server)
```

---

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

<details>
<summary><b>iOS</b></summary>

```bash
cd ios && ./build.sh debug build         # Generate Xcode project + build
cd ios && ./build.sh debug device        # Build + deploy to connected iPhone
cd ios && ./build.sh release testflight  # Archive + upload to TestFlight
```

</details>

<details>
<summary><b>Android</b></summary>

```bash
cd android && ./gradlew installDebug   # Build + install on connected device
cd android && ./gradlew bundleRelease  # Build AAB for Google Play
```

</details>

---

## Running Tests

```bash
# All unit tests (headless, no server required):
build/Debug/fate_tests.exe

# Specific test suite:
build/Debug/fate_tests.exe -tc="Death Lifecycle"
build/Debug/fate_tests.exe -tc="PvP Target Validation"
build/Debug/fate_tests.exe -tc="PacketCrypto"
build/Debug/fate_tests.exe -tc="ArenaManager"
build/Debug/fate_tests.exe -tc="PersistenceQueue*"
```

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| **Archetype ECS** | Contiguous SoA memory, O(matching) queries — no per-entity hashmaps |
| **Zone arenas** | All zone memory freed in O(1) on scene exit, zero fragmentation |
| **Spatial grid** | Power-of-two bitshift cell lookup, zero hash computation for bounded maps |
| **Server-authoritative** | Client sends intent only, server validates and applies all state changes |
| **Priority DB flush** | IMMEDIATE for trades/gold, HIGH for level-ups, 5-min auto-save as safety net |
| **Dirty-flag gating** | 95 mutation sites flagged, saves skip unchanged sections (position, stats, inventory, etc.) |
| **Two-tick death** | Alive → Dying → Dead prevents lost kill credit from same-frame procs |
| **AEAD encryption** | XChaCha20-Poly1305 on all game traffic, per-session keys, sequence-as-nonce |
| **IPv6 dual-stack** | `sockaddr_storage`, AF_INET6 with IPv4 fallback, iOS App Store compliant |
| **Chunk VBO rendering** | Pre-built per-chunk VAO/VBO, GL_TEXTURE_2D_ARRAY for zero tile bleeding |
| **Prefab variants** | JSON Patch (RFC 6902) inheritance — variants store only diffs from base prefab |
| **16-field delta compression** | AOI-scoped entity updates, only dirty fields sent, HP bypasses throttling |
| **Custom reliable UDP** | 3 channels (unreliable / reliable-ordered / reliable-unordered), no TCP overhead |
| **Fiber job system** | Win32 fibers / minicoro cross-platform, lock-free work stealing |
| **Optimistic combat** | Attack animations play immediately, server confirms damage numbers |
| **Write-ahead log** | Binary WAL with CRC32 journals gold/item/XP mutations for crash recovery |

---

## Contributor

**Caleb Kious** — Engine Architecture, Networking, Server Systems, Cross-Platform Tooling

[![GitHub](https://img.shields.io/badge/GitHub-wFate-181717?style=flat-square&logo=github)](https://github.com/wFate)
