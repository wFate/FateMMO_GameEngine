<div align="center">

# ⚔️ FateMMO Game Engine

[![CI](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?style=flat-square&logo=cplusplus)
![Lines of Code](https://img.shields.io/badge/LOC-135%2C400%2B-brightgreen?style=flat-square)
![Tests](https://img.shields.io/badge/tests-1%2C197-brightgreen?style=flat-square)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20iOS%20%7C%20Android%20%7C%20Linux%20%7C%20macOS-orange?style=flat-square)

**Production-grade 2D MMORPG engine built entirely in C++23.** Engineered for mobile-first landscape gameplay with a fully integrated Unity-style editor, server-authoritative multiplayer architecture, and encrypted custom networking — all from a single codebase, zero middleware.

> **135,400+ lines** across **699 files** — engine (61.6K), game (30.1K), server (22.9K), tests (20.8K)

</div>

---

## 🚀 Quick Start

The open-source engine builds out of the box with zero external dependencies — everything is fetched automatically via CMake FetchContent.

```bash
# Clone and build:
git clone https://github.com/wFate/FateMMO_GameEngine.git
cd FateMMO_GameEngine
cmake -B build
cmake --build build

# Run the demo:
./build/FateDemo          # Linux/macOS
build\Debug\FateDemo.exe  # Windows
```

The demo opens the full editor UI with a procedural tile grid — explore the dockable panels, viewport, and editor tools.

> **Full game build:** The proprietary game client, server, and tests build automatically when their source directories are present. The open-source release includes the complete engine library and editor.

---

## 🛠️ Tech Stack & Architecture

| Category | Technology & Innovation |
|----------|-----------|
| **Language** | Modern C++23 (MSVC, GCC 14, Clang 18). `std::expected`, structured bindings, fold expressions throughout |
| **Graphics RHI** | `gfx::Device` abstraction with **OpenGL 3.3 Core** & native **Metal** backends (iOS + macOS). Pipeline State Objects, typed 32-bit handles, collision-free uniform cache. Zero-batch-break SpriteBatch (10K capacity, hash-based dirty-flag sort skip), palette swap shaders, scissor clipping stack, nine-slice rendering |
| **SDF Text** | True **MTSDF font rendering** — uber-shader with 4 styles (Normal/Outlined/Glow/Shadow), offline `msdf-atlas-gen` atlas (512x512, 177 glyphs), `median(r,g,b)` + `screenPxRange` for resolution-independent edges at any zoom. World-space and screen-space APIs, UTF-8 decoding |
| **Render Pipeline** | 11-pass RenderGraph: GroundTiles -> Entities -> Particles -> **SkillVFX** -> SDFText -> DebugOverlays -> Lighting -> BloomExtract -> BloomBlur -> PostProcess -> Blit |
| **Editor** | Dear ImGui (docking) + ImGuizmo + ImPlot + imnodes. Custom dark theme (Inter font family, FreeType LightHinting). Property inspectors, visual node editors, animation editor, sprite slicing, tile painting, play-in-editor, undo/redo (200 actions) |
| **Networking** | Custom reliable UDP (`0xFA7E`), **X25519 Diffie-Hellman** + **XChaCha20-Poly1305 AEAD** encryption, IPv6 dual-stack, 3 channel types, 32-bit ACK bitfields, RTT-based retransmission, connection cookies, per-client token-bucket rate limiting |
| **Database** | PostgreSQL (libpqxx), **17 repositories**, 10 startup caches, fiber-based async dispatch, connection pool (5-50) + circuit breaker, write-ahead log (WAL) with CRC32 per entry and priority-based 4-tier flushing |
| **ECS** | Data-oriented archetype ECS, contiguous SoA memory, **52 registered components**, generational handles, prefab variants (JSON Patch), compile-time `CompId`, Hot/Warm/Cold tier classification, `FATE_REFLECT` macro with field-level metadata |
| **Memory** | Zone arenas (256 MB, O(1) reset), double-buffered frame arenas (64 MB), thread-local scratch arenas (Fleury conflict-avoidance), lock-free pool allocators, debug occupancy bitmaps, ImPlot visualization panels |
| **Audio** | SoLoud (SDL2 backend, 32 virtual voices, OGG streaming, 2D spatial audio, 3 buses, 10 game events wired) |
| **Async & Jobs** | Win32 fibers / minicoro, 4 workers, 32-fiber pool, lock-free MPMC queues, counter-based suspend/resume. Fiber-based async scene & asset loading — zero frame stalls |
| **Asset Pipeline** | Generational handles (20+12 bit), hot-reload (300ms debounced), fiber async decode + main-thread GPU upload, failed-load caching, PhysicsFS VFS, compressed textures (ETC2 / ASTC 4x4 / ASTC 8x8) with KTX1 loader, VRAM-budgeted LRU cache (512 MB) |

---

## 🗡️ Game Systems

All gameplay logic is fully server-authoritative with priority-based DB persistence, dirty-flag tracking at 95 mutation sites, spanning **46+ robust systems** across 69 shared gameplay files (16,140 LOC of pure C++ game logic — zero engine dependencies).

<details>
<summary><b>🔥 Combat, PvP & Classes</b></summary>

- **Optimistic Combat** — Attack windups play immediately to hide latency; 3-frame windup (300ms), hit frame with predicted damage text + procedural lunge offset. `CombatPredictionBuffer` ring buffer (32 slots). Server reconciles final damage.
- **Combat Core** — Hit rate with coverage system, spell resists, block, armor reduction (75% cap), 3x3 class advantage matrix (hot-reloadable JSON), crit system with class scaling.
- **Skill Manager** — 60+ skills with skillbook learning, cooldowns, cast-time system (server-ticked, CC/movement interrupts, fizzle on dead target), 4x5 hotbar, passive bonuses, resource types (Fury/Mana/None).
- **Skill VFX Pipeline** — Composable visual effects: JSON definitions with 4 optional phases (Cast/Projectile/Impact/Area). Sprite sheet animations + particle embellishments. 32 max active effects. 13 tests.
- **Status Effects** — DoTs (bleed/burn/poison), buffs, shields, invuln, transform, bewitch, source-tagged removal (Aurora buff preservation), stacking, `getExpGainBonus()`.
- **Crowd Control** — Stun/freeze/root/taunt with priority hierarchy, immunity checks.
- **PK System** — Status transitions (White -> Purple -> Red -> Black), decay timers, cooldowns, same-faction targeting restricted to PK-flagged players.
- **Honor & Rankings** — PvP honor gain/loss tables, 5-kills/hour tracking per player pair. Global/class/guild/honor/mob kills/collection leaderboards with faction filtering, 60s cache, paginated.
- **Arena & Battlefield** — 1v1/2v2/3v3 queue matchmaking, AFK detection (30s), 3-min matches, honor rewards. 4-faction PvP battlefields. `EventScheduler` FSM (2hr cycle, 10min signup). Reconnect grace (180s) for battlefield/dungeon, arena DC = forfeit.
- **Two-Tick Death** — Alive -> Dying (procs fire) -> Dead (next tick), guaranteeing kill credit without race conditions. Replicated as 3-state `deathState`.
- **Cast-Time System** — Server-ticked `CastingState`, interruptible by CC/movement, fizzles on dead targets. Replicated via delta compression.
- **Combat Leash** — Boss/mini-boss mobs reset to full HP and clear threat table after 15s idle (no incoming damage). Regular mobs unaffected.

</details>

<details>
<summary><b>✨ Progression, Items & Collections</b></summary>

- **Fixed Stats & XP** — Gray-through-red level scaling, 0%-130% XP multipliers. Base stats fixed per class to balance the meta, elevated only by gear & collections.
- **Collections System** — DB-driven passive achievement tracking across 3 categories (Items/Combat/Progression). 30 seeded definitions, 9 event trigger points. Permanent additive stat bonuses (11 stat types) with no cap. Costume rewards on completion. `SvCollectionSync` + `SvCollectionDefs` packets.
- **Enchanting & Sockets** — +1 to +15 enhancement with weighted success rates (+1-8 safe, +9-15 risky with 50%->2% curve). Protection stones (always consumed, prevents breaking). Secret bonuses at +11/+12/+15. Gold costs scale 100g -> 2M.
- **Socket System** — Accessory socketing (Ring/Necklace/Cloak), weighted stat rolls (+1: 25% -> +10: 0.5%), server-authoritative with re-socket support. 7 scroll items in DB.
- **Core Extraction** — Equipment disassembly into 7-tier crafting cores based on rarity and enchant level (+1 per 3 levels). Common excluded.
- **Crafting** — 4-tier recipe book system (Novice / Book I / II / III) with ingredient validation, level/class gating, gold costs. `RecipeCache` loaded at startup.
- **Consumables Pipeline** — 8 subtypes fully wired: HP/MP Potions, SkillBooks (class/level validated), Stat Reset (Elixir of Forgetting), Town Recall, Fate Coins (3->level*50 XP), EXP Boost Scrolls (10%/20%, 1hr, stackable tiers), Beacon of Calling (cross-scene party teleport), Soul Anchor (auto-prevent XP loss on death).
- **Costumes & Closet** — DB-driven cosmetic system. 5 rarity tiers (Common->Legendary), per-slot equipping, master show/hide toggle, paper-doll integration. 3 grant paths: mob drops (per-mob drop chance), collection rewards, shop purchase. Full replication via 32-bit delta field mask. `SvCostumeDefs`/`SvCostumeSync`/`SvCostumeUpdate` packets.

</details>

<details>
<summary><b>🌍 Economy, Social & Trade</b></summary>

- **Inventory & Bags** — 16 fixed slots + 10 equipment slots. Nested container bags (1-10 sub-slots). Auto-stacking consumables/materials. Drag-to-equip/stack/swap/destroy with full server validation. UUID v4 item instance IDs. Tooltip data synced (displayName, rarity, stats, enchant).
- **Bank & Vault** — Persistent DB storage for items and gold. Flat 5,000g deposit fee. Full `ItemInstance` metadata preserved through deposit/withdraw.
- **Market & Trade** — Peer-to-peer 2-step security trading (Lock -> Confirm -> Execute). 8 item slots + gold. Slot locking prevents market/enchant during trade. Auto-cancel on zone transition. Market with 2% tax, offline seller credit, jackpot pools, atomic buy via `RETURNING`.
- **Crafting** — 4-tier recipe book with ingredient validation and level/class gating.
- **Guilds & Parties** — Ranks, 16x16 pixel symbols, XP contributions, ownership transfer. 3-player parties with +10%/member XP bonuses and loot modes (FreeForAll/Random per-item).
- **Friends & Chat** — 50 friends, 100 blocks, online status. 7 chat channels (Map/Global/Trade/Party/Guild/Private/System), cross-faction garbling, server-side mutes (timed), profanity filtering (leetspeak normalization, 52-word list).
- **Bounties** — PvE bounty board (max 10 active, 50K-500M gold, 48hr expiry), 2% tax, guild-mate protection, 12hr guild-leave cooldown, party payout splits.
- **Economic Nonces** — `NonceManager` with random uint64 per-client, single-use replay prevention, 60s expiry. Wired into trade and market handlers. 8 tests.

</details>

<details>
<summary><b>🏰 World, AI & Dungeons</b></summary>

- **Mob AI** — Cardinal-only movement with L-shaped chase pathing, axis locking, wiggle unstuck, roam/idle phases, threat-based aggro tables, `shouldBlockDamage` callback (god mode). Wall collision is intentional — enables classic lure-and-farm positioning tactics.
- **Spawns & Zones** — `SceneSpawnCoordinator` per-scene lifecycle, `SpawnZoneCache` from DB (circle/square shapes), respawn timers, death persistence via `ZoneMobStateRepository` (prevents boss respawn exploit). `createMobEntity()` static factory.
- **Quest System** — 5 objective types (Kill/Collect/Deliver/TalkTo/PvP) with prerequisite chains, branching NPC dialogue trees (enum-based actions + conditions), max 10 active, 6 starter quests across 4 tiers.
- **Instanced Dungeons** — Per-party ECS worlds, 10-minute timers, boss rewards, daily tickets, invite system (30s timeout), celebration phase. Reconnect grace (180s). Event locks prevent double-enrollment. Per-minute chat timer.
- **Aurora Gauntlet** — 6-zone PvP with hourly faction-rotation buff (+25% ATK/EXP), wall-clock `hour%4` rotation. Aether Stone + 50K gold entry. Aether world boss (Lv55, 150M HP, 36hr respawn) with 23-item loot table. Zone scaling Lv10->55. Death ejects to Town.
- **Pet System** — Leveling, rarity-tiered stats, XP sharing (50%), server-authoritative auto-looting (0.5s ticks, 64px radius, ownership+party aware). `PetDefinitionCache` from DB.
- **Loot Pipeline** — Server rolls -> ground entities -> spatial replication -> pickup validation -> 120s despawn. Per-player damage attribution, live party lookup at death, strict purge on DC/leave.
- **NPC System** — 9 NPC types: Shop, Bank, Teleporter (with item/gold/level costs), Guild, Dungeon, Arena, Battlefield, Story (branching dialogue), QuestGiver. Proximity validation on all interactions.

</details>

---

## 🎨 Retained-Mode UI System

Custom data-driven UI engine with **viewport-proportional scaling** (`screenHeight / 900.0f`) for pixel-perfect consistency across all devices. Anchor-based layout (12 presets + percentage sizing), JSON screen definitions, 9-slice rendering, two-tier color theming, virtual `hitTest` overrides for mobile-optimized touch targets.

- **53 Widget Types:** 21 Engine-Generic (Panels, ScrollViews, ProgressBars, Checkboxes, ConfirmDialogs, NotificationToasts, LoginScreen) and **26 Game-Specific** (DPad, SkillArc with 4-page C-arc, FateStatusBar, InventoryPanel with paper doll, CostumePanel, CollectionPanel, and more) + 6 internal layout primitives.
- **10 JSON Screens & 28 Theme Styles:** Parchment, HUD dark, dialog, tab, scrollbar themes. Full serialization of layout properties, fonts, colors, and inline style overrides. Ctrl+S dual-save (build + source dir).
- **Paper Doll System:** `PaperDollCatalog` singleton with JSON-driven catalog — body/hairstyle/equipment sprites per gender, direction-aware rendering, texture caching, editor preview. Used in game HUD, character select, and character creation.
- **Zero-ImGui Game Client:** All HUD, nameplates, and floating text render via SDFText + SpriteBatch. ImGui is compiled out of shipping builds entirely.
- **95+ UI tests.**

---

## 🔒 Server & Networking

**Headless 20 Hz server** (`FateServer`) with max 2,000 concurrent connections. **36 handler files**, **17 DB repositories**, **10 startup caches**. Every game action is server-validated.

<details>
<summary><b>🔐 Transport & Encryption</b></summary>

| Property | Value |
|----------|-------|
| Protocol | Custom reliable UDP (`0xFA7E`), Win32 + POSIX |
| Encryption | **X25519 key exchange** + **XChaCha20-Poly1305 AEAD** (24-byte nonce, 16-byte tag) |
| IPv6 | Dual-stack with IPv4 fallback (DNS64/NAT64 — iOS App Store mandatory) |
| Channels | Unreliable (movement), ReliableOrdered (critical), ReliableUnordered (reliable no ordering) |
| Packets | 18-byte header, 32-bit ACK bitfield, RTT estimation (EWMA), retransmission delay `max(0.2s, 2*RTT)` |
| Rate Limiting | Per-client, per-packet-type token buckets (23 packet types configured), violation decay, auto-disconnect |
| Anti-Replay | Economic nonce system (trade/market), connection cookies (HMAC FNV-1a, 10s time-bucketed) |
| Auto-Reconnect | `ReconnectPhase` state machine, exponential backoff (1s->30s cap), 60s total timeout |

</details>

<details>
<summary><b>📡 Replication & AOI</b></summary>

- **Area of Interest** — Spatial-hash culling (128px cells), 640px activation / 768px deactivation (hysteresis). Scene-filtered. Optional `visibilityFilter` callback (GM invisibility).
- **Delta Compression** — 32-bit field mask (17 fields: position, anim, HP, moveState, statusEffects, deathState, casting, target, level, faction, equipVisuals, pkStatus, honorRank, costumeVisuals). Only dirty fields serialized.
- **Batched Updates** — Multiple entity deltas packed into single `SvEntityUpdateBatch` packets (~90% header overhead reduction vs per-entity packets).
- **Tiered Frequency** — Near 20 Hz / Mid 7 Hz / Far 4 Hz / Edge 2 Hz. HP changes always sent regardless of tier.
- **Ghost Lifecycle** — Robust enter/leave/destroy pipeline with `recentlyUnregistered_` bridge, `processDestroyQueue()`, full disconnect cleanup.

</details>

<details>
<summary><b>💾 Persistence & Database</b></summary>

| Layer | Detail |
|-------|--------|
| **WAL** | Binary write-ahead log with CRC32 per entry, `fflush` after every append. Journals gold/item/XP mutations. Replay on crash recovery |
| **Circuit Breaker** | 3-state (Closed -> Open -> HalfOpen), 5 failures -> 30s cooldown. WAL queues writes during outage |
| **Priority Flushing** | 4 tiers: IMMEDIATE (0s — gold/inventory/trades), HIGH (5s — level-ups/PK), NORMAL (60s — position), LOW (300s — pet/bank). 1s dedup, 10/tick drain, 5-min auto-save safety net |
| **Dirty Flags** | `PlayerDirtyFlags` at **95 mutation sites**. Disconnect + auto-save bypass with `forceSaveAll=true`. Async error re-dirties for retry |
| **Connection Pool** | Thread-safe (min 5, max 50, +10 overflow). Fiber-based async dispatch. `PlayerLockMap` with `shared_ptr<mutex>` for concurrent mutation serialization |

</details>

<details>
<summary><b>🛡️ GM Command System</b></summary>

`GMCommandRegistry` with `AdminRole` enum (Player/GM/Admin). 17 commands across 6 categories: Player Management (kick/ban/permaban/unban/mute/unmute/whois/setrole), Teleportation (tp/tphere/goto), Spawning (spawnmob), Economy (additem/addgold/setlevel/addskillpoints), GM Tools (announce/dungeon/invisible/god), Help (admin). Ban/unban fully DB-wired with timed expiry. Invisibility uses replication visibility filter. God mode blocks damage at all 3 paths.

</details>

---

## ⚙️ Editor (Dear ImGui)

Custom polished dark theme — Inter font family (14px body, 16px SemiBold headings, 12px metadata) via FreeType LightHinting.

<details>
<summary><b>🎯 Core Editor Features</b></summary>

- **Entity Hierarchy** — Grouped by name+tag, color-coded (player/ground/obstacle/mob/boss), error badges, tree indentation guides.
- **Live Inspector** — Edit all 52 component types live with **full undo/redo**. Sprite preview thumbnails. Reflection-driven generic fallback via `FATE_REFLECT`.
- **Scene Interaction** — Click to select (depth-priority), drag to move, sticky selection. Ground tiles locked (inspect-only).
- **Create / Delete / Duplicate** — Menu + keyboard shortcuts, deep copy via JSON serialization, locked entity protection.
- **8 Tile Tools** — Move (W), Resize (E), Rotate (R), Paint (B), Erase (X), Flood Fill (G), RectFill (U), LineTool (L). All tool-paused-only with compound undo.
- **Play-in-Editor** — Green/Red Play/Stop buttons. Full ECS snapshot + restore round-trip. Camera preserved. Ctrl+S blocked during play.
- **200-action Undo/Redo** — Tracks moves, resizes, deletes, duplicates, tile paint, all inspector field edits. Handle remap after delete+undo.
- **Input Separation** — Clean priority chain: Paused = ImGui -> Editor -> nothing. Playing = ImGui (viewport-excluded) -> UI focused node -> Game Input. Tool shortcuts paused-only, Ctrl shortcuts always.
- **Device Profiles** — 29 device presets (iPhone SE through iPhone 17 Pro, iPad Air/Pro, Pixel 9, Samsung S24/S25, desktop resolutions, ultrawide). Safe area overlay with notch/Dynamic Island insets.

</details>

<details>
<summary><b>🧩 Panels & Browsers</b></summary>

- **Asset Browser** — Unity-style: golden folder icons, file type cards with colored accent strips, sprite thumbnails with checkerboard, breadcrumb nav, search, lazy texture cache, drag-and-drop, context menu (Place in Scene / Open in Animation Editor / Open in VS Code / Show in Explorer).
- **Animation Editor** — Sprite Sheet Slicer (grid overlay, cell assignment, quick templates, `.meta.json` output) + legacy Individual Frame mode. Hit frame editor, preview with play/pause/step, 3-direction authoring -> 4-direction runtime.
- **Tile Palette** — Recursive subdirectory scan, scrollable grid, brush size (1-5), 4-layer dropdown (Ground/Detail/Fringe/Collision), layer visibility toggles.
- **Dialogue Node Editor** — Visual node-based dialogue trees via imnodes. Speaker/text nodes, choice pins, JSON save/load, node position persistence.
- **Paper Doll Panel** — Live composite preview with Browse-to-assign workflow for body/hair/equipment sprites.
- **+ 7 more** — Log Viewer, Memory Panel (arena/pool/frame visualization), Command Console, Network Panel, Post-Process Panel, Project Browser, Scene Management.

</details>

---

## ⚡ Building & Targets

All core dependencies are fetched automatically via CMake FetchContent — **zero manual installs required** for the engine and demo.

```bash
# Engine + Demo (open-source, no external deps):
cmake -B build
cmake --build build

# Full game build (requires vcpkg for OpenSSL, libpq, libsodium):
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug

# Shipping build (strips editor, release optimizations):
cmake --preset x64-Shipping
cmake --build --preset x64-Shipping
```

### Output Targets

| Target | Description | Availability |
|--------|-------------|--------------|
| **fate_engine** | Core engine static library | Always (open-source) |
| **FateDemo** | Minimal demo with editor UI | Open-source build |
| **FateEngine** | Full game client | When `game/` sources present |
| **FateServer** | Headless authoritative server | When `server/` + PostgreSQL present |
| **fate_tests** | 1,197 unit tests (doctest) | When `tests/` sources present |

### Platform Matrix

| Platform | Status | Details |
|----------|--------|---------|
| **Windows** | Primary | MSVC (VS 2025), primary development target |
| **macOS** | Supported | CMake, full Metal rendering, minicoro fibers, ProMotion 120fps |
| **iOS** | Pipeline Ready | CMake Xcode generator, Metal/GLES 3.0, CAMetalLayer, TestFlight script, DNS64/NAT64 |
| **Android** | Pipeline Ready | Gradle + NDK r27, SDLActivity, `./gradlew installDebug` |
| **Linux** | CI Verified | GCC 14, Clang 18 — builds green on every push |

---

## 🧪 Testing

The engine maintains exceptional stability through **1,197 test cases** across **168 test files**, powered by `doctest`.

```bash
# Run all tests:
./build/Debug/fate_tests.exe

# Target specific suites:
./build/Debug/fate_tests.exe -tc="Death Lifecycle"
./build/Debug/fate_tests.exe -tc="PacketCrypto"
./build/Debug/fate_tests.exe -tc="PersistenceQueue*"
./build/Debug/fate_tests.exe -tc="SkillVFX*"
./build/Debug/fate_tests.exe -tc="CollisionGrid*"
```

Coverage spans: combat formulas, encryption/decryption, entity replication, inventory operations, skill systems, quest progression, economic nonces, arena matchmaking, dungeon lifecycle, VFX pipeline, compressed textures, UI layout, collision grids, async asset loading, and more.

---

## 📐 Architecture at a Glance

```
engine/                    # 61,600 LOC — Core engine (20 subsystems)
 render/                   #   Sprite batching, SDF text, lighting, bloom, paper doll, VFX
 net/                      #   Custom UDP, AEAD crypto, replication, AOI, interpolation
 ecs/                      #   Archetype ECS, 52 components, reflection, serialization
 ui/                       #   53 widgets, JSON screens, themes, viewport scaling
 editor/                   #   ImGui editor, undo/redo, animation editor, asset browser
 tilemap/                  #   Chunk VBOs, texture arrays, Blob-47 autotile, 4-layer
 scene/                    #   Async loading, versioning, prefab variants
 asset/                    #   Hot-reload, fiber async, LRU VRAM cache, compressed textures
 input/                    #   Action map, touch controls, 6-frame combat buffer
 audio/                    #   SoLoud, 3-bus, spatial audio, 10 game events
 job/                      #   Fiber system, MPMC queue, scratch arenas
 memory/                   #   Zone/frame/scratch arenas, pool allocators
 spatial/                  #   Fixed grid, spatial hash, collision bitgrid
 core/                     #   Structured errors, Result<T>, CircuitBreaker
 particle/                 #   CPU emitters, per-particle lifetime/color lerp
 platform/                 #   Device info, RAM tiers, thermal polling
 profiling/                #   Tracy integration, spdlog, rotating file sink
 vfx/                      #   SkillVFX player, JSON definitions, 4-phase compositing
 vfs/                      #   PhysicsFS, ZIP mount, overlay priority
 telemetry/                #   Metric collection, JSON flush, HTTPS stub

game/                      # 30,100 LOC — Game logic layer
 shared/                   #   69 files, 16,140 LOC of pure gameplay (zero engine deps)
   combat_system           #   Hit rate, armor, crits, class advantage, PvP balance
   skill_manager           #   60+ skills, cooldowns, cast times, resource types
   mob_ai                  #   Cardinal movement, threat, leash, L-shaped chase
   status_effects          #   DoTs, buffs, shields, source-tagged removal
   inventory               #   16 slots, equipment, bags, stacking, UUID instances
   trade_manager           #   2-step security, slot locking, atomic transfer
   arena_manager           #   1v1/2v2/3v3, matchmaking, AFK detection, honor
   gauntlet                #   Event scheduler, divisions, wave spawning, MVP
   ...                     #   +27 more systems (guild, party, pet, crafting, etc.)
 components/               #   ECS component wrappers for all shared systems
 systems/                  #   11 ECS systems (combat, render, movement, mob AI, spawn...)
 data/                     #   Paper doll catalog, NPC definitions, quest data

server/                    # 22,900 LOC — Headless authoritative server
 handlers/                 #   36 packet handler files (split from monolith)
 db/                       #   17 repositories, 5 definition caches, WAL, pool, dispatcher
 cache/                    #   Item/loot/recipe/pet/costume/collection caches
 auth/                     #   TLS auth server (bcrypt, starter equipment, visual preview)
 *.h/.cpp                  #   ServerApp, SpawnManager, DungeonManager, RateLimiter, GM commands

tests/                     # 20,800 LOC — 1,197 test cases across 168 files
```

---

<div align="center">

<br>

<img src="https://img.shields.io/badge/%E2%9A%94%EF%B8%8F_Forged_by-Caleb_Kious-8B5CF6?style=for-the-badge&logoColor=white" alt="Forged by Caleb Kious" />

<br>

<sub>**Engine Architecture** | **Networking & Crypto** | **Server Systems** | **Editor Tooling** | **Cross-Platform** | **All Game Logic**</sub>

<br><br>

<img src="https://img.shields.io/badge/C%2B%2B-135%2C400%2B_lines-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++ LOC" />
<img src="https://img.shields.io/badge/files-699-2ea44f?style=flat-square" alt="Files" />
<img src="https://img.shields.io/badge/tests-1%2C197_passing-2ea44f?style=flat-square" alt="Tests" />
<img src="https://img.shields.io/badge/platforms-5-orange?style=flat-square" alt="Platforms" />
<img src="https://img.shields.io/badge/game_systems-46%2B-blueviolet?style=flat-square" alt="Game Systems" />
<img src="https://img.shields.io/badge/UI_widgets-53-ff69b4?style=flat-square" alt="UI Widgets" />
<img src="https://img.shields.io/badge/DB_repositories-17-informational?style=flat-square" alt="DB Repos" />

<br><br>

[![GitHub](https://img.shields.io/badge/GitHub-wFate-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/wFate)

<br>

</div>
