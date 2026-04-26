<div align="center">

# ⚔️ FateMMO Game Engine

[![CI](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/wFate/FateMMO_GameEngine?style=flat-square&label=🎮%20Release&color=gold)](https://github.com/wFate/FateMMO_GameEngine/releases/tag/FateMMOv2)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?style=flat-square&logo=cplusplus)
![Lines of Code](https://img.shields.io/badge/LOC-193%2C000%2B-brightgreen?style=flat-square)
![Files](https://img.shields.io/badge/files-848-2ea44f?style=flat-square)
![Tests](https://img.shields.io/badge/tests-1%2C621_passing-brightgreen?style=flat-square)
![Protocol](https://img.shields.io/badge/protocol-v12-blueviolet?style=flat-square)
![Quests](https://img.shields.io/badge/quests-167-E040FB?style=flat-square)
![Security](https://img.shields.io/badge/security-Noise__NK%20%2B%20AuthProof%20%2B%20DB__sessions-critical?style=flat-square)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20iOS%20%7C%20Android%20%7C%20Linux%20%7C%20macOS-orange?style=flat-square)

**Production-grade 2D MMORPG engine built entirely in C++23.** Engineered for mobile-first landscape gameplay with a fully integrated Unity-style editor, server-authoritative multiplayer architecture, and Noise_NK encrypted custom networking — all from a single codebase, zero middleware, zero third-party game frameworks.

> **193,000+ lines** across **848 files** — engine (85K), game (41K), server (37K), tests (31K)

🌐 [**www.FateMMO.com**](https://www.FateMMO.com) &nbsp;·&nbsp; 🎬 [**Watch the Showcase**](https://www.youtube.com/watch?v=9zS-RVbranE)

</div>

---

### 🚧 v3 Incoming — Phase 3C Content Drop

> **Demo v3 is around the corner.** Building on v2's editor-runtime loop, the next release folds in **Phase 3C ambient NPC mini-chains** (10 chains × 3 quests = 30 new side quests, ~6,500 kills of brutal end-grind), the **Tier 3 narrative arc** (Gravestones / Altars / Umbra Remnants), the **type-taxonomy hygiene pass** (816 items normalized into 6 canonical buckets), and a **server-authoritative PvP zone gate** rebuild that closes a silent disable bug. **Protocol stays at v12** — no client redistribution required for shipped wins; just freshen `FateEngine.exe`.
>
> 🎯 **Headline content (since v2):**
> - 🪦 **Greta's Gravestones** (WhisperingWoods, Lv 6-12) — 5 cairn sites + parallel "tend / question" tails. First content to seed Opals into the live loop.
> - 🕯️ **Brae's Altars** (BlightedSwamp, Lv 38-46) — 3 altar sites, 3-way fork (Sun-Touched / Bough-Bound / Balanced).
> - 🌑 **Umbra Remnants** (Althea, Lv 50-65) — 5 quests, 2 branching endings ("Let her rest" vs "Hold the wards yourself").
> - ⚔️ **Phase 3C ambient grind** — Bram, Hadrik, Tessen, Mira+Vell, Sister Sand+Old Cinder, Toren, Vael, Vess, Ash, Mora promoted to quest-givers with stat scrolls + Fate's Grace + tier-matched enhance stones on every Q3 capstone.

---

### 🎉 v2 Release — Engine Demo Levels Up

> **Demo v2 is live.** The open-source `FateDemo` build ships with the full editor-runtime control loop the proprietary game uses every day:
>
> - ▶️ **Play / Pause / Resume / Stop** — green Play snapshots the ECS, red Stop restores it; Pause/Resume preserves camera state mid-session.
> - 👁️ **Observe Mode** — blue **Observe** button runs the loaded scene live with editor chrome hidden, so you see exactly what the player sees. Wire your own handler via `AppConfig::onObserveStart` to swap in a network-spectate flow.
> - 🗺️ **Scene Dropdown** — pick any `assets/scenes/*.json` from the viewport toolbar; transitions are gated against in-flight Play state.
> - 📁 **File Menu + Ctrl+S** — New / Open / Save / Save As wired through atomic-write JSON serialization (`.tmp` + rename, parent-dir auto-create, cross-volume copy fallback).
>
> 📦 [**Download FateMMO_Demo_v2.zip**](https://github.com/wFate/FateMMO_GameEngine/releases/tag/FateMMOv2) &nbsp;·&nbsp; ⭐ [**Star the repo**](https://github.com/wFate/FateMMO_GameEngine) &nbsp;·&nbsp; 🌐 [**FateMMO.com**](https://www.FateMMO.com) &nbsp;·&nbsp; 🎬 [**YouTube Showcase**](https://www.youtube.com/watch?v=9zS-RVbranE)

---

## 💎 Key Highlights

| 🏗️ | **193K+ LOC** of hand-written C++23 across engine, game, server, and tests — no code generation, no middleware, no AAA framework license fees |
|:---:|:---|
| 🔐 | **Noise_NK cryptography** — two X25519 DH ops + XChaCha20-Poly1305 AEAD, forward secrecy, symmetric epoch-gated rekeying, **encrypted AuthProof** so auth tokens never leave the client in the clear |
| 🛡️ | **Hardened v12 protocol** — 64-bit CSPRNG session tokens, timing-safe auth comparisons, anti-replay nonces, 64-bit ACK window, `CmdAckExtended` recovery, DB-backed `auth_sessions` with cross-process `LISTEN/NOTIFY` kick |
| 🎮 | **50+ server-authoritative game systems** — combat, skills, inventory, trade, guilds, arenas, dungeons, pets, costumes, collections, opals economy |
| ▶️ | **Play / Observe / Scene-dropdown** — full editor-runtime loop in the open-source v2 demo, with snapshot/restore on Play and configurable Observer hooks |
| 🌳 | **Branching dialogue-tree quests** — **167 quests** across 4 tiers + Phase 3C ambient mini-chains, state-aware NPC trees, **interact-site framework** (cairns / altars / shrines / gravestones), Honor + Opals + tier-matched enhance stones on every turn-in |
| 🪦 | **3 Tier-3 narrative arcs** — Greta's Gravestones (Lv 6-12), Brae's Altars (Lv 38-46, 3-way fork), Umbra Remnants (Lv 50-65, branching ending). Story-first chains with zero new mobs and zero combat balance changes |
| ⚔️ | **Phase 3C ambient grind** — 30 new side quests (Q730-Q822) across 10 NPC chains, ~6,500 brutal kills, themed stat scrolls + Fate's Grace + tier-matched enhance stones on every Q3 capstone |
| 🗄️ | **DB-driven content engine** — **111 numbered SQL migrations**, **816 hand-tuned items** across 6 canonical types, **23 PostgreSQL repository files** with fiber-based async dispatch (zero game-thread blocking) |
| 🖥️ | **Full Unity-style editor** with live inspector, undo/redo, Aseprite animation editor, atomic-write JSON saves, asset browser, and 29 device profiles |
| 📱 | **5-platform support** from a single codebase — Windows, **macOS (native Metal, ProMotion 120fps)**, iOS, Android, Linux |
| 🧪 | **1,621 automated tests** across 218 test files keeping every subsystem honest (10,585 assertions, all green) |
| 🎨 | **64 custom UI widgets** with 42 theme styles, JSON-driven screens, viewport scaling, and zero-ImGui shipping builds (5.7 MB stripped exe) |
| 🌍 | **27-scene handcrafted world** with 5 factions, **67 placed NPCs**, faction guards, boss rotations, and Aurora Gauntlet endgame PvP tower |
| 📡 | **Protocol v12** custom UDP — critical-lane bypass for 9 load-bearing opcodes, `SvEntityEnterBatch` coalesce, 32-bit delta compression, fiber-based async DB dispatch, client-side `CharacterFlags` mirror |
| 👁️ | **Admin observer/spectate mode** — log in without a character and roam any live scene with full replication |
| 🐾 | **Active pet system** — equip one pet, share 50% XP, gain stat bonuses, auto-loot within radius (Legendary Fox: 0.2s tick, 128px) |
| 💰 | **Opals economy + AdMob rewarded video** with ECDSA-signed server-side verification |
| 📦 | **Pluggable VFS** — `IAssetSource` abstracts every read; ship loose files in dev, bundled `.pak` archives in production via PhysicsFS overlay |

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

The demo opens the full editor UI with a procedural tile grid. Use the new **▶️ Play** button in the Scene viewport to enter a snapshot-protected play session, **👁️ Observe** to live-preview a scene with chrome hidden, or pick a scene from the dropdown to load it.

> 💡 **Prefer a pre-built binary?** Grab [**FateMMO_Demo_v2.zip**](https://github.com/wFate/FateMMO_GameEngine/releases/tag/FateMMOv2) from the releases page — no build required.

> **Full game build:** The proprietary game client, server, and tests build automatically when their source directories are present. The open-source release includes the complete engine library and editor.

---

## 🛠️ Tech Stack & Architecture

| Category | Technology & Innovation |
|----------|-----------|
| **Language** | Modern C++23 (MSVC, GCC 14, Clang 18). `std::expected`, structured bindings, fold expressions throughout |
| **Graphics RHI** | `gfx::Device` abstraction with **OpenGL 3.3 Core** & native **Metal** backends (iOS + macOS). Pipeline State Objects, typed 32-bit handles, collision-free uniform cache. Zero-batch-break SpriteBatch (10K capacity, hash-based dirty-flag sort skip), palette swap shaders, nestable scissor clipping stack, nine-slice rendering. Metal: 9 MSL shader ports, CAMetalLayer, ProMotion 120fps triple-buffering |
| **SDF Text** | True **MTSDF font rendering** — uber-shader with 4 styles (Normal/Outlined/Glow/Shadow), offline `msdf-atlas-gen` atlas (512x512, 177 glyphs, 4px distance range), `median(r,g,b)` + `screenPxRange` for resolution-independent edges at any zoom. **4 registered fonts** (Inter-Regular, Inter-SemiBold, PressStart2P, PixelifySans) via `FontRegistry` singleton. Multi-font rendering via zero-copy `activeGlyphs_` pointer swap. World-space and screen-space APIs, UTF-8 decoding |
| **Render Pipeline** | 11-pass RenderGraph: GroundTiles → Entities → Particles → **SkillVFX** → SDFText → DebugOverlays → Lighting → BloomExtract → BloomBlur → PostProcess → Blit |
| **Editor** | Dear ImGui (docking) + ImGuizmo + ImPlot + imnodes. Custom dark theme (Inter font family, FreeType LightHinting). **Scene viewport toolbar** (Play/Pause/Resume/Stop, Observe, Scene dropdown). Property inspectors, visual node editors, **Aseprite-first animation editor** with layered paper-doll preview (5-layer composite), sprite slicing, tile painting, play-in-editor, undo/redo (200 actions). Every editor-authored JSON write goes through `writeFileAtomic` (`.tmp` + rename + cross-volume fallback) |
| **Networking** | Custom reliable UDP (`0xFA7E`, **PROTOCOL_VERSION 12**), **Noise_NK handshake** (two X25519 DH ops) + **XChaCha20-Poly1305 AEAD** encryption (key-derived 48-bit nonce prefix, symmetric rekeying every 65K packets / 15 min, epoch-gated). **Auth hardening**: 64-bit CSPRNG session tokens (libsodium), encrypted `CmdAuthProof` (0xD8) so the token never rides plaintext, timing-safe comparisons, per-account login lockout with exponential backoff, fail-closed encrypt symmetry on both client and server send paths. **DB-backed sessions** (`auth_sessions` table, mig 089) with `LISTEN/NOTIFY session_kicked` cross-process kick. IPv6 dual-stack, 3 channel types, **26-byte header with 64-bit ACK bitfield** (v9+), RTT-based retransmission, 1 MB kernel socket buffers, 2048-slot pending queue with **critical-lane bypass for 9 load-bearing opcodes**, **SvEntityEnterBatch coalesce**, **CmdAckExtended** out-of-window recovery |
| **Database** | PostgreSQL (libpqxx), **23 repository files**, **10 startup caches**, **fiber-based `DbDispatcher`** (async player load, disconnect saves, metrics, maintenance — zero game-thread blocking), connection pool (5–50) + circuit breaker, priority-based 4-tier dirty-flag flushing, 30s staggered auto-save. `PlayerLockMap` for concurrent mutation serialization. WAL removed — 30s window bounds crash loss. **111 numbered SQL migrations** under `Docs/migrations/`. **816 items** in `item_definitions` across 6 canonical types — Armor 355 / Consumable 222 / Material 148 / Weapon 86 / Container 4 / Currency 1 (vendor-exchange token; gold + opals are first-class account columns, not item rows) — hardened by the S118-S120 type-taxonomy passes |
| **ECS** | Data-oriented archetype ECS, contiguous SoA memory, **56 registered components**, generational handles, prefab variants (JSON Patch), compile-time `CompId`, Hot/Warm/Cold tier classification, `FATE_REFLECT` macro with field-level metadata, `ComponentFlags` trait policies, RAII iteration depth guard, 4096-archetype reserve capacity. `World::processDestroyQueue(scope)` tags every flush with caller intent + per-type entity breakdown for diagnosability |
| **Memory** | Zone arenas (256 MB, O(1) reset), double-buffered frame arenas (64 MB), thread-local scratch arenas (Fleury conflict-avoidance), lock-free pool allocators, debug occupancy bitmaps, ImPlot visualization panels |
| **Audio** | SoLoud (SDL2 backend, 32 virtual voices, OGG streaming, 2D spatial audio, 3 buses, 10 game events wired). All loads route through `IAssetSource::readBytes` → `Wav::loadMem(copy=true)` so packaged `.pak` archives work transparently |
| **Async & Jobs** | Win32 fibers / minicoro, 4 workers, 32-fiber pool, lock-free MPMC queues, counter-based suspend/resume with fiber-local scratch arenas. Fiber-based async scene, asset, and DB loading — zero frame stalls. Queue-full spin emits diagnostic WARN at 1024 spins, never silently hangs |
| **Spatial** | Fixed power-of-two grid (bitshift O(1) lookup), Mueller-style 128px spatial hash, per-scene packed collision bitgrid (1 bit/tile, negative coord support, `isBlockedRect` AABB queries). Server loads from scene JSON at startup — zero rubber-banding |
| **Asset Pipeline** | Generational handles (20+12 bit), hot-reload (300ms debounced), fiber async decode + main-thread GPU upload, failed-load caching (prevents re-attempts), **`IAssetSource` abstraction** (`DirectFsSource` for loose files / `VfsSource` for PhysicsFS-backed `.pak`), compressed textures (ETC2 / ASTC 4x4 / ASTC 8x8) with KTX1 loader, VRAM-budgeted LRU cache (512 MB, O(N log N) eviction). Behind `option(FATE_USE_VFS …)` so behavior is byte-identical until per-platform flip |

---

## 🗡️ Game Systems

Five factions shape a **27-scene world** — the 🩸 **Xyros** severing the threads of fate, the 🛡️ **Fenor** weaving steadfast eternity, the 🔮 **Zethos** unraveling ancient mysteries, the 👑 **Solis** forging a golden epoch, and the 🕳️ **Umbra** — a secret non-playable faction born from the negative space of the tapestry itself, hostile to all who enter their domain. Each faction fields **3 tiers of named guards** — mage sentries, archer elites, and a boss-tier warden — that auto-aggro enemy-faction players on sight. Bind at **Innkeeper NPCs**, journey through **167 dialogue-tree quests** across 4 tiers + Phase 3C ambient mini-chains, and fight the **Fate Guardian** in the rotating boss arena.

All gameplay logic is fully server-authoritative with priority-based DB persistence, dirty-flag tracking at **95 mutation sites**, spanning **50+ robust systems** across **75 shared gameplay files** (**19,000+ LOC** of pure C++ game logic — zero engine dependencies). Every system is DB-wired with load-on-connect, save-on-disconnect, and async auto-save.

<details>
<summary><b>🔥 Combat, PvP & Classes</b></summary>

- **Optimistic Combat** — Attack windups play immediately to hide latency; 3-frame windup (300ms), hit frame with predicted damage text + procedural lunge offset. `CombatPredictionBuffer` ring buffer (32 slots). Server reconciles final damage.
- **Combat Core** — Hit rate with coverage system (non-boss mobs `mob_hit_rate=10` for 5-level coverage, bosses `=30` for 15-level — damage floor vs. over-leveled players), spell resists, block, armor reduction (75% cap), 3x3 class advantage matrix (hot-reloadable JSON), crit system with class scaling.
- **Skill Manager** — 60+ skills with skillbook learning, cooldowns, cast-time system (server-ticked, CC/movement interrupts, fizzle on dead target), 4x5 hotbar, passive bonuses, resource types (Fury/Mana/None). `SvSkillDefs` sends full class catalog on login.
- **Skill VFX Pipeline** — Composable visual effects: JSON definitions with 4 optional phases (Cast/Projectile/Impact/Area). Sprite sheet animations + particle embellishments. 32 max active effects. 13 tests.
- **Status Effects** — DoTs (bleed/burn/poison), buffs, shields, invuln, transform, bewitch, source-tagged removal (Aurora buff preservation), stacking, `getExpGainBonus()`.
- **Crowd Control** — Stun/freeze/root/taunt with priority hierarchy, immunity checks, **diminishing returns** (per-source 15s window, 50% duration reduction per repeat, immune after 3rd application).
- **PK System** — Status transitions (White → Purple → Red → Black), decay timers, cooldowns, same-faction targeting restricted to PK-flagged players.
- **Honor & Rankings** — PvP honor gain/loss tables, 5-kills/hour tracking per player pair. **PvE honor on every quest turn-in** via formula `(5 + reqLvl×2 + chainLen×10) × tierMul` (Starter 1.0 → Adept 2.0). Global/class/guild/honor/mob kills/collection leaderboards with faction filtering, 60s cache, paginated. **Arena Honor Shop** sells 3 Legendary faction shields (Dark's / Light's / Fate's Shield at 250K / 555K / 777K honor).
- **Arena & Battlefield** — 1v1/2v2/3v3 queue matchmaking, AFK detection (30s), 3-min matches, honor rewards. 4-faction PvP battlefields. `EventScheduler` FSM (2hr cycle, 10min signup). Reconnect grace (180s) for battlefield/dungeon, arena DC = forfeit.
- **Two-Tick Death** — Alive → Dying (procs fire) → Dead (next tick), guaranteeing kill credit without race conditions. Replicated as 3-state `deathState` with critical-lane bypass so death overlays always fire.
- **Fate's Grace** — Epic consumable unlocked from the Opals Shop: third button on the death overlay, revives at death location with full HP. Dedicated protocol opcode for atomic reconciliation.
- **Cast-Time System** — Server-ticked `CastingState`, interruptible by CC/movement, fizzles on dead targets. Replicated via delta compression.
- **3-Tier Shield Bar** — Mana Shield depletes outer-first across 3 color tiers (300% max HP absorb at rank 3). Replicated via `SvCombatEvent.absorbedAmount` for instant client-side decrement.
- **Faction Guards** — Each village is defended by 3 tiers of named guards (Xyros: Ruin/Fatal/Doom, Fenor: Weaver/Thread/Bastion, Zethos: Sage/Seeker/Prophecy, Solis: Treasure/Fortune/Heir, Umbra: Whisper/Phantom/Erasure). Stationary, paper-doll rendered with class-specific skills, aggro-on-sight for enemy faction players. Umbra guards attack *everyone* — walk into The Scorched Hollow at your own risk.
- **Combat Leash** — Boss/mini-boss mobs reset to full HP and clear threat table after 5s idle with no aggro target, or 60s while actively aggroed (allows kiting bosses to safe spots). Engaged mobs use `contactRadius * 2` for target re-acquisition. Regular mobs unaffected.

</details>

<details>
<summary><b>🌳 Quest Trees & Interact-Sites</b></summary>

- **Dialogue-Tree Framework** — Every quest is a JSON-authored branching tree (`assets/dialogue/quests/<id>.json`) with `offer` / `inProgress` / `turnIn` sub-trees, 12-action vocabulary (`AcceptQuest`, `CompleteQuest`, `OpenShop`, `OpenScreen`, `SetFlag`, `Goto`, `EndDialogue`, etc.), and condition-gated visibility per choice (`HasItem`, `HasFlag`, `HasInventorySpace`, `HasCompletedQuest`, `HasActiveQuest`). **183 dialogue JSON files**: 124 quest trees + 33 NPC trees + 26 interact-site trees.
- **167 Quests, 4 Tiers + Phase 3C** — Starter / Novice / Apprentice / Adept, plus the Umbra fragment chain and the new ambient mini-chains. **11 objective types** (`Kill`, `Collect`, `Deliver`, `TalkTo`, `Interact`, `PvP`, `Explore`, `KillInParty`, `CompleteArena`, `CompleteBattlefield`, `PvPKillByStatus`) with prerequisite chains, max 10 active.
- **Interact-Site Framework** — `InteractSiteComponent` placed on world entities (cairns, altars, shrines, gravestones, ward-stones) progresses `ObjectiveType::Interact` quests via player click, sets `CharacterFlagsComponent` flags, and triggers tree dialogues with first-visit + revisit nodes. **Wire packet (`CmdInteractSite` 0xE2 / `SvInteractSiteResult` 0xE3)** keys off `siteStringId` directly — no PIDs, no server-issued ghost replication, and identical scene-JSON resolution on both sides.
- **Tier 3 Investigation Chains** — Story-first arcs
  - 🪦 **Gravestones (WhisperingWoods, Greta the Gravekeeper, Lv 6-12)** — 4 quests, 5 cairn sites (named / unmarked / drowned), parallel "tend" / "question" tails. First content to seed Opals into the live-play loop (+5 / +1 per quest).
  - 🕯️ **Altars (BlightedSwamp, Brae, Lv 38-46)** — 6 quests, 3 sites, 3-way fork (Sun-Touched / Bough-Bound / Balanced + Broker capstone). The third altar is where the player first realizes the world is older than the four factions.
  - 🌑 **Umbra Remnants (Althea, Lv 50-65)** — 5 quests, 5 sites, 2 branching endings ("Let her rest" vs "Hold the wards yourself"). Foreshadowed by Q703's "older weathers, older peoples" line.
- **🆕 Phase 3C Ambient Mini-Chains (10 chains × 3 quests = 30 side quests)** 
- **Honor + Opals on Every Turn-In** — `QuestRewards` carries `int honor = 0` (0 = use formula `(5 + reqLvl×2 + chainLen×10) × tierMul`, Starter 1.0 → Adept 2.0) and `int64_t opals = 0` (opt-in). Both are applied in `QuestManager::turnInQuest` after objective verification + Collect/Deliver consumption. Granted rewards echo back to chat as `[Quest]` lines (title / EXP / gold / honor / opals / items).
- **8 Milestone Collections** — `QuestsCompleted` collection condition fires on every successful turn-in and grants permanent stat bonuses at thresholds
- **Cross-Zone Delivery Chains** — Maren → Aelith → Helga / Kiral arcs span up to 5 zones, with NPC-A handing the player a soulbound quest item to deliver to NPC-B halfway across the world map.
- **Branching NPC Dialogue** — State-aware quest-tree picker at every NPC. Priority: turnIn > inProgress > offer > NPC ambient > legacy greeting. Driven by `QuestGiverComponent.questIds`.
- **`CharacterFlagsComponent` client mirror (v12)** — `SvCharacterFlagsSnapshot` (login) + `SvCharacterFlagDelta` (per-mutation) replicate the player's flag set so dialogue `HasFlag` conditions evaluate locally without round-trips. Enables hard-branching trees on the client.

</details>

<details>
<summary><b>✨ Progression, Items & Collections</b></summary>

- **Fixed Stats & XP** — Gray-through-red level scaling, 0%–130% XP multipliers. Base stats fixed per class to balance the meta, elevated only by gear, collections, and the active pet.
- **Collections System** — DB-driven passive achievement tracking across 3 categories (Items / Combat / Progression) plus the new **Quest milestones** category. **30+ seeded definitions** with 8 quest-completion milestones. 9 event trigger points. Permanent additive stat bonuses (11 stat types) with no cap. Costume rewards on completion. `SvCollectionSync` + `SvCollectionDefs` packets.
- **Enchanting & Sockets** — +1 to +15 enhancement with weighted success rates (+1–8 safe, +9–15 risky with 50% → 2% curve). Protection stones (always consumed, prevents breaking). Secret bonuses at +11 / +12 / +15. Gold costs scale 100g → 2M. Server-wide broadcast on successful enchant at +9 and above.
- **Socket System** — Accessory socketing (Ring / Necklace / Cloak), weighted stat rolls (+1: 25% → +10: 0.5%), server-authoritative with re-socket support. 7 scroll items in DB.
- **Core Extraction** — Equipment disassembly into 7-tier crafting cores based on rarity and enchant level (+1 per 3 levels). Common excluded.
- **Crafting** — 4-tier recipe book system (Novice / Book I / II / III) with ingredient validation, level/class gating, gold costs. `RecipeCache` loaded at startup. **Bag-aware** — `canAddItem` verifies sub-slot space before commit.
- **Consumables Pipeline** — **18 effect types** fully wired: HP/MP Potions, SkillBooks (class/level validated), Stat Reset (Elixir of Forgetting), Town Recall (blocked in combat/instanced content), Fate Coins (3→level×50 XP), EXP Boost Scrolls (10%/20%, 1hr, stackable tiers), Beacon of Calling (cross-scene party teleport), Soul Anchor (auto-consumed on death to prevent XP loss), Fate's Grace (revive-in-place).
- **Costumes & Closet** — DB-driven cosmetic system. 5 rarity tiers (Common→Legendary), per-slot equipping, master show/hide toggle, paper-doll integration. 3 grant paths: mob drops (per-mob drop chance via `mob_costume_drops`), collection rewards, shop purchase. Full replication via 32-bit delta field mask. `SvCostumeDefs` / `SvCostumeSync` / `SvCostumeUpdate` packets.
- **💎 Opals Currency** — Premium non-P2W currency. End-to-end wired: direct-credit mob drops (Normals 1–5 @ 20–30%, MiniBosses 10–40 @ 90–100%, Bosses 200–700 @ 100% in dungeons), DB persistence, `SvPlayerState` replication, menu-driven Opals Shop with server-validated purchases against JSON catalog. QoL items priced as grind goals, not power.
- **Per-Item Stack Caps** — `Inventory::addItem` / `moveItem` / `moveBagItem` honor per-item `max_stack` and split oversized drops across multiple slots (200-pot drop with `max_stack=99` → 99+99+2 across 3 slots).

</details>

<details>
<summary><b>🌍 Economy, Social & Trade</b></summary>

- **Inventory & Bags** — 16 fixed slots + 10 equipment slots. Nested container bags (1–10 sub-slots). Auto-stacking consumables/materials. Drag-to-equip/stack/swap/destroy with full server validation. UUID v4 item instance IDs. Tooltip data synced (displayName, rarity, stats, enchant). **Configurable pickup priority** (Inventory-First / Bag-First) via Settings panel — wired through combat + pet auto-loot.
- **Bank & Vault** — Persistent DB storage for items and gold. Flat 5,000g deposit fee. Full `ItemInstance` metadata preserved through deposit/withdraw. Gold withdraw cap check prevents silent loss.
- **Market & Trade** — Peer-to-peer 2-step security trading (Lock → Confirm → Execute). 8 item slots + gold. Slot locking prevents market/enchant during trade. Auto-cancel on zone transition. Market with 2% tax, status lifecycle (Active / Sold / Expired / Completed), seller-claim gold flow, jackpot pools, atomic buy via `RETURNING`.
- **Opals Shop** — In-game SHP menu tab (no NPC needed). JSON catalog (`assets/data/opals_shop.json`) with 10+ SKUs — bags (1,000–5,000 opals), Fate's Grace revive charges (5,000), 5 pet eggs (3,000–8,500). Server validates every purchase, returns `updatedOpals`.
- **Crafting** — 4-tier recipe book with ingredient validation and level/class gating.
- **Guilds & Parties** — Ranks, 16x16 pixel symbols, XP contributions, ownership transfer. 3-player parties with +10%/member XP bonuses and loot modes (FreeForAll / Random per-item).
- **Friends & Chat** — 50 friends, 100 blocks, online status with live `currentScene` enrichment. New **`FriendsPanel`** with 3 tabs (Friends / Requests / Blocked) and inline Whisper / Party-invite / Remove / Accept / Decline / Cancel / Unblock actions. 7 chat channels (Map / Global / Trade / Party / Guild / Private / System), cross-faction garbling, server-side mutes (timed), profanity filtering (leetspeak normalization, 52-word list). **21 per-prefix system broadcast colors** (Loot / Boss / Event / Guild sub-types). Right-click → Whisper UX prefills `/w <name> ` in the chat input.
- **Bounties** — PvE bounty board (max 10 active, 50K–500M gold, 48hr expiry), 2% tax, guild-mate protection, 12hr guild-leave cooldown, party payout splits. **`BountyPanel`** with Active / My Bounties / History tabs surfaced via interact-site Bounty Boards in all 4 faction villages.
- **Economic Nonces** — `NonceManager` with random uint64 per-client, single-use replay prevention, 60s expiry. Wired into trade and market handlers. 8 tests.

</details>

<details>
<summary><b>🌍 World Architecture & Factions</b></summary>

- **27 Handcrafted Scenes** — 7 adventure zones, 5 faction villages (incl. Umbra's Scorched Hollow), 1 Castle PvP hub, 9 instanced dungeon floors (Lighthouse F1–F5 + Secret, Pirate Ship F1–F3), 4 Fate's Domain sub-arenas, and a transit Beach.
- **5 Factions** — Four playable factions with competing philosophies about fate, plus the **Umbra** — a secret non-playable faction that exists in the gaps between the other four:
  - 🩸 **Xyros** — Fate is a weapon. Raid enemy villages for glory, charge headfirst into PvP, measure worth in kill counts. Guards: **Ruin** (Mage) → **Fatal** (Archer) → **Doom** (Warrior Boss).
  - 🛡️ **Fenor** — Peace is the hardest thing to keep. Sanctuary village, open doors, fight only to protect. Guards: **Weaver** (Mage) → **Thread** (Archer) → **Bastion** (Warrior Boss).
  - 🔮 **Zethos** — Seekers chasing rumors at the edge of an ancient forest. Take every quest, explore every corner. Guards: **Sage** (Mage) → **Seeker** (Archer) → **Prophecy** (Warrior Boss).
  - 👑 **Solis** — Gold moves the world. Trade ruthlessly, grind efficiently, every quest has a payout. Guards: **Treasure** (Mage) → **Fortune** (Archer) → **Heir** (Warrior Boss).
  - 🕳️ **Umbra** — The negative space in the tapestry of fate — what remains when threads are severed. Not evil, just absence. Every name they carry is incomplete, every word trails off. Their village, **The Scorched Hollow**, is the mid-game recall hub — players *need* it, but Umbra guards attack everyone on sight and PvP is enabled. Guards: **Whisper** (Mage Lv35) → **Phantom** (Archer Lv40) → **Erasure** (Warrior Boss Lv45).
- **67 NPCs placed** across 13 NPC-bearing scenes — 40 faction NPCs (10 per village) + 7 Umbra fragment-named NPCs in The Scorched Hollow (**Vess** Innkeeper, **Faded** Shopkeeper, **Ash** Banker, **"The"** Quest Giver, **One** Blacksmith, **Mora** Alchemist, **Absent** Lore NPC) + 17 adventure-zone anchors + 3 Castle hub NPCs. After Session 119, **36 of those NPCs offer quests** — including the new Phase 3C ambient quest-givers **Bram**, **Hadrik**, **Tessen**, **Mira+Vell**, **Sister Sand+Old Cinder**, **Toren**, **Vael** alongside legacy chain anchors **Greta**, **Maren**, **Tomas**, **Aelith**, **Kiral**, **Helga**, **Nessa**, **Kais**, **Seraph Althea**, and **The Broker**. Helpful but unsettling Umbra voices, hollow tones, unfinished sentences. Faction-aware click-targeting via `NPCComponent.targetFactions`.
- **"The" Quest Chain** — An NPC whose name is literally all that remains of a title. His quests aren't generic kill tasks — they're a fragmented being searching for what was taken:
  - *Crystal Caverns (Lv22-30):* "I heard something down there once. My name, maybe. Bring it back."
  - *Scorched Wastes (Lv32-40):* "The fire took something from me. I can't remember what. Look in the ash."
  - *Blighted Swamp (Lv42-50):* "There's a piece of me rotting in that swamp. I can feel it. I just can't reach it."
  - Complete all three to trigger a hidden quest — **"..."** — where he almost remembers his name. Gets one more word. But it's wrong. The Umbra don't get to be whole.
- **Faction Guard System** — 3-tier layered village defense: outer gate mage sentries (Lv30, 8-tile range), inner gate archer elites (Lv40, 6-tile range), village core warrior boss (Lv45, melee). All stationary, 30s respawn, paper-doll rendered with faction-specific skills. Guards also gate overworld adventure zones — enemy-faction guards block quest NPCs, portals, and scene transitions, creating organic faction territories.
- **Fate Guardian** — Fate's Domain rotating world boss (Lv50, 500K HP) with server-wide event broadcasts. 5-minute respawn delay after death, avoids same-scene consecutive spawns. Multi-spawn-point support (1-of-N random).
- **Innkeeper NPCs** — Bind your respawn point at any faction inn (100K gold). `recallScene` persisted to DB. Town Recall scrolls teleport to last bound inn.

</details>

<details>
<summary><b>🏰 World, AI & Dungeons</b></summary>

- **Mob AI** — Cardinal-only movement with L-shaped chase pathing, axis locking, wiggle unstuck, roam/idle phases, threat-based aggro tables, `shouldBlockDamage` callback (god mode). **Server-side DEAR** — mobs in empty scenes skipped entirely, distance-based tick scaling (full rate within 20 tiles, quadratic throttle beyond, 2s idle patrol beyond 48 tiles). Wall collision is intentional — enables classic lure-and-farm positioning tactics.
- **Spawns & Zones** — `SceneSpawnCoordinator` per-scene lifecycle (activate on first player, teardown on last leave), `SpawnZoneCache` from DB (circle/square shapes), respawn timers, death persistence via `ZoneMobStateRepository` (prevents boss respawn exploit). `createMobEntity()` static factory. Collision-validated spawn positions (30 retries, 48px mob separation). Per-entity stat overrides via `spawn_zones.instances_json` — HP/damage/attackRange/leashRadius/respawnSeconds/hpRegenPerSec per-instance.
- **Quest System** — 11 objective types (Kill / Collect / Deliver / TalkTo / **Interact** / PvP / Explore / KillInParty / CompleteArena / CompleteBattlefield / PvPKillByStatus) with prerequisite chains, branching NPC dialogue trees (enum-based actions + conditions), max 10 active, **167 quests** across 4 tiers (Starter / Novice / Apprentice / Adept) + the Umbra fragment chain + Phase 3C ambient mini-chains (Q730-Q822).
- **Instanced Dungeons** — Per-party ECS worlds, 10-minute timers, boss rewards, daily tickets (per-dungeon), invite system (30s timeout), celebration phase. Reconnect grace (180s). Event locks prevent double-enrollment. Per-minute chat timer. Per-dungeon HP×1.5 / damage×1.3 multipliers for 3-player party tuning.
- **Aurora Gauntlet** — 6-zone PvP with hourly faction-rotation buff (+25% ATK/EXP), wall-clock `hour%4` rotation. Aether Stone + 50K gold entry. Aether world boss (Lv55, 150M HP, 36hr respawn) with 23-item loot table. Zone scaling Lv10→55. Death ejects to Town. Live `GauntletHUD` (TopRight) + `GauntletResultModal` top-10 leaderboard.
- **🐾 Active Pet System** — Equip one pet at a time; active pet shares 50% of player XP from mob kills, contributes to `equipBonus*` (HP + crit rate + XP bonus) via `PetSystem::applyToEquipBonuses` (recalc on equip/unequip/level). 5 pets shipped across 4 rarity tiers: 🐢 Turtle + 🐺 Wolf (Common), 🦅 Hawk (Uncommon), 🐆 Panther + (Rare), 🦊 Fox (Legendary). Premium tier knobs (interval/itemsPerTick/radius) on `pet_definitions` — Legendary Fox auto-loots every 0.2s with 128px radius. Client-side `PetFollowSystem` keeps the active pet trailing the player with 4-state machine (Following / Idle / MovingToLoot / PickingUp). Server-authoritative auto-looting (per-rarity tick, 64–128px radius, ownership + party aware). DB-level unique partial index enforces one active pet per character. Consumable pet eggs purchasable from the Opals Shop (Legendary Fox 30,000 opals).
- **Loot Pipeline** — Server rolls → ground entities → spatial replication → pickup validation → 90s despawn. Per-player damage attribution, live party lookup at death, strict purge on DC/leave. Epic/Legendary/Mythic server-wide broadcast; party loot broadcast to all members.
- **NPC System** — 10 NPC types: Shop, Bank, Teleporter (with item/gold/level costs), Guild, Dungeon, Arena, Battlefield, Story (branching dialogue), QuestGiver, Innkeeper (respawn binding). Proximity validation on all interactions. `EntityHandle`-based caching for zone-transition safety.
- **Event Return Points** — Centralized system prevents players from being stranded after disconnecting from instanced content. Return point set on event entry, cleared on normal exit, re-set on grace rejoin.
- **Trade Cleanup** — Active trades cancelled on disconnect, partner inventory trade-locks released via `unlockAllTradeSlots()`, preventing permanently locked slots.
- **👁️ Admin Observer / Spectate Mode** — Admin-role accounts can `/spectate <scene>` into any live scene *without a character entity* — replication still fires, ghost entities interpolate, MobAI continues ticking via `sceneObserverCounts_` presence refcount (so dead scenes don't freeze when only an observer is watching). `SvSpectateAck` (0xD9) returns typed status (accepted / not-admin / unknown-scene / stopped). Sentinel log `observer_only_ticked=0` detects regressions. Perfect for live-ops debugging, boss-fight reviews, and anti-cheat spot checks.

</details>

---

## 🎨 Retained-Mode UI System

Custom data-driven UI engine with **viewport-proportional scaling** (`screenHeight / 900.0f`) for pixel-perfect consistency across all devices. Anchor-based layout (12 presets + percentage sizing), JSON screen definitions, 9-slice rendering, two-tier color theming, virtual `hitTest` overrides for mobile-optimized touch targets. 21 per-prefix system message color/font configurations.

- **64 Widget Types:** 25 Engine-Generic (Panels, ScrollViews, ProgressBars, Checkboxes, ConfirmDialogs with serializable bgColor/borderColor/messageOffset, NotificationToasts, LoginScreen, ImageBox, TextInput with masked password mode, **PanelChrome / TabRail / DebugChromePanel / Divider** chrome primitives) and **39 Game-Specific** (DPad, SkillArc with 4-page C-arc, FateStatusBar, InventoryPanel with paper doll, CostumePanel, CollectionPanel, ArenaPanel, BattlefieldPanel, **PetPanel**, **OpalsShopPanel**, **BountyPanel** *(NEW)*, **FriendsPanel** *(NEW)*, **GauntletHUD** *(NEW)*, **GauntletResultModal** *(NEW)*, CraftingPanel, MarketPanel with buy confirmation + status lifecycle, BagViewPanel, EmoticonPanel, QuantitySelector, PlayerContextMenu, ChatIdleOverlay, BossHPBar, **NpcDialoguePanel** with state-aware quest-tree picker, DeathOverlay with Fate's Grace button, and more).
- **10 JSON Screens & 42 Theme Styles:** Parchment, HUD dark, dialog, tab, scrollbar, **14 chrome styles** (panel_chrome.default / title_pill / close_x + 8 panel variants + tooltip; tab_rail.default / active / inactive). Full serialization of layout properties, fonts, colors, and inline style overrides. Ctrl+S dual-save (build + source dir) via atomic write. Hot-reload with 0.5s polling + suppress-after-save guard.
- **Paper Doll System:** `PaperDollCatalog` singleton with JSON-driven catalog (`assets/paper_doll.json`) — body/hairstyle/equipment sprites per gender with style name strings, direction-aware rendering with per-layer depth offsets and frame clamping, texture caching, editor preview panel with live composite + Browse-to-assign. Used in game HUD, character select, and character creation.
- **Zero-ImGui Game Client:** All HUD, nameplates, and floating text render via SDFText + SpriteBatch. ImGui is compiled out of shipping builds entirely.
- **105+ UI tests.**

---

## 🔒 Server & Networking

**Headless 20 Hz server** (`FateServer`) with max **2,000 concurrent connections**. **46 handler files**, **23 DB repository files**, **10 startup caches**, **15-min idle timeout**, graceful shutdown with player save flush. Every game action is server-validated — zero trust client. **PROTOCOL_VERSION 12** — full v9 reliability rebuild + v10 interact-site framework + v11 site-string-id swap + v12 `CharacterFlags` client mirror.

<details>
<summary><b>🔐 Transport & Encryption</b></summary>

| Property | Value |
|----------|-------|
| Protocol | Custom reliable UDP (`0xFA7E`, **v12**), Win32 + POSIX |
| Encryption | **Noise_NK handshake** — two X25519 DH ops (`es` + `ee`, BLAKE2b-512 derivation, protocol-name domain separator) + **XChaCha20-Poly1305 AEAD** (key-derived 48-bit session nonce prefix OR'd with 16-bit packet sequence, 16-byte tag, separate tx/rx keys). Symmetric rekey every 65K packets / 15 min, **epoch-gated** (4-byte LE epoch payload, gated by `tryAdvanceRekeyEpoch` so retransmits dedupe instead of desyncing keys). Anonymous-DH fallback removed — every session is authenticated against the server's static identity key |
| Server Identity | Long-term X25519 static key (`config/server_identity.key`); public key distributed with client for MITM prevention. Key file is `chmod 0600` on POSIX, locked-DACL on Windows |
| Auth Hardening | **64-bit session tokens** generated via libsodium CSPRNG (`PacketCrypto::randomBytes`) — zero `std::mt19937`. **`CmdAuthProof` (0xD8)**: auth token is encrypted under the Noise session key *after* handshake — never traverses the wire in plaintext. Timing-safe comparisons via `sodium_memcmp`. Per-account login rate-limit with exponential backoff, auth-token TTL. **`AuthPhase` state machine** (HandshakePending → ProofReceived → Authenticated) gates non-system packet handlers — game commands are dropped until proof verifies |
| DB-Backed Sessions | **`auth_sessions` table** (mig 089) with PK `token`, partial unique index on `(account_id) WHERE activated_at IS NOT NULL`. `consumeAndActivate` is atomic: SELECT-FOR-UPDATE, DELETE-RETURNING any prior active session, NOTIFY `session_kicked` payload `{node, acct, cid}`. **`SessionListener`** runs `LISTEN session_kicked` on a dedicated `pqxx::connection` (NOT pooled — LISTEN state lives on a single backend), filters by `node == myServerNode_`, drops kick events into a thread-safe queue drained by `ServerApp::tick`. **Multi-process ready** — login on node B kicks the session on node A within ~1s |
| Fail-Closed Encrypt | Both `NetClient::sendPacket` and `NetServer::sendPacket` drop + LOG_ERROR on encrypt failure rather than fall back to plaintext. `payloadSize` bounds-checked before `sendTo` AND before `trackReliable` |
| IPv6 | Dual-stack with IPv4 fallback (DNS64/NAT64 — iOS App Store mandatory). Auth TCP uses `getaddrinfo(AF_UNSPEC, ...)` so DNS names work and SNI/X509 hostname verification is wired |
| Channels | Unreliable (movement, combat events), ReliableOrdered (critical), ReliableUnordered |
| Packets | 26-byte header (v9+: ackBits widened 32→64-bit), RTT estimation (EWMA 0.875/0.125), retransmission delay `max(0.2s, 2*RTT)`, zero-copy retransmit. Pending-packet queue 2048 slots, 75% congestion threshold. **`SvEntityEnterBatch`** coalesce, **`CmdAckExtended`** out-of-window recovery, epoch-gated `Rekey` |
| Socket Buffers | 1 MB kernel send/recv (`SO_SNDBUF`/`SO_RCVBUF`) — prevents silent drops during burst replication |
| Payload | 1200 B UDP standard (`MAX_PAYLOAD_SIZE=1174` after v9's 26-byte header); large reliables up to 16 KB (handler buffers bumped 4K→16K across 9 sites) |
| Critical-Lane Bypass | **9 load-bearing opcodes** (`SvEntityEnter`, `SvEntityLeave`, `SvPlayerState`, `SvZoneTransition`, `SvDeathNotify`, `SvRespawn`, `SvKick`, `SvScenePopulated`, `SvEntityEnterBatch`) bypass reliable-queue congestion check — eliminates "invisible mob" + "death overlay never fires" symptoms under load |
| Rate Limiting | Per-client, per-packet-type token buckets (**55+ packet types** across 14 categories), violation decay, auto-disconnect at 100 violations |
| Anti-Replay | Economic nonce system (trade/market, single-use uint64, 60s expiry, cleaned on disconnect), connection cookies (FNV-1a time-bucketed anti-spoof — not a cryptographic MAC), atomic dungeon-ticket claim |
| Auth Security | TLS 1.2+ with AEAD-only ciphers, shipping enforces `SSL_VERIFY_PEER` (no self-signed), login rate limiting (5 attempts → 5-min IP lockout, 15 attempts → 15-min username lockout), bcrypt timing-oracle defense (dummy hash run on unknown usernames), version gate |
| Auto-Reconnect | `ReconnectPhase` state machine, exponential backoff (1s→30s cap), 60s total timeout |
| Idle Timeout | 15-min inactivity auto-disconnect, per-client activity tracking, system chat warning before kick |
| Event Return Points | Centralized scene/position restore on DC from instanced content (dungeon/arena/battlefield) |

</details>

<details>
<summary><b>📡 Replication & AOI</b></summary>

- **Area of Interest** — Spatial-hash culling (128px cells), 640px activation / 768px deactivation (hysteresis). Scene-filtered. Optional `visibilityFilter` callback (GM invisibility).
- **Delta Compression** — **32-bit field mask** (17 fields: position, animFrame, flipX, HP/maxHP, moveState, animId, statusEffects, deathState, casting, target, level, faction, equipVisuals, pkStatus, honorRank, costumeVisuals). Only dirty fields serialized. Expanded from 16-bit for costume support.
- **Batched Updates** — Multiple entity deltas packed into single `SvEntityUpdateBatch` packets (~90% header overhead reduction vs per-entity packets). ~50 deltas packed into 2-3 batched packets per tick.
- **`SvEntityEnterBatch` Coalesce (v9)** — Initial replication sends one batch packet per `MAX_PAYLOAD_SIZE` budget instead of one `SvEntityEnter` per entity. 231 entities at ~100 B → ~20 batch packets vs 231 individual reliables. Batch is critical-lane (bypasses congestion).
- **Tiered Frequency** — Near 20 Hz / Mid 7 Hz / Far 4 Hz / Edge 2 Hz. HP + deathState changes force-sent regardless of tier. Near tier covers full viewport diagonal (40 tiles / 1280px) for smooth visible-mob updates.
- **Scene-Scoped Broadcasts** — Combat packets (skill results, auto-attacks, DoT ticks, emoticons) are scene-filtered, not global. 10 broadcast sites converted from global → scene-scoped. `SvCombatEvent` demoted ReliableOrdered → Unreliable (reduced queue saturation ~30–40 reliables/sec).
- **Scene Population Sync** — `SvScenePopulated` handshake ensures loading screen stays up until all initial entity data arrives. Eliminates mob pop-in after zone transitions. 5s client-side safety timeout.
- **Ghost Lifecycle** — Robust enter/leave/destroy pipeline with `recentlyUnregistered_` bridge, `processDestroyQueue("scope")` with per-type breakdown, full disconnect cleanup.
- **NPC Replication** — `SvEntityEnterMsg` extended with npcId + npcStringId + targetFactions; entityType=2 uses `createGhostNPC` factory (no `EnemyStatsComponent`, keeps NPCs out of mob spatial hash).
- **Character Flags Mirror (v12)** — `SvCharacterFlagsSnapshot` (login) + `SvCharacterFlagDelta` (per-mutation) replicate `CharacterFlagsComponent.flags` so dialogue `HasFlag` conditions evaluate locally without server round-trips.

</details>

<details>
<summary><b>💾 Persistence & Database</b></summary>

| Layer | Detail |
|-------|--------|
| **Circuit Breaker** | 3-state (Closed → Open → HalfOpen), 5 failures → 30s cooldown, single-probe pattern |
| **Priority Flushing** | 4 tiers: IMMEDIATE (0s — gold/inventory/trades), HIGH (5s — level-ups/PK/zone transitions), NORMAL (60s — position), LOW (300s — pet/bank). 1s dedup, 10/tick drain |
| **Auto-Save** | 30s staggered per-player with `forceSaveAll=true`. Maximum 30s data loss window on crash. Event-triggered HIGH priority saves on zone transition and level up |
| **Fiber-Based `DbDispatcher`** | Header-only, runs on JobSystem worker threads (2 workers). Covers async player load (18 queries as single job), disconnect saves, market browse/list, persistence saves, maintenance, expired-death cleanup, and all 5 MetricsCollector DB ops. **Zero game-thread blocking.** |
| **Async Saves** | Disconnect saves snapshot all data instantly on game thread, dispatch single-transaction write to worker fiber. Epoch bumps invalidate stale in-flight periodic saves. `PlayerLockMap` preserves mutexes across worker fibers (fast-reconnect stale-data protection) |
| **Dirty Flags** | `PlayerDirtyFlags` at **95 mutation sites**. Async error re-dirties for retry. Batched mob death persistence (single DB transaction per scene per tick regardless of kill count — 27 round-trips → 1) |
| **Connection Pool** | Thread-safe (min 5, max 50, +10 overflow). Per-tick DB call diagnostics via `DbPool::Guard` RAII (elapsed + call count in slow-tick logs) |
| **Async Player Load** | Fiber-based non-blocking player data load on connect — 18 queries packed into single `PlayerLoadResult` job. `playerEntityId == 0` gates packet handlers during load. Zero tick stalls during login storms |
| **Persistence Contract Test** | `tests/test_persistence_contract.cpp` round-trips every mutable column in characters / inventory / bank / pets / costumes / quests / skills against a live DB (gated on `FATE_DB_HOST` env). Catches save-method drift across every subsystem repository |

</details>

<details>
<summary><b>🛡️ GM Command System</b></summary>

`GMCommandRegistry` with `AdminRole` enum (Player / GM / Admin). **44 commands across 8 categories**:
- **Player Management** — kick / ban / permaban / unban / mute / unmute / whois / setrole
- **Teleportation** — tp / tphere / goto
- **Spawning** — spawnmob / listzones / makezone / movezone / deletezone / editzone / respawnzones / clearmobs
- **Economy** — additem / addgold / setlevel / addskillpoints / setopals / setgold
- **GM Tools** — announce / dungeon / invisible / god / sessions / heal / revive
- **Server** — shutdown (configurable countdown + cancel) / reloadcache / vfs_status
- **Monitoring** — serverstats / netstats / bufferstats / scenecheck / spawnstats / bosses / anomalies
- **Debug + Social + Help** — buff / roll / admin / spectate

Ban/unban fully DB-wired with timed expiry. Invisibility uses replication visibility filter. God mode blocks damage at all 3 paths. Monitoring commands pull from `MetricsCollector::snapshot(gameTime)`. Server-initiated disconnect via `SvKick` (0xCC) with typed kickCode + reason — replaces silent `removeClient()` across GM commands, duplicate-login detection, and server shutdown. Slow tick profiling with severity classification (`[minor]` >50ms through `[CRITICAL]` >10s), per-tick DB call diagnostics, and 7-section breakdown.

</details>

---

## ⚙️ Editor (Dear ImGui)

Custom polished dark theme — Inter font family (14px body, 16px SemiBold headings, 12px metadata) via FreeType LightHinting.

<details>
<summary><b>🎯 Core Editor Features</b></summary>

- **Scene Viewport Toolbar (v2)** — ▶️ Play / Resume / ⏸️ Pause / ⏹️ Stop, 👁️ Observe / Stop Obs, 🗺️ Scene dropdown (scans `assets/scenes/*.json`), and a right-aligned FPS readout. Camera state preserved across Play↔Pause↔Stop transitions. Observer hooks expose `AppConfig::onObserveStart` / `onObserveStop` so downstream apps can swap the default local-preview behavior for their own (e.g., network spectate).
- **File Menu + Ctrl+S (v2)** — New Scene (gated `!inPlayMode_`), Open Scene submenu (lists `assets/scenes/*.json`), Save (Ctrl+S, gated on a current scene path), and Save As... (validated name input). Every editor-authored JSON write goes through `engine/core/atomic_write.{h,cpp}::writeFileAtomic` — `.tmp` + rename, parent-dir auto-create, copy+remove fallback for cross-volume targets, tmp cleanup on failure.
- **Entity Hierarchy** — Grouped by name+tag, color-coded (player/ground/obstacle/mob/boss), error badges, tree indentation guides.
- **Live Inspector** — Edit all 56 component types live with **full undo/redo**. Sprite preview thumbnails. Reflection-driven generic fallback via `FATE_REFLECT`. SemiBold headings, separator lines.
- **Scene Interaction** — Click to select (depth-priority, closest-center), drag to move, sticky selection. Ground tiles locked (inspect-only). Entity selection auto-clears if destroyed by gameplay/network/undo.
- **Create / Delete / Duplicate** — Menu + keyboard shortcuts, deep copy via JSON serialization, locked entity protection.
- **8 Tile Tools** — Move (W), Resize (E), Rotate (R), Paint (B), Erase (X), Flood Fill (G), RectFill (U), LineTool (L). All tool-paused-only with compound undo. Collision-layer Rect/Line tools now stamp without requiring a selected palette tile, surfacing missing-precondition status in the HUD instead of silently dropping clicks.
- **Play-in-Editor** — Green/Red Play/Stop buttons. Full ECS snapshot + restore round-trip. Camera preserved. Ctrl+S blocked during play.
- **200-action Undo/Redo** — Tracks moves, resizes, deletes, duplicates, tile paint, all inspector field edits. Handle remap after delete+undo.
- **Input Separation** — Clean priority chain: Paused = ImGui → Editor → nothing. Playing = ImGui (viewport-excluded) → UI focused node → Game Input. Tool shortcuts paused-only, Ctrl shortcuts always. Key-UP events always forwarded to prevent stuck keys.
- **Device Profiles** — 29 device presets (iPhone SE through iPhone 17 Pro, iPad Air/Pro, Pixel 9, Samsung S24/S25, desktop resolutions, ultrawide). Safe area overlay with notch/Dynamic Island insets. `setInputTransform(offset, scale)` maps window-space to FBO-space for correct hit testing across all resolutions.

</details>

<details>
<summary><b>🧩 Panels & Browsers</b></summary>

- **Asset Browser** — Unity-style: golden folder icons, file type cards with colored accent strips, sprite thumbnails with checkerboard, breadcrumb nav, search, lazy texture cache, drag-and-drop, context menu (Place in Scene / Open in Animation Editor / Open in VS Code / Show in Explorer).
- **Animation Editor** — Aseprite-first import pipeline with auto-sibling discovery, layered paper-doll preview (5-layer composite), variable frame duration, onion skinning, content pipeline conventions. Sprite Sheet Slicer (color-coded direction lanes, hit frame "H" badges, mousewheel zoom, frame info tooltips). 3-direction authoring → 4-direction runtime. See details below.
- **Tile Palette** — Recursive subdirectory scan, scrollable grid, brush size (1-5), 4-layer dropdown (Ground / Detail / Fringe / Collision), layer visibility toggles.
- **Dialogue Node Editor** — Visual node-based dialogue trees via imnodes. Speaker/text nodes, choice pins, JSON save/load (atomic), node position persistence.
- **UI Editor** — Full WYSIWYG for all 64 widget types: colored type-badge hierarchy, property inspector for every widget, selection outline, viewport drag, undo/redo with full screen JSON snapshots. Ctrl+S dual-save + hot-reload safe pointer revalidation.
- **Network Panel** — Editor dock surfacing client-side metrics: protocol banner (`v12`), encryption status, RTT (color-coded), reliable queue depth (color-coded against 2048 cap), dropped-non-critical count, AOI entity count, host:port, Connect/Disconnect button.
- **Paper Doll Panel** — Live composite preview with Browse-to-assign workflow for body/hair/equipment sprites.
- **+ 7 more** — Log Viewer, Memory Panel (arena/pool/frame visualization via ImPlot), Command Console, Post-Process Panel, Project Browser, Scene Management, Debug Chrome Panel.

</details>

<details>
<summary><b>🎬 Animation Editor Deep Dive</b></summary>

Full visual animation authoring with an Aseprite-first import pipeline and layered paper-doll preview.

**🔗 Aseprite Import Pipeline:**
- File → Import Aseprite JSON with native file dialog (no manual path typing)
- Auto-discovers `_front` / `_back` / `_side` siblings and merges into unified multi-direction result
- Parses `frameTags` for state names, extracts per-frame durations, detects hit frames from slice metadata

**🖼️ Enhanced Frame Grid:**
- Color-coded direction lanes (blue=down, green=up, yellow=side)
- Hit frame "H" badge with right-click toggle, mousewheel zoom (0.5x–8x)
- Frame info tooltips, quick templates (New Mob / New Player), `.meta.json` auto-save (atomic)

**🧍 Layered Paper-Doll Preview:**
- 5-layer composite (Body / Hair / Armor / Hat / Weapon) from `PaperDollCatalog`
- Class presets (Warrior/Mage/Archer), per-layer visibility toggles, direction selector, preview zoom

**🎞️ Additional Features:**
- Variable per-frame ms timing (imported from Aseprite, editable in UI)
- Onion skinning (prev/next frames at 30% alpha)
- Keyboard shortcuts: Space=play/pause, Left/Right=step, H=toggle hit frame
- Content pipeline: `assets/sprites/{class}/{class}_{layer}_{direction}.png` with shared `.meta.json`
- `tryAutoLoad()` with suffix-stripped fallback — entities animate automatically with zero manual configuration

</details>

---

## 📈 Recent Engineering Wins

Concrete performance, security, and reliability gains shipped in the latest phases (Sessions 95-120).

| Win | Impact |
|---|---|
| 🆕 **Phase 3C ambient NPC mini-chains shipped (S119)** | 30 new quests Q730-Q822 across 10 chains, ~6,500 brutal kills, 12 NPC dialogue rewrites with Greta/Brae state-machine pattern. Q721 cameo precedence preserved on Vess/Ash/Mora via `autoAdvance` ordering. **1599/1599 tests / 10,585 assertions green.** Zero migrations, zero protocol bump — entirely additive content using existing assets |
| 🆕 **`item_definitions` type-taxonomy hygiene pass (S120)** | Closed 4 long-standing data-shape gaps: lowercase `type='material'` (1 row), empty-subtype Materials (17 rows reclassified), `Material/Material` tautology (1 row), `type='Equipment'` orphan ladder (24 rows deleted in S118). DB landed at 816 items across 6 canonical types and 22 Material subtypes. Test seeder patched to `ON CONFLICT DO UPDATE` so future fixture writes self-heal |
| 🆕 **Server-authoritative PvP zone gate restored (S115)** | `combat_handler.cpp` was reading `CharacterStats::isInPvPZone` — a per-character flag that defaulted false and was never written anywhere on the server. PvP was silently disabled game-wide. Wired `inSafeZone` to `sceneCache_.isPvPEnabled(currentScene)`, deleted 3 dead-code paths, restored a broken `MetricsCollector` counter, added 8 new contract tests |
| 🆕 **Atomic-save swap-conflict fix (S116)** | `Inventory::moveItem` is implemented as `std::swap` — both items keep their `instance_id`s, only `slot_index` exchanges. Re-save tripped `uq_character_inventory_slot` partial unique index because Postgres validates per-statement. Fix: NULL the position columns on preserved rows between the orphan-DELETE and UPSERT loop. Two new regression tests cover top-slot + bag-slot swaps |
| 🆕 **Loot economy rebalance (S117, migrations 098+099)** | Squirrel `P(any item) = 89.3% → 20%` via item-by-item drop cuts. 4 `consumable_*_potion` duplicates retired (15 loot rows + 4 item_defs). 317 non-equipment items reset to `level_req=1`. 109 non-boss potion drops capped at 5%/qty 1. 14 mid-tier mobs tier-shifted (small→medium / medium→large). Boss potion drops intentionally untouched |
| **Demo v2 — Play / Observe / Scene-dropdown / File menu / Ctrl+S** | Open-source `FateDemo` reaches feature parity with the proprietary editor's runtime control loop. Configurable `AppConfig::onObserveStart` hook lets downstream apps swap in network spectate or other observer flows |
| **Quest-tree framework + 167 dialogue-tree quests + interact-site framework** | Every quest is a JSON branching tree with state-aware NPC pickers, `ObjectiveType::Interact` for cairns/altars/shrines, and Honor + Opals on every turn-in. 8 milestone collections fire on lifetime-quest thresholds |
| **PROTOCOL v9 → v10 → v11 → v12 in 3 sessions** | v9: 64-bit ACK window, `SvEntityEnterBatch` coalesce, `CmdAckExtended`, epoch-gated rekey. v10: interact-site packets. v11: site-string-id swap (PIDs add zero value when sites have stable string IDs). v12: `CharacterFlags` client mirror so dialogue `HasFlag` conditions evaluate locally. **Five subsequent sessions shipped without a bump** — protocol surface has stabilized |
| **DB-backed `auth_sessions` + cross-process LISTEN/NOTIFY kick** | Sessions survive process crashes. Multi-process deployment ready: login on node B kicks the existing session on node A within ~1s via Postgres NOTIFY. Replaces three in-memory ServerApp maps |
| **Fail-closed encrypt symmetry on both client + server send paths** | Drops + LOG_ERROR on encrypt failure rather than silently falling back to plaintext (which the peer would reject and force infinite retransmits) |
| **`AuthPhase` state machine** | `HandshakePending` → `ProofReceived` → `Authenticated`. Non-system handlers reject packets until proof verifies — closes the theoretical window where malformed proof bytes could reach game logic |
| **Atomic write helper for every editor JSON save** | Scene saves, UI screens, dialogue nodes, animation templates / framesets / packed meta / `.meta.json` siblings — all route through `writeFileAtomic` (`.tmp` + rename + cross-volume copy fallback). Failed Save-As no longer corrupts destination |
| **`IAssetSource` + PhysicsFS VFS** | Unified read path for textures, JSON, shaders, scenes, audio, dialogue, server scene scans, shutdown config. Two implementations (DirectFs + Vfs), behind `option(FATE_USE_VFS …)`. 1507/1507 tests pass under both flag states |
| **Shipping-build CI guard** — `scripts/check_shipping.ps1` wraps `VsDevCmd.bat` + `cmake --preset x64-Shipping`; every `imgui.h`/`ImGui::*`/editor member requires explicit `FATE_SHIPPING`/`EDITOR_BUILD` guard | Stripped ImGui-free shipping exe verified at 5.7 MB; regressions fail fast in CI |
| **Persistence contract test** — every mutable column in characters / inventory / bank / pets / costumes / quests / skills round-trips against a live DB | Catches save-method drift across every subsystem repository (gated on `FATE_DB_HOST` env so CI without DB still passes). `TestAccountGuard` RAII wrapper added in S120 ensures `ctest_*` rows clean up on `REQUIRE`-throw unwind |
| **`MAX_STACK_SIZE = 9999` global removed** | `Inventory::addItem` / `moveItem` / `moveBagItem` honor per-item `max_stack` and split oversized drops into multiple capped chunks. 25 server call sites + 8 test files updated. `canAddItem` simulates the same routing without mutation for visibility-condition checks |
| **Critical-lane bypass extended to `SvEntityEnterBatch`** | 9 load-bearing opcodes (was 8) bypass reliable-queue congestion check — death overlays + initial-entry replication never get strangled under load |
| **`MetricsCollector` sync DB → async `DbDispatcher`** (5 methods) | Eliminated invisible ~300–1500ms periodic stall that manifested as client-side ~6.5s mob-freeze stutter |
| **UDP socket buffers** 64KB → 1MB (`SO_RCVBUF`/`SO_SNDBUF`) | Silent packet drops during burst replication eliminated |
| **Pending-packet queue cap** 256 → 2048 | Handles initial replication bursts in 231-entity scenes without drops |
| **Packet buffers** 4KB → 16KB across 9 handler files | Large inventory-sync / collection-sync / market-listings payloads no longer truncated |
| **Mob death persistence** batched per scene/tick | 27 DB round-trips → 1 transaction (fixed 22.7s AOE freeze) |
| **`ArchetypeStorage`** reserve 256 → 4096 + no-`Archetype&`-across-`emplace_back` | Relogin crash eliminated; client hit archetype #759 mid-migration |
| **Client frame pacer** (VSync off + `SDL_Delay` @ 240 FPS target) | MSVC Debug→Release profile + pacer restored 94 → 250 FPS on Win11/DWM |

---

## ⚠️ Known Issues

These are tracked issues in the open-source engine build. Contributions addressing any of these are welcome.

**Build warnings (non-blocking):**
- Unused parameter warnings in virtual base class methods (`world.h`, `app.h`) — intentional empty defaults for overridable hooks
- `warn_unused_result` on `nlohmann::json::parse` in `loaders.cpp` — the call validates JSON syntax; return value is intentionally discarded

**Architectural:**
- **AOI (Area of Interest) is disabled** — two bugs remain: boundary flickering when entities cross cell edges, and empty `aoi.current` set on first tick. Replication currently sends all entities. Fix requires wider hysteresis band and minimum visibility duration.
- **Fiber backend on non-Windows** uses minicoro, which is less battle-tested than the Win32 fiber path. Monitor for stack overflow on deep call chains.
- **Metal shader still loads from disk** under `FATE_USE_VFS=ON` — `gfx::Device::createShaderFromFiles` needs a from-memory entry point before the VFS flag flips on Apple platforms.

---

## 🌱 From Engine Demo to Full Game

The open-source repo builds and runs as an editor/engine demo. To develop a full game on top of this engine, you would create the following directories (which the CMake system auto-detects):

**Game Logic (`game/`):**
- `game/components/` — Game-specific ECS components (transform, sprite, animator, colliders, combat stats, inventory, equipment, pets, factions, **interact sites**, etc.)
- `game/systems/` — Game systems that operate on components (combat, AI/mob behavior, skill execution, spawning, loot, party, nameplates, **interact-site triggers**, etc.)
- `game/shared/` — Data structures shared between client and server (item definitions, faction data, skill tables, mob stats, **dialogue trees**, **quest definitions**)
- `game/data/` — Static game data catalogs (paper doll definitions, skill trees, enchant tables, opals shop catalog)
- An entry point (`game/main.cpp` or similar) with a class inheriting from `fate::App`

**Server (`server/`):**
- Request handlers for every game action (auth, movement, combat, trade, inventory, chat, party, guild, arena, dungeons, **interact sites**, **bounty board**)
- Database repositories (PostgreSQL via libpqxx) including **session repository + listener** for cross-process kicks
- Server-authoritative game state, validation, and anti-cheat

**Content (`assets/`):**
- `assets/sprites/` — Character sheets, mob sprites, item icons, UI art, skill effects
- `assets/tiles/` — Tileset images for the tilemap renderer
- `assets/audio/` — Sound effects and music
- `assets/prefabs/` — Entity prefab definitions (JSON)
- `assets/scenes/` — Scene/map data files (JSON)
- `assets/dialogue/` — Branching dialogue trees: `quests/` per-quest trees, `npcs/` ambient trees, `sites/` interact-site trees

**Tests (`tests/`):**
- Unit and integration tests using doctest

The engine's `#ifdef FATE_HAS_GAME` compile guards allow it to build cleanly both with and without the game layer. When `game/` sources are present, CMake defines `FATE_HAS_GAME` and builds the full `FateEngine` executable instead of the `FateDemo` target.

---

## ⚡ Building & Targets

All core dependencies are fetched automatically via CMake FetchContent — **zero manual installs required** for the engine and demo.

```bash
# Engine + Demo (open-source, no external deps):
cmake -B build
cmake --build build

# Full game build (requires vcpkg for OpenSSL, libpq, libsodium, freetype):
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug

# Shipping build (strips editor + ImGui, release optimizations, TLS cert pinning enforced):
cmake --preset x64-Shipping
cmake --build --preset x64-Shipping

# VFS-enabled build (PhysicsFS-backed asset reads, optional .pak overlay):
cmake -B build -DFATE_USE_VFS=ON
cmake --build build

# Shipping CI sanity (wraps VsDevCmd.bat; fails fast on any imgui/editor leak):
pwsh scripts/check_shipping.ps1                        # defaults: x64-Shipping + FateEngine
pwsh scripts/check_shipping.ps1 -Preset x64-Release    # dev-release verify
pwsh scripts/check_shipping.ps1 -Target FateServer     # server-only rebuild
```

> 💡 **Shipping guard rule:** every `#include "imgui.h"` / `ImGui::*` / `ImVec*` / editor-only `Editor::instance()` member is wrapped in `#ifndef FATE_SHIPPING` (ImGui) or `#ifdef EDITOR_BUILD` (editor-only members). Headers never include ImGui directly — UI-hover queries route through `UIManager::pressedNode()` instead. Result: a **5.7 MB** `FateEngine.exe` with zero ImGui symbols.

### Output Targets

| Target | Description | Availability |
|--------|-------------|--------------|
| **fate_engine** | Core engine static library | Always (open-source) |
| **FateDemo** | Minimal demo with editor UI + Play/Observe/Scene-dropdown | Open-source build |
| **FateEngine** | Full game client | When `game/` sources present |
| **FateServer** | Headless authoritative server | When `server/` + PostgreSQL present |
| **fate_tests** | 1,621 unit tests (doctest, 10,585 assertions) | When `tests/` sources present |

### Platform Matrix

| Platform | Status | Details |
|----------|--------|---------|
| **Windows** | Primary | MSVC (VS 2026 / VS 18), primary development target |
| **macOS** | Supported | CMake, full Metal rendering, minicoro fibers, ProMotion 120fps |
| **iOS** | Pipeline Ready | CMake Xcode generator, Metal/GLES 3.0, CAMetalLayer, TestFlight script, DNS64/NAT64 |
| **Android** | Pipeline Ready | Gradle + NDK r27, SDLActivity, `./gradlew installDebug` |
| **Linux** | CI Verified | GCC 14, Clang 18 — builds green on every push |

---

## 🧪 Testing

The engine maintains exceptional stability through **1,621 test cases** across **218 test files**, powered by `doctest`. **10,585 assertions**, all green.

```bash
# Run all tests:
./build/Debug/fate_tests.exe

# Target specific suites:
./build/Debug/fate_tests.exe -tc="Death Lifecycle"
./build/Debug/fate_tests.exe -tc="PacketCrypto"
./build/Debug/fate_tests.exe -tc="PersistenceQueue*"
./build/Debug/fate_tests.exe -tc="SkillVFX*"
./build/Debug/fate_tests.exe -tc="CollisionGrid*"
./build/Debug/fate_tests.exe -tc="AsepriteImporter*"
./build/Debug/fate_tests.exe -tc="PetSystem*"
./build/Debug/fate_tests.exe -tc="PetAutoLoot*"
./build/Debug/fate_tests.exe -tc="InteractSite*"
./build/Debug/fate_tests.exe -tc="SessionRepository*"
./build/Debug/fate_tests.exe -tc="DialogueTree*"
./build/Debug/fate_tests.exe -tc="AtomicWrite*"
```

Coverage spans: combat formulas, encryption/decryption, entity replication, inventory operations, skill systems, quest progression (incl. honor formula + milestone collections), economic nonces, arena matchmaking, dungeon lifecycle, VFX pipeline, compressed textures, UI layout, collision grids, async asset loading, Aseprite import pipeline, animation frame durations, costume system, collection system, pet system, pet auto-loot, **interact-site validator + packets + dialogue conditions + Q155/Q700 retrofits**, **session repository + listener cross-process kick**, **atomic-write durability**, **persistence contract** (live-DB column round-trip), and more.

---

## 📐 Architecture at a Glance

```
engine/                    # 85,391 LOC — Core engine (20 subsystems, 373 files)
 render/                   #   Sprite batching, SDF text, lighting, bloom, paper doll, VFX, Metal RHI
 net/                      #   Custom UDP (v12), Noise_NK crypto, AuthProof, replication, AOI, interpolation
 ecs/                      #   Archetype ECS (4096 reserve), 56 components, reflection, serialization
 ui/                       #   64 widgets, JSON screens, themes, viewport scaling, panel chrome
 editor/                   #   ImGui editor, undo/redo, Aseprite animation editor, asset browser, Play/Observe/Scene toolbar
 tilemap/                  #   Chunk VBOs, texture arrays, Blob-47 autotile, 4-layer
 scene/                    #   Async loading, versioning, prefab variants
 asset/                    #   IAssetSource (DirectFs / Vfs), hot-reload, fiber async, LRU VRAM cache, compressed textures
 input/                    #   Action map, touch controls, 6-frame combat buffer
 audio/                    #   SoLoud, 3-bus, spatial audio, 10 game events
 job/                      #   Fiber system, MPMC queue, scratch arenas
 memory/                   #   Zone/frame/scratch arenas, pool allocators
 spatial/                  #   Fixed grid, spatial hash, collision bitgrid
 core/                     #   Structured errors, Result<T>, CircuitBreaker, atomic_write
 particle/                 #   CPU emitters, per-particle lifetime/color lerp
 platform/                 #   Device info, RAM tiers, thermal polling
 profiling/                #   Tracy integration, spdlog, rotating file sink
 vfx/                      #   SkillVFX player, JSON definitions, 4-phase compositing
 vfs/                      #   PhysicsFS, ZIP mount, overlay priority
 telemetry/                #   Metric collection, JSON flush, HTTPS stub

game/                      # 40,559 LOC — Game logic layer (117 files)
 shared/                   #   19,148 LOC of pure gameplay across 75 files (zero engine deps)
   combat_system           #   Hit rate, armor, crits, class advantage, PvP balance, scene-cache PvP gate
   skill_manager           #   60+ skills, cooldowns, cast times, resource types
   mob_ai                  #   Cardinal movement, threat, leash, L-shaped chase
   status_effects          #   DoTs, buffs, shields, source-tagged removal
   inventory               #   16 slots, equipment, nested bags, stacking, UUID instances, per-item max_stack
   trade_manager           #   2-step security, slot locking, atomic transfer
   arena_manager           #   1v1/2v2/3v3, matchmaking, AFK detection, honor
   gauntlet                #   Event scheduler, divisions, wave spawning, MVP
   faction_system          #   5 factions, guards, innkeeper, faction-aware targeting
   pet_system              #   Active pet equip, XP share, stat bonuses, auto-loot
   opals_system            #   Currency, shop catalog, direct-credit drops, AdMob rewards
   quest_manager           #   167 quests, dialogue trees, interact sites, honor + opals + Phase 3C ambient grind
   dialogue_registry       #   npcs/ + quests/ + sites/ scan, condition evaluator
   ...                     #   +25 more systems (guild, party, crafting, collections, etc.)
 components/               #   ECS component wrappers + InteractSiteComponent + CharacterFlagsComponent + QuestGiverComponent
 systems/                  #   12 ECS systems (combat, render, movement, mob AI, pet follow, spawn, interact site...)
 data/                     #   Paper doll catalog, NPC definitions, quest data, opals shop catalog

server/                    # 36,827 LOC — Headless authoritative server (140 files)
 handlers/                 #   46 packet handler files (split from monolith)
 db/                       #   23 repository files, 10 startup caches, pool, fiber DbDispatcher, session_repo + listener
 cache/                    #   Item/loot/recipe/pet/costume/collection/guard caches
 auth/                     #   TLS auth server (bcrypt + dummy-hash timing-oracle defense, starter equipment, login rate limiting, CSPRNG tokens)
 *.h/.cpp                  #   ServerApp, SpawnCoordinator, DungeonManager, RateLimiter, GM commands, MetricsCollector, ShutdownManager

tests/                     # 30,619 LOC — 1,621 test cases across 218 files (10,585 assertions, all green)

ads_ssv_server/            # Standalone rewarded-video verifier (ECDSA-signed HTTPS callbacks)
Docs/migrations/           # 111 numbered SQL migrations (schema + content seeds)
assets/dialogue/           # 183 dialogue trees: 124 quests + 33 NPCs + 26 sites
scripts/                   # check_shipping.ps1 (CI), run_server.ps1 (launcher)
```

---

## 🤝 Contributing

Contributions to the engine core are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on submitting issues, pull requests, and the `FATE_HAS_GAME` guard requirements for engine code.

## 📜 License

Apache License 2.0 — see [LICENSE](LICENSE) for details.

---

<div align="center">

<br>

<img src="https://img.shields.io/badge/%E2%9A%94%EF%B8%8F_Forged_by-Caleb_Kious-8B5CF6?style=for-the-badge&logoColor=white" alt="Forged by Caleb Kious" />

<br>

<sub>**Engine Architecture** | **Networking & Crypto** | **Server Systems** | **Editor Tooling** | **Cross-Platform** | **All Game Logic** | **Solo Developer**</sub>

<br><br>

<img src="https://img.shields.io/badge/C%2B%2B-193%2C000%2B_lines-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++ LOC" />
<img src="https://img.shields.io/badge/files-848-2ea44f?style=flat-square" alt="Files" />
<img src="https://img.shields.io/badge/tests-1%2C621_passing-2ea44f?style=flat-square" alt="Tests" />
<img src="https://img.shields.io/badge/assertions-10%2C585-success?style=flat-square" alt="Assertions" />
<img src="https://img.shields.io/badge/protocol-v12-blueviolet?style=flat-square" alt="Protocol" />
<img src="https://img.shields.io/badge/crypto-Noise__NK%20%2B%20AuthProof%20%2B%20DB__sessions-critical?style=flat-square" alt="Crypto" />
<img src="https://img.shields.io/badge/platforms-5-orange?style=flat-square" alt="Platforms" />
<img src="https://img.shields.io/badge/game_systems-50%2B-blueviolet?style=flat-square" alt="Game Systems" />
<img src="https://img.shields.io/badge/UI_widgets-64-ff69b4?style=flat-square" alt="UI Widgets" />
<img src="https://img.shields.io/badge/quests-167-E040FB?style=flat-square" alt="Quests" />
<img src="https://img.shields.io/badge/factions-5-red?style=flat-square" alt="Factions" />
<img src="https://img.shields.io/badge/scenes-27-FFB300?style=flat-square" alt="Scenes" />
<img src="https://img.shields.io/badge/NPCs-67_placed-orange?style=flat-square" alt="NPCs" />
<img src="https://img.shields.io/badge/items-816-yellowgreen?style=flat-square" alt="Items" />
<img src="https://img.shields.io/badge/admin_commands-44-4CAF50?style=flat-square" alt="GM Commands" />
<img src="https://img.shields.io/badge/handlers-46-9cf?style=flat-square" alt="Handler Files" />
<img src="https://img.shields.io/badge/DB_repos-23-informational?style=flat-square" alt="DB Repos" />
<img src="https://img.shields.io/badge/migrations-111-FF7043?style=flat-square" alt="Migrations" />
<img src="https://img.shields.io/badge/dialogue_trees-183-9C27B0?style=flat-square" alt="Dialogue Trees" />

<br><br>

[![GitHub](https://img.shields.io/badge/GitHub-wFate-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/wFate)

<br>

</div>
