<div align="center">

# ⚔️ FateMMO Game Engine

[![CI](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?style=flat-square&logo=cplusplus)
![Lines of Code](https://img.shields.io/badge/LOC-101%2C500%2B-brightgreen?style=flat-square)
![Tests](https://img.shields.io/badge/tests-1%2C093-brightgreen?style=flat-square)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20iOS%20%7C%20Android%20%7C%20Linux%20%7C%20macOS-orange?style=flat-square)

Custom 2D MMORPG engine built in C++23. Engineered for mobile-first landscape gameplay with a full built-in Unity-style editor for rapid iteration, scene building, and live state snapshots. All game systems are strictly server-authoritative, powered by a robust backend architecture.

> **101,500+ lines** across **582 files** — engine (38.5K), game (30.2K), server (16.5K), tests (16K)

</div>

---

## 🛠️ Tech Stack & Architecture

| Category | Technology & Innovation |
|----------|-----------|
| **Language** | Modern C++23 (MSVC, GCC 13, Clang 17) |
| **Graphics RHI** | `gfx::Device` abstraction with GL 3.3 Core & native **Metal** backends, zero-batch-break SpriteBatch, and true MTSDF font rendering. |
| **Editor** | Dear ImGui (docking) + ImGuizmo + ImPlot + imnodes. Built-in property inspectors, visual node editors, sprite slicing, and tile painting. |
| **Networking** | Custom reliable UDP, X25519 Diffie-Hellman + XChaCha20-Poly1305 AEAD, IPv6 dual-stack. AOI delta compression with bitmask batching. |
| **Database** | PostgreSQL (libpqxx), async fiber dispatch, connection pool + circuit breaker, write-ahead log (WAL) with priority-based flushing. |
| **ECS** | Data-oriented archetype ECS, contiguous SoA memory, 54 registered components, generational handles, prefab variants (JSON Patch). |
| **Memory** | Zone arenas (256 MB, O(1) reset), thread-local scratch arenas, lock-free pool allocators, and strict memory safety tracking. |
| **Audio** | SoLoud (SDL2 backend, 32 virtual voices, OGG streaming, 2D spatial audio). |
| **Async & Jobs** | Win32 fibers / minicoro, lock-free MPMC queues, fiber-based async scene & asset loading. Zero frame stalls. |

---

## 🗡️ Game Systems

All gameplay logic is fully server-authoritative with priority-based DB persistence, spanning across 46+ robust systems.

<details>
<summary><b>🔥 Combat, PvP & Classes</b></summary>

- **Optimistic Combat** — Attack windups play immediately to hide latency; server reconciles and confirms damage.
- **Combat Core** — Hit rate with coverage, spell resists, block, armor reduction (75% cap), and 3x3 class advantage matrices.
- **PK System** — Status transitions (White → Purple → Red → Black), decay timers, cooldowns.
- **Honor & Rankings** — PvP honor gain/loss tables, 5-kills/hour tracking per player pair. Global, class, and guild leaderboards.
- **Arena & Battlefield** — 1v1/2v2/3v3 queue matchmaking, AFK detection. 4-faction PvP battlefields driven by an EventScheduler.
- **Two-Tick Death** — Alive → Dying (procs) → Dead (next tick), guaranteeing kill credit without race conditions.
- **Cast-Time System** — Server-ticked CastingState, interruptible by CC/movement, fizzles on dead targets.

</details>

<details>
<summary><b>✨ Progression, Items & Collections</b></summary>

- **Fixed Stats & XP** — Gray-through-red level scaling, 0%-130% XP multipliers. Stats are fixed per class to balance the meta, elevated by gear & collections.
- **Skill Manager** — 60 skills with learning via skillbooks, 4x5 hotbar, passive bonuses, and an extensive Skill VFX pipeline.
- **Collections System** — Passive achievement tracking (Items, Combat, Progression) granting permanent additive stat bonuses and Costume rewards.
- **Enchanting & Sockets** — +1 to +15 enhancement with weighted success rates. Accessory socketing and drag-and-drop stat scrolls.
- **Core Extraction** — Equipment disassembly into 7-tier crafting cores based on rarity and enchant level.
- **Consumables Pipeline** — Soul Anchors (auto-prevent XP loss), Fate Coins, EXP Boost Scrolls, Beacon of Calling (cross-scene party teleport), Elixir of Forgetting (skill resets).
- **Costumes & Closet** — DB-driven cosmetic system. 5 rarity tiers, per-slot equipping, paper-doll integration, mob costume drop chances.

</details>

<details>
<summary><b>🌍 Economy, Social & Trade</b></summary>

- **Inventory & Bags** — 16 fixed slots + 10 equipment slots. Nested containers, drag-to-equip/stack/swap, full server validation.
- **Bank & Vault** — Persistent DB storage for items and gold. Flat-fee deposits.
- **Market & Trade** — Peer-to-peer 2-step security trading (Lock → Confirm → Execute). Market listings with tax, offline credit, and jackpot pools.
- **Crafting** — 4-tier recipe book system with ingredient validation and level/class gating.
- **Guilds & Parties** — Ranks, 16x16 pixel symbols, XP contributions. 3-player parties with XP bonuses and loot modes (FreeForAll/Random).
- **Friends & Chat** — 7 channels (cross-faction garbling), friend/block lists, profanity filtering, server-side mutes.
- **Bounties** — PvE bounty board with 48hr expiries, guild-mate protection, and party payout splits.

</details>

<details>
<summary><b>🏰 World, AI & Dungeons</b></summary>

- **Mob AI** — Cardinal movement, L-shaped chase, threat-based aggro tables, and combat leashing (boss resets after 15s idle).
- **Spawns & Zones** — Region-based editor controls, respawn timers, and zone containment.
- **Quest System** — 5 objective types (Kill/Collect/Deliver/TalkTo/PvP) with prerequisite chains and branching NPC dialogue trees.
- **Instanced Dungeons** — Per-party ECS worlds, 10-minute timers, boss rewards, daily tickets, and 180s reconnect grace periods.
- **Aurora Gauntlet** — 6-zone PvP with hourly faction-rotation buffs, entry item costs, and Aether world bosses.
- **Pet System** — Leveling, rarity-tiered stats, XP sharing, and server-authoritative auto-looting (0.5s ticks).
- **Loot Pipeline** — Server rolls → ground entities → spatial replication → pickup validation → 120s despawn.

</details>

---

## 🎨 Retained-Mode UI System

Custom data-driven UI engine designed for cross-platform HUDs and menus, featuring **viewport-proportional scaling** to perfectly match mobile and desktop proportions.

- **52 Widget Types:** Includes Engine-Generic (Panels, ScrollViews, ProgressBars) and Game-Specific (DPad, SkillArc, FateStatusBar, InventoryPanel, CostumePanel).
- **JSON Screens & Themes:** 12 defined JSON screens over 28 rich theme styles. Complete serialization of layout properties, fonts, and colors.
- **Character Select Overhaul:** Highly customizable editor layout (70+ properties), with a **Paper Doll Sprite Preview** blending character body, armor, hat, and weapon textures live.
- **Virtual hitTest:** Arc and circular widgets cleanly override touch geometry to optimize mobile tap targets independently of rendering rects.

---

## ⚙️ Building & Execution

All dependencies are fetched automatically via CMake FetchContent. vcpkg is utilized for OpenSSL, libpq, and libsodium on Windows.

```bash
# First time only (Windows):
C:\vcpkg\vcpkg.exe install openssl:x64-windows libpq:x64-windows libsodium:x64-windows

# Build:
cmake -S . -B build
cmake --build build --config Debug
```

### Output Targets

| Target | Path |
|--------|------|
| **Client** | `out/build/x64-Debug/FateEngine.exe` (or `build/Debug/...`) |
| **Server** | `out/build/x64-Debug/FateServer.exe` |
| **Tests**  | `out/build/x64-Debug/fate_tests.exe` |

### Platform Matrix

| Platform | Status |
|----------|--------|
| **Windows** | Developer Build. |
| **macOS**   | Supported (CMake, Metal rendering, minicoro fibers). |
| **iOS**     | Build pipeline ready (CMake Xcode generator, Metal/GLES 3.0, TestFlight script). |
| **Android** | Build pipeline ready (Gradle + NDK r27, SDLActivity, `./gradlew installDebug`). |
| **Linux**   | CI builds (GCC 13, Clang 17), target environment for Headless Server. |

---

## 🧪 Testing

The engine maintains exceptional stability through an exhaustive test suite powered by `doctest`.

```bash
# Run all 1,093 unit & scenario tests (headless, no server required):
out/build/x64-Debug/fate_tests.exe

# Target specific suites:
out/build/x64-Debug/fate_tests.exe -tc="Death Lifecycle"
out/build/x64-Debug/fate_tests.exe -tc="PacketCrypto"
out/build/x64-Debug/fate_tests.exe -tc="PersistenceQueue*"
```

---

<div align="center">

<br>

<img src="https://img.shields.io/badge/%E2%9A%94%EF%B8%8F_Forged_by-Caleb_Kious-8B5CF6?style=for-the-badge&logoColor=white" alt="Forged by Caleb Kious" />

<br>

<sub>**Engine Architecture** · **Networking** · **Server Systems** · **Editor Tooling** · **Cross-Platform**</sub>

<br><br>

<img src="https://img.shields.io/badge/C%2B%2B-101%2C500%2B_lines-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++ LOC" />
<img src="https://img.shields.io/badge/files-582-2ea44f?style=flat-square" alt="Files" />
<img src="https://img.shields.io/badge/tests-1%2C093_passing-2ea44f?style=flat-square" alt="Tests" />
<img src="https://img.shields.io/badge/platforms-5-orange?style=flat-square" alt="Platforms" />

<br><br>

[![GitHub](https://img.shields.io/badge/GitHub-wFate-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/wFate)

<br>

</div>