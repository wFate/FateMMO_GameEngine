<div align="center">

# ⚔️ FateMMO Game Engine

[![CI](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/wFate/FateMMO_GameEngine?style=flat-square&label=🎮%20Release&color=gold)](https://github.com/wFate/FateMMO_GameEngine/releases/tag/FateMMOv2)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?style=flat-square&logo=cplusplus)
![Lines of Code](https://img.shields.io/badge/LOC-204%2C500%2B-brightgreen?style=flat-square)
![Files](https://img.shields.io/badge/files-865-2ea44f?style=flat-square)
![Tests](https://img.shields.io/badge/tests-1%2C687_passing-brightgreen?style=flat-square)
![Protocol](https://img.shields.io/badge/protocol-v17-blueviolet?style=flat-square)
![Quests](https://img.shields.io/badge/quests-167-E040FB?style=flat-square)
![Migrations](https://img.shields.io/badge/SQL_migrations-120-FF7043?style=flat-square)
![Security](https://img.shields.io/badge/security-Noise__NK%20%2B%20AuthProof%20%2B%20DB__sessions-critical?style=flat-square)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20iOS%20%7C%20Android%20%7C%20Linux%20%7C%20macOS-orange?style=flat-square)

**Production-grade 2D MMORPG engine built entirely in C++23.** Engineered for mobile-first landscape gameplay with a fully integrated Unity-style editor, server-authoritative multiplayer architecture, and Noise_NK encrypted custom networking — all from a single codebase, zero middleware, zero third-party game frameworks.

> 🧱 **204,500+ lines** across **865 files** — engine (92K), game (41K), server (38K), tests (32K)

🌐 [**www.FateMMO.com**](https://www.FateMMO.com) &nbsp;·&nbsp; 🎬 [**Watch the Showcase**](https://www.youtube.com/watch?v=9zS-RVbranE)

</div>

---

### 🚧 v3 Incoming — Loot, Loadouts, Chrome & Living Economy

> **Demo v3 is around the corner.** Nine back-to-back sessions just closed: a **full loot economy rebalance** (6 migrations rebanding the 7-tier enhance-stone ladder), the **drag-driven enchant flow** (protected stones are now real inventory items, no more ephemeral client-side staging), the **Merchant Pass + Recall Scroll** Opals SKUs, the **polymorphic 20-slot skill loadout** that lets players drag potions onto their skill bar and fire them like spells, **chrome'd HUD bespokes** (DPad / SkillArc / SettingsPanel / MenuTabBar — 49 styled fields), the **bag-item NPC sell pipeline** (sell straight from bag sub-slots, no retrieve dance), and a **drag-bind pipeline repair** that finally wires the SkillLoadoutStrip end-to-end. **Protocol v17** ships with seven new wire packets and a renamed assignment opcode. Tests at **1,687 / 1,688 green**.
>
> 🎯 **Headline drops (since v2):**
> - 🛒 **Bag-Item NPC Sell — `CmdShopSellFromBag = 0x50` (S138, protocol v17)** — Right-click any item *inside* a bag and sell directly to the shopkeeper. New wire packet (`{npcId, bagSlot, bagSubSlot, quantity}`), dedicated server handler that validates bag container + sub-slot + soulbound state + ownership, and a confirm-popup that mirrors the top-level sell flow. Server emits both `SvInventorySync` AND `SvBagContentsForSlot` post-credit so the bag-UI mirror never goes stale. Closes the long-standing "only one item then sell vanishes" symptom — cleanly, by adding the proper one-packet-per-source path rather than overloading the legacy struct.
> - 🪡 **Skill Loadout Strip — Drag-Bind Repair + Decoupling (S139)** — Seven sub-fixes against the S135 `SkillLoadoutStrip`. **Two distinct root causes** in the drop pipeline (UIManager set `dropLocalPos` *after* `acceptsDrop` was queried; `InventoryPanel::onRelease` was clearing the shared `DragPayload` before UIManager could route) plus polish: page dots clickable, slots render as true circles, drop-highlight ring follows the cursor across slots, inventory grid no longer auto-shrinks with strip height/padding. Drag an HP potion from main grid OR from inside an open bag → it binds. Client-only — no protocol bump, no server restart.
> - ✨ **Bespoke Widget Chrome — Checkpoint 8 (S136)** — Chromes the 4 widgets the previous arc skipped because they aren't rectangular shells: `MenuTabBar`, `SettingsPanel`, `DPad`, `SkillArc`. **49 new styled fields** (11+16+9+13) routed through the existing `panelUseChrome_` / `hudUseChrome_` propagation walks — no new manager toggles, no new View-menu items. Chrome path replaces `drawCircle` / `drawRing` / `drawMetallicCircle` with `RoundedRectParams` equivalents; legacy paths preserved verbatim under `else` branches as instant rollback. Defaults mirror legacy colors byte-for-byte.
> - 🧰 **Inventory Bag Panel — Geometry Fix + `bagPanelOffset` Knob (S137)** — Bags of different sizes were positioned all over the place because the brown panel was *centered* in the grid area while the slots inside were *top-anchored*. The two layouts drifted apart with bag size; 10-slot bags pushed their panel above the visible slots. Fix: top-anchored panel + new `Vec2 bagPanelOffset` inspector knob + bag render translates local→absolute consistently. "Can't click top slots" regression closed in the same pass.
> - 🛡️ **Chrome Regression Sweep — Close-X Reliability (S138)** — Six discrete close-X / chrome interaction bugs swept in one session: CollectionPanel's close-X recursed itself into a stack-overflow disconnect; SettingsPanel + GuildPanel (menu-tab copy) + CostumePanel close-Xs were silently no-ops; ShopPanel chrome dropped sell-confirm clicks on the floor; the inventory close-X didn't tear down the surrounding shop UX. Plus the SkillArc Action button no longer bleeds the legacy sword icon through chrome's flat fill, and the AA-edge ring on Attack/PickUp no longer reads as a black border when chrome border is configured to match the fill.
> - 🪡 **Skill-Arc Consumable Bindings (S135)** — Polymorphic 20-slot bar (5 slots × 4 pages) that holds Skills OR Items OR Empty. Drag HP/MP potions, recall scrolls, fated potions, or food onto any slot from inventory or bag overlay. Bindings reference `instance_id` (stable across logout via S116), persist in DB (`character_skill_bar` polymorphic columns), auto-clear when the bound stack is destroyed, and fire per-item-type cooldowns (5s combat potions / 30s premium consumables). New `SkillLoadoutStrip` widget embedded in `InventoryPanel` for drag/drop loadout management. **14 server-side stale-binding sweep sites** wired across trade / sell / market / destroy / bank / consume / craft / bag / revive / teleport / inventory-merge / soul-anchor.
> - ⚒️ **7-Tier Enhance-Stone Economy + Protected Stones as Physical Items (S134)** — Stone tier ladder rebanded in clean 10-level windows. Drag a Protection Scroll onto a base stone → server crafts a real `_protected` item that lives in inventory and survives logout. Drag-driven enchant flow now requires explicit `stoneSlot` from the wire — no more "dragged stone is cosmetic" gap. Risky-tier failure **destroys** equipment instead of flagging `isBroken`. Two confirm-dialog popups gate every attempt. Same-container constraint (inv↔inv or same-bag↔same-bag) on both enchant and craft drags.
> - 🎰 **Loot Rebalance Arc (S130–S133, migs 103-111)** — Average trash drops-per-hour cut from **12.27 → 1.26**; legendary/epic rates corrected on endgame elites; Caverns trash backfill (was starved); enhance-stone systemic +10 level offset bug fixed (every mob now drops the canonical band tier); apex stone supply pyramid (worldboss → boss → trash trickle). Final state: every band cleanly stocked; per-tier trash rate ladder locked in `project_loot_rebalance_targets.md`.
> - 💎 **Merchant Pass + Recall Scroll Wired (S132)** — Premium marketplace pass (30 days listing access, **stacking from current expiry**) gates the Marketplace `ListItem` flow. `recall_scroll` purchasable for opals OR gold. New `max_marketplace_listings` per-character column (defaults 7) so future "Listing Slot Extender" SKUs can `+= grant`. Character-select **PREMIUM pill**, market-panel **`PASS Nd` / `NO PASS` status pill**, and premium-item tooltip descriptions populated client-side.
> - ✨ **Chrome System Default-On (S125–S127)** — 25 per-panel + 3 manager `xxxUseChrome_` toggles flipped to `true`. View-menu items kept as legacy rollback. Inter SemiBold across the board; parchment, HUD-dark, dialog, scrollbar, tab, and tooltip themes all reconciled into one polished aesthetic.

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

| 🏗️ | **204,500+ LOC** of hand-written C++23 across engine, game, server, and tests — no code generation, no middleware, no AAA framework license fees |
|:---:|:---|
| 🔐 | **Noise_NK cryptography** — two X25519 DH ops + XChaCha20-Poly1305 AEAD, forward secrecy, symmetric epoch-gated rekeying, **encrypted AuthProof** so auth tokens never leave the client in the clear |
| 🛡️ | **Hardened v17 protocol** — 64-bit CSPRNG session tokens, timing-safe auth comparisons, anti-replay nonces, 64-bit ACK window, `CmdAckExtended` recovery, DB-backed `auth_sessions` with cross-process `LISTEN/NOTIFY` kick |
| 🎮 | **50+ server-authoritative game systems** — combat, skills, polymorphic loadouts, inventory, trade, guilds, arenas, dungeons, pets, costumes, collections, opals economy, merchant pass marketplace |
| 🪡 | **Polymorphic Skill-Arc Loadout (S135 + S139 drag-bind repair)** — drag potions / recall scrolls onto any of 20 slots (5 × 4 pages) from main inventory OR inside an open bag, bindings reference `instance_id`, persist across logout, per-item cooldowns, **14 stale-binding sweep sites** keep everything coherent across trade/sell/destroy/craft/bank flows. S139 swept the drop-pipeline ordering bug + circle slots + cursor-tracking drop-highlight |
| 🛒 | **Bag-Item NPC Sell — `CmdShopSellFromBag = 0x50` (S138, protocol v17)** — first-class wire packet for selling items that live in bag sub-slots; server emits both inventory + bag-content sync post-credit so the bag mirror never goes stale |
| ⚒️ | **7-Tier Enchant Economy** — drag-driven flow with explicit stone-source on the wire (no cosmetic stone bug), protected stones as **physical inventory items** that persist across logout, risky-tier destroy on failure, confirm-dialog popups gating every attempt |
| ✨ | **Bespoke Widget Chrome — Checkpoint 8 (S136)** — `MenuTabBar` / `SettingsPanel` / `DPad` / `SkillArc` joined the chrome system through 49 new styled fields routed via existing `panelUseChrome_` / `hudUseChrome_` toggles. Legacy `drawMetallicCircle` paths preserved verbatim under `else` for instant rollback |
| ▶️ | **Play / Observe / Scene-dropdown** — full editor-runtime loop in the open-source v2 demo, with snapshot/restore on Play and configurable Observer hooks |
| 🌳 | **Branching dialogue-tree quests** — **167 quests** across 4 tiers + Phase 3C ambient mini-chains, state-aware NPC trees, **interact-site framework** (cairns / altars / shrines / gravestones), Honor + Opals + tier-matched enhance stones on every turn-in |
| 🪦 | **3 Tier-3 narrative arcs** — story-first chains with zero new mobs and zero combat balance changes; one of them is a 3-way fork, another a branching ending |
| ⚔️ | **Phase 3C ambient grind (S119)** — 30 new side quests across 10 NPC chains, themed stat scrolls + Fate's Grace + tier-matched enhance stones on every capstone |
| 🎰 | **Locked loot economy** — 6-mig rebalance arc collapsed avg trash drops/hour from 12.27 → 1.26, fixed systemic +10 enhance-stone offset, apex stone pyramid (worldboss → boss → trash trickle), per-tier ladder targets locked |
| 💎 | **Merchant Pass + Listing Slot extensibility** — premium pass stacks expiry from current end-date, gates marketplace List flow, per-character `max_marketplace_listings` column ready for future Slot Extender SKUs |
| 🗄️ | **DB-driven content engine** — **120 numbered SQL migrations**, **816 hand-tuned items** across 6 canonical types, **17 PostgreSQL repository files** + **12 startup caches** with fiber-based async dispatch (zero game-thread blocking) |
| 🖥️ | **Full Unity-style editor** with live inspector, undo/redo, Aseprite animation editor, atomic-write JSON saves, asset browser, and 29 device profiles |
| 📱 | **5-platform support** from a single codebase — Windows, **macOS (native Metal, ProMotion 120fps)**, iOS, Android, Linux |
| 🧪 | **1,687 automated tests** across 214 test files keeping every subsystem honest (10,973 assertions, all green except one pre-existing tier-bracket fixture) |
| 🎨 | **65+ custom UI widgets** with 42 theme styles + chrome-default-on, JSON-driven screens, viewport scaling, and zero-ImGui shipping builds (5.7 MB stripped exe) |
| 🌍 | **27-scene handcrafted world** with 5 factions, **67 placed NPCs**, faction guards, boss rotations, and Aurora Gauntlet endgame PvP tower |
| 📡 | **Protocol v17 custom UDP** — critical-lane bypass for 9 load-bearing opcodes, `SvEntityEnterBatch` coalesce, 32-bit delta compression, fiber-based async DB dispatch, client-side `CharacterFlags` mirror, `SvConsumableCooldown` poke (0xE9), `CmdCraftProtectStone` (0xEA), `CmdShopSellFromBag` (0x50) |
| 👁️ | **Admin observer/spectate mode** — log in without a character and roam any live scene with full replication |
| 🐾 | **Active pet system** — equip one pet, share 50% XP, gain stat bonuses, auto-loot within radius (Legendary tier: 0.2s tick, 128px) |
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
| **Networking** | Custom reliable UDP (`0xFA7E`, **PROTOCOL_VERSION 17**), **Noise_NK handshake** (two X25519 DH ops) + **XChaCha20-Poly1305 AEAD** encryption (key-derived 48-bit nonce prefix, symmetric rekeying every 65K packets / 15 min, epoch-gated). **Auth hardening**: 64-bit CSPRNG session tokens (libsodium), encrypted `CmdAuthProof` (0xD8) so the token never rides plaintext, timing-safe comparisons, per-account login lockout with exponential backoff, fail-closed encrypt symmetry on both client and server send paths. **DB-backed sessions** (`auth_sessions` table, mig 089) with `LISTEN/NOTIFY session_kicked` cross-process kick. IPv6 dual-stack, 3 channel types, **26-byte header with 64-bit ACK bitfield** (v9+), RTT-based retransmission, 1 MB kernel socket buffers, 2048-slot pending queue with **critical-lane bypass for 9 load-bearing opcodes**, **SvEntityEnterBatch coalesce**, **CmdAckExtended** out-of-window recovery. v13–v17 added `SvConsumableCooldown` (0xE9), `CmdCraftProtectStone` (0xEA), `SvCraftProtectStoneResult` (0xEB), polymorphic `CmdAssignSlot` (0x36), and `CmdShopSellFromBag` (0x50) for bag-source NPC sells |
| **Database** | PostgreSQL (libpqxx), **17 repository files**, **12 startup caches**, **fiber-based `DbDispatcher`** with **bounded backlog + per-tick pump** (S128 — async player load, disconnect saves, metrics, maintenance — zero game-thread blocking, hard cap 8192 with FIFO-preserving cancel-on-drop), connection pool (5–50) + **read-replica fallthrough** (`FATE_READ_DATABASE_URL`) + circuit breaker, priority-based 4-tier dirty-flag flushing, 30s staggered auto-save. `PlayerLockMap` for concurrent mutation serialization. WAL removed — 30s window bounds crash loss. **120 numbered SQL migrations** under `Docs/migrations/`. **816 items** in `item_definitions` across 6 canonical types — Armor 355 / Consumable 222 / Material 148 / Weapon 86 / Container 4 / Currency 1 (vendor-exchange token; gold + opals are first-class account columns, not item rows) — hardened by the S118-S120 type-taxonomy passes. Inventory saves: orphan-DELETE → NULL positions → UPSERT-by-`instance_id` four-step pattern (S116) |
| **ECS** | Data-oriented archetype ECS, contiguous SoA memory, **56 registered components**, generational handles, prefab variants (JSON Patch), compile-time `CompId`, Hot/Warm/Cold tier classification, `FATE_REFLECT` macro with field-level metadata, `ComponentFlags` trait policies, RAII iteration depth guard, 4096-archetype reserve capacity. `World::processDestroyQueue(scope)` tags every flush with caller intent + per-type entity breakdown for diagnosability |
| **Memory** | Zone arenas (256 MB, O(1) reset), double-buffered frame arenas (64 MB), thread-local scratch arenas (Fleury conflict-avoidance), lock-free pool allocators, debug occupancy bitmaps, ImPlot visualization panels |
| **Audio** | SoLoud (SDL2 backend, 32 virtual voices, OGG streaming, 2D spatial audio, 3 buses, 10 game events wired). All loads route through `IAssetSource::readBytes` → `Wav::loadMem(copy=true)` so packaged `.pak` archives work transparently |
| **Async & Jobs** | Win32 fibers / minicoro, **4 workers default** (configurable via `FATE_JOB_WORKERS` env), 32-fiber pool, lock-free MPMC queues, counter-based suspend/resume with fiber-local scratch arenas. Fiber-based async scene, asset, and DB loading — zero frame stalls. `tryPushFireAndForget` non-blocking variant for dispatcher producers; spinning variant retained for blocking-required callers |
| **Spatial** | Fixed power-of-two grid (bitshift O(1) lookup), Mueller-style 128px spatial hash, per-scene packed collision bitgrid (1 bit/tile, negative coord support, `isBlockedRect` AABB queries). Server loads from scene JSON at startup — zero rubber-banding |
| **Asset Pipeline** | Generational handles (20+12 bit), hot-reload (300ms debounced), fiber async decode + main-thread GPU upload, failed-load caching (prevents re-attempts), **`IAssetSource` abstraction** (`DirectFsSource` for loose files / `VfsSource` for PhysicsFS-backed `.pak`), compressed textures (ETC2 / ASTC 4x4 / ASTC 8x8) with KTX1 loader, VRAM-budgeted LRU cache (512 MB, O(N log N) eviction). Behind `option(FATE_USE_VFS …)` so behavior is byte-identical until per-platform flip |

---

## 🗡️ Game Systems

Five factions shape a **27-scene world** — the 🩸 **Xyros** severing the threads of fate, the 🛡️ **Fenor** weaving steadfast eternity, the 🔮 **Zethos** unraveling ancient mysteries, the 👑 **Solis** forging a golden epoch, and the 🕳️ **Umbra** — a secret non-playable faction born from the negative space of the tapestry itself, hostile to all who enter their domain. Each faction fields **3 tiers of named guards** — mage sentries, archer elites, and a boss-tier warden — that auto-aggro enemy-faction players on sight. Bind at **Innkeeper NPCs**, journey through **167 dialogue-tree quests** across 4 tiers + Phase 3C ambient mini-chains, and fight the **Fate Guardian** in the rotating boss arena.

All gameplay logic is fully server-authoritative with priority-based DB persistence, dirty-flag tracking at **95+ mutation sites**, spanning **50+ robust systems** across **75 shared gameplay files** (**19,300+ LOC** of pure C++ game logic — zero engine dependencies). Every system is DB-wired with load-on-connect, save-on-disconnect, and async auto-save.

<details>
<summary><b>🔥 Combat, PvP & Classes</b></summary>

- **Optimistic Combat** — Attack windups play immediately to hide latency; 3-frame windup (300ms), hit frame with predicted damage text + procedural lunge offset. `CombatPredictionBuffer` ring buffer (32 slots). Server reconciles final damage.
- **Combat Core** — Hit rate with coverage system (non-boss mobs `mob_hit_rate=10` for 5-level coverage, bosses `=30` for 15-level — damage floor vs. over-leveled players), spell resists, block, armor reduction (75% cap), 3x3 class advantage matrix (hot-reloadable JSON), crit system with class scaling.
- **Skill Manager** — 60+ skills with skillbook learning, cooldowns, cast-time system (server-ticked, CC/movement interrupts, fizzle on dead target), **polymorphic 20-slot loadout** (Skill / Item / Empty), passive bonuses, resource types (Fury/Mana/None). `SvSkillDefs` sends full class catalog on login.
- **🆕 Skill-Arc Consumable Bindings (S135 + S139 drag-bind repair)** — Drag HP/MP potions, recall scrolls, fated potions, or food onto any of the 20 loadout slots from the inventory grid OR an open bag overlay. Tap on the in-combat HUD arc consumes one stack and triggers a per-item-type cooldown (5s combat / 30s premium). Bindings reference stable `instance_id`s, persist across logout via the polymorphic `character_skill_bar` schema, and auto-clear when the bound stack is destroyed via the **14-site stale-binding sweep**. `SkillLoadout` UI-facing facade owns canonical state; `SkillArc` HUD widget is now a thin renderer over the model. The `SkillLoadoutStrip` widget embedded in the inventory panel got its drop pipeline rebuilt in S139 — `UIManager::handleRelease` now sets `dropLocalPos` *before* `acceptsDrop`, `InventoryPanel::onRelease` no longer clobbers the shared `DragPayload` for strip drops, page dots are clickable, slots render as true circles, and the drop-highlight ring follows the cursor across slots.
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
- **Tier 3 Investigation Chains** — Three story-first arcs (early, mid, and late game) explore the world's deeper lore. One is a 3-way moral fork with a capstone; another is a branching ending. Specifics deliberately omitted — these are designed to surprise the player at the table.
- **🆕 Phase 3C Ambient Mini-Chains (10 chains × 3 quests = 30 side quests)** — Promoted ambient NPCs across the world map become full quest-givers. Currency-only rewards using existing items: gold + EXP + opals + themed stat scrolls + Fate's Grace + tier-matched enhance stones (with protected variants on capstones). Honor scales 3–5× the formula on capstones to make them feel like a milestone.
- **Honor + Opals on Every Turn-In** — `QuestRewards` carries `int honor = 0` (0 = use formula `(5 + reqLvl×2 + chainLen×10) × tierMul`, Starter 1.0 → Adept 2.0) and `int64_t opals = 0` (opt-in). Both are applied in `QuestManager::turnInQuest` after objective verification + Collect/Deliver consumption. Granted rewards echo back to chat as `[Quest]` lines.
- **8 Milestone Collections** — `QuestsCompleted` collection condition fires on every successful turn-in and grants permanent stat bonuses at thresholds.
- **Cross-Zone Delivery Chains** — Multi-NPC delivery arcs span up to 5 zones, handing the player a soulbound quest item from NPC-A to deliver to NPC-B halfway across the world map.
- **Branching NPC Dialogue** — State-aware quest-tree picker at every NPC. Priority: turnIn > inProgress > offer > NPC ambient > legacy greeting. Driven by `QuestGiverComponent.questIds`.
- **`CharacterFlagsComponent` client mirror (v12)** — `SvCharacterFlagsSnapshot` (login) + `SvCharacterFlagDelta` (per-mutation) replicate the player's flag set so dialogue `HasFlag` conditions evaluate locally without round-trips. Enables hard-branching trees on the client.

</details>

<details>
<summary><b>✨ Progression, Items & Collections</b></summary>

- **Fixed Stats & XP** — Gray-through-red level scaling, 0%–130% XP multipliers. Base stats fixed per class to balance the meta, elevated only by gear, collections, and the active pet.
- **Collections System** — DB-driven passive achievement tracking across 3 categories (Items / Combat / Progression) plus the **Quest milestones** category. **30+ seeded definitions** with 8 quest-completion milestones. 9 event trigger points. Permanent additive stat bonuses (11 stat types) with no cap. Costume rewards on completion. `SvCollectionSync` + `SvCollectionDefs` packets.
- **🆕 7-Tier Enchant Economy (S134)** — `mat_enhance_stone_basic → legendary` rebanded into clean 10-level windows (1-10, 11-20, 21-30, 31-40, 41-50, 51-60, 61-70). +1 to +15 enhancement with weighted success rates (+1–8 safe, +9–15 risky with 50% → 2% curve). **Drag-driven flow** — `CmdEnchantMsg` carries explicit `stoneSlot` (no fallback to first-match-by-id; rejects on tier mismatch). Risky-tier failure now **destroys** equipment rather than flagging `isBroken`. Secret bonuses at +11 / +12 / +15. Gold costs scale 100g → 2M. Server-wide broadcast on successful enchant at +9 and above.
- **🆕 Protected Stones as Physical Items (S134)** — Drag a Protection Scroll onto a base enhancement stone → server crafts a real `mat_enhance_stone_<tier>_protected` item that lives in inventory, persists across logout, and survives moves/trades. Atomic post-consume-aware space check (walks main inv + every open bag), rollback on placement failure. Two confirm-dialog popups gate every attempt: `enchant_confirm_dialog` shows `<itemName>\n+N -> +N+1\nStone: <stoneName>\nSuccess: X% (Protected)` — `enchant_protect_confirm_dialog` shows `Combine 1 Protection Scroll\n+ 1 X\nto create 1 Protected X?`. Same-container constraint: only `inv-stone → inv-equip` and `same-bag stone → same-bag equip` drags are wired (cross-container drags silently cancel).
- **Socket System** — Accessory socketing (Ring / Necklace / Cloak), weighted stat rolls (+1: 25% → +10: 0.5%), server-authoritative with re-socket support. 7 scroll items in DB.
- **Core Extraction** — Equipment disassembly into 7-tier crafting cores based on rarity and enchant level (+1 per 3 levels). Common excluded.
- **Crafting** — 4-tier recipe book system (Novice / Book I / II / III) with ingredient validation, level/class gating, gold costs. `RecipeCache` loaded at startup. **Bag-aware** — `canAddItem` verifies sub-slot space before commit.
- **Consumables Pipeline** — **18 effect types** fully wired: HP/MP Potions, SkillBooks (class/level validated), Stat Reset (Elixir of Forgetting), Town Recall (blocked in combat/instanced content), Fate Coins (3→level×50 XP), EXP Boost Scrolls (10%/20%, 1hr, stackable tiers), Beacon of Calling (cross-scene party teleport), Soul Anchor (auto-consumed on death to prevent XP loss), Fate's Grace (revive-in-place), **Merchant Pass** (30-day marketplace listing access, stacking from current expiry).
- **Costumes & Closet** — DB-driven cosmetic system. 5 rarity tiers (Common→Legendary), per-slot equipping, master show/hide toggle, paper-doll integration. 3 grant paths: mob drops (per-mob drop chance via `mob_costume_drops`), collection rewards, shop purchase. Full replication via 32-bit delta field mask. `SvCostumeDefs` / `SvCostumeSync` / `SvCostumeUpdate` packets.
- **💎 Opals Currency** — Premium non-P2W currency. End-to-end wired: direct-credit mob drops (Normals 1–5 @ 20–30%, MiniBosses 10–40 @ 90–100%, Bosses 200–700 @ 100% in dungeons), DB persistence, `SvPlayerState` replication, menu-driven Opals Shop with server-validated purchases against JSON catalog. QoL items priced as grind goals, not power.
- **Per-Item Stack Caps** — `Inventory::addItem` / `moveItem` / `moveBagItem` honor per-item `max_stack` and split oversized drops across multiple slots (200-pot drop with `max_stack=99` → 99+99+2 across 3 slots).

</details>

<details>
<summary><b>🌍 Economy, Social & Trade</b></summary>

- **Inventory & Bags** — 16 fixed slots + 10 equipment slots. Nested container bags (1–10 sub-slots). Auto-stacking consumables/materials. Drag-to-equip/stack/swap/destroy with full server validation. UUID v4 item instance IDs (stable per S116). Tooltip data synced (displayName, rarity, stats, enchant). **Configurable pickup priority** (Inventory-First / Bag-First) via Settings panel — wired through combat + pet auto-loot. **🆕 Bag-source NPC sell pipeline (S138)** — long-press a bag sub-slot to surface the Sell context entry; the new `CmdShopSellFromBag` (0x50) packet routes to a dedicated server handler that validates bag container + sub-slot + soulbound state and emits both inventory + bag-content sync post-credit so the bag mirror never goes stale.
- **Bank & Vault** — Persistent DB storage for items and gold. Flat 5,000g deposit fee. Full `ItemInstance` metadata preserved through deposit/withdraw. Gold withdraw cap check prevents silent loss.
- **🆕 Merchant Pass + Marketplace (S132)** — `item_merchant_pass` (5000 opals OR 30-day NPC SKU) gates the Marketplace `ListItem` flow with `merchantPassExpiresAtUnix > now()` check. Stacking semantics: `expires_at = max(now, current_expires) + 30 days` so consuming an active pass extends from existing expiry. Per-character `max_marketplace_listings` column (default 7) replaces the hardcoded `MarketConstants::MAX_LISTINGS_PER_PLAYER` — future "Listing Slot Extender" SKUs can `+= grant`. Character-select **PREMIUM pill** + market-panel **`PASS Nd` / `NO PASS` status pill** + premium-item tooltip descriptions.
- **Market & Trade** — Peer-to-peer 2-step security trading (Lock → Confirm → Execute). 8 item slots + gold. Slot locking prevents market/enchant during trade. Auto-cancel on zone transition. Market with 2% tax, status lifecycle (Active / Sold / Expired / Completed), seller-claim gold flow, jackpot pools, atomic buy via `RETURNING`. **Marketplace expiry auto-return for offline sellers** — `tickMaintenance` builds online-character set every 5 min, fiber dispatches `autoReturnExpiredOffline(onlineSet, limit=50)` which re-INSERTs items into the seller's first free main slot (preserving original `instance_id`).
- **Opals Shop** — In-game SHP menu tab (no NPC needed). JSON catalog (`assets/data/opals_shop.json`) with 12+ SKUs — bags (1,000–5,000 opals), Fate's Grace revive charges (5,000), 5 pet eggs (3,000–8,500), **Merchant Pass (5,000)**, **Recall Scroll (500)**. Server validates every purchase, returns `updatedOpals`.
- **Crafting** — 4-tier recipe book with ingredient validation and level/class gating.
- **Guilds & Parties** — Ranks, 16x16 pixel symbols, XP contributions, ownership transfer. 3-player parties with +10%/member XP bonuses and loot modes (FreeForAll / Random per-item).
- **Friends & Chat** — 50 friends, 100 blocks, online status with live `currentScene` enrichment. **`FriendsPanel`** with 3 tabs (Friends / Requests / Blocked) and inline Whisper / Party-invite / Remove / Accept / Decline / Cancel / Unblock actions. 7 chat channels (Map / Global / Trade / Party / Guild / Private / System), cross-faction garbling, server-side mutes (timed), profanity filtering (leetspeak normalization, 52-word list). **21 per-prefix system broadcast colors** (Loot / Boss / Event / Guild sub-types). Long-press → Whisper UX prefills `/w <name> ` in the chat input.
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
- **67 NPCs placed** across 13 NPC-bearing scenes — 40 faction NPCs (10 per village) + 7 Umbra fragment-named NPCs in The Scorched Hollow + 17 adventure-zone anchors + 3 Castle hub NPCs. After Session 119, **36+ of those NPCs offer quests** spanning legacy investigation chain anchors and the Phase 3C ambient mini-chains. Helpful but unsettling Umbra voices, hollow tones, unfinished sentences. Faction-aware click-targeting via `NPCComponent.targetFactions`.
- **🕳️ The Umbra Investigation** — A fragmented quest chain authored entirely as Umbra-tone narrative. No generic kill tasks; the story is the gameplay. Spans three mid-game adventure zones with a hidden coda for completionists. (Specifics intentionally not in the README — find them in-game.)
- **Faction Guard System** — 3-tier layered village defense: outer gate mage sentries (Lv30, 8-tile range), inner gate archer elites (Lv40, 6-tile range), village core warrior boss (Lv45, melee). All stationary, 30s respawn, paper-doll rendered with faction-specific skills. Guards also gate overworld adventure zones — enemy-faction guards block quest NPCs, portals, and scene transitions, creating organic faction territories.
- **Fate Guardian** — Fate's Domain rotating world boss (Lv50, 500K HP) with server-wide event broadcasts. 5-minute respawn delay after death, avoids same-scene consecutive spawns. Multi-spawn-point support (1-of-N random).
- **Innkeeper NPCs** — Bind your respawn point at any faction inn (100K gold). `recallScene` persisted to DB. Town Recall scrolls teleport to last bound inn.

</details>

<details>
<summary><b>🏰 World, AI & Dungeons</b></summary>

- **Mob AI** — Cardinal-only movement with L-shaped chase pathing, axis locking, wiggle unstuck, roam/idle phases, threat-based aggro tables, `shouldBlockDamage` callback (god mode). **Server-side DEAR** — mobs in empty scenes skipped entirely, distance-based tick scaling (full rate within 20 tiles, quadratic throttle beyond, 2s idle patrol beyond 48 tiles). Wall collision is intentional — enables classic lure-and-farm positioning tactics.
- **Spawns & Zones** — `SceneSpawnCoordinator` per-scene lifecycle (activate on first player, teardown on last leave), `SpawnZoneCache` from DB (circle/square shapes), respawn timers, death persistence via `ZoneMobStateRepository` (prevents boss respawn exploit). **Dungeon-template scenes are death-flag-gated** (`persistDeathState=false`) so per-party instances don't poison the global state. `createMobEntity()` static factory. Collision-validated spawn positions (30 retries, 48px mob separation). Per-entity stat overrides via `spawn_zones.instances_json` — HP/damage/attackRange/leashRadius/respawnSeconds/hpRegenPerSec per-instance.
- **🎰 Locked Loot Economy (S130–S133)** — 6-mig rebalance arc completed: trash drops/hour 12.27 → 1.26; legendary/epic elite rates corrected on endgame TheVoid + SkySanctuary mobs; Crystal Caverns trash backfilled with theme-appropriate drops; **enhance-stone systemic +10 level offset bug fixed** (every mob's stone drop now matches the canonical 10-level band); apex stone supply pyramid (worldboss 0.03 → boss 0.005 → trash 0.0001 trickle). Locked targets in `project_loot_rebalance_targets.md`.
- **Quest System** — 11 objective types (Kill / Collect / Deliver / TalkTo / **Interact** / PvP / Explore / KillInParty / CompleteArena / CompleteBattlefield / PvPKillByStatus) with prerequisite chains, branching NPC dialogue trees (enum-based actions + conditions), max 10 active, **167 quests** across 4 tiers (Starter / Novice / Apprentice / Adept) + the Umbra fragment chain + Phase 3C ambient mini-chains (Q730-Q822).
- **Instanced Dungeons** — Per-party ECS worlds, 10-minute timers, boss rewards, daily tickets (per-dungeon), invite system (30s timeout), celebration phase. Reconnect grace (180s). Event locks prevent double-enrollment. Per-minute chat timer. Per-dungeon HP×1.5 / damage×1.3 multipliers for 3-player party tuning.
- **Aurora Gauntlet** — 6-zone PvP with hourly faction-rotation buff (+25% ATK/EXP), wall-clock `hour%4` rotation. Aether Stone + 50K gold entry. Aether world boss (Lv55, 150M HP, 36hr respawn) with 23-item loot table. Zone scaling Lv10→55. Death ejects to Town. Live `GauntletHUD` (TopRight) + `GauntletResultModal` top-10 leaderboard.
- **🐾 Active Pet System** — Equip one pet at a time; active pet shares 50% of player XP from mob kills, contributes to `equipBonus*` (HP + crit rate + XP bonus) via `PetSystem::applyToEquipBonuses` (recalc on equip/unequip/level). 5 pets shipped across 4 rarity tiers: 🐢 Turtle + 🐺 Wolf (Common), 🦅 Hawk (Uncommon), 🐆 Panther (Rare), 🦊 Fox (Legendary). Premium tier knobs (interval/itemsPerTick/radius) on `pet_definitions` — Legendary Fox auto-loots every 0.2s with 128px radius. Client-side `PetFollowSystem` keeps the active pet trailing the player with 4-state machine (Following / Idle / MovingToLoot / PickingUp). Server-authoritative auto-looting (per-rarity tick, 64–128px radius, ownership + party aware). DB-level unique partial index enforces one active pet per character. Consumable pet eggs purchasable from the Opals Shop (Legendary Fox 30,000 opals).
- **Loot Pipeline** — Server rolls → ground entities → spatial replication → pickup validation → 90s despawn. Per-player damage attribution, live party lookup at death, strict purge on DC/leave. Epic/Legendary/Mythic server-wide broadcast; party loot broadcast to all members.
- **NPC System** — 10 NPC types: Shop, Bank, Teleporter (with item/gold/level costs), Guild, Dungeon, Arena, Battlefield, Story (branching dialogue), QuestGiver, Innkeeper (respawn binding). Proximity validation on all interactions. `EntityHandle`-based caching for zone-transition safety.
- **Event Return Points** — Centralized system prevents players from being stranded after disconnecting from instanced content. Return point set on event entry, cleared on normal exit, re-set on grace rejoin.
- **Trade Cleanup** — Active trades cancelled on disconnect, partner inventory trade-locks released via `unlockAllTradeSlots()`, preventing permanently locked slots.
- **👁️ Admin Observer / Spectate Mode** — Admin-role accounts can `/spectate <scene>` into any live scene *without a character entity* — replication still fires, ghost entities interpolate, MobAI continues ticking via `sceneObserverCounts_` presence refcount (so dead scenes don't freeze when only an observer is watching). `SvSpectateAck` (0xD9) returns typed status (accepted / not-admin / unknown-scene / stopped). Sentinel log `observer_only_ticked=0` detects regressions. Perfect for live-ops debugging, boss-fight reviews, and anti-cheat spot checks.

</details>

---

## 🎨 Retained-Mode UI System

Custom data-driven UI engine with **viewport-proportional scaling** (`screenHeight / 900.0f`) for pixel-perfect consistency across all devices. Anchor-based layout (12 presets + percentage sizing), JSON screen definitions, 9-slice rendering, two-tier color theming, virtual `hitTest` overrides for mobile-optimized touch targets. 21 per-prefix system message color/font configurations.

- **65+ Widget Types:** 25 Engine-Generic (Panels, ScrollViews, ProgressBars, Checkboxes, ConfirmDialogs with serializable bgColor/borderColor/messageOffset, NotificationToasts, LoginScreen, ImageBox, TextInput with masked password mode, **PanelChrome / TabRail / DebugChromePanel / Divider** chrome primitives) and **40+ Game-Specific** (**🆕 chrome'd DPad / SkillArc / MenuTabBar / SettingsPanel** via S136's bespoke-widget pass, **SkillArc** 4-page polymorphic C-arc with S139 cursor-tracking drop-highlight, **SkillLoadoutStrip** for in-panel drag/drop loadout management with circle slots + page-dot hit testing, FateStatusBar, InventoryPanel with paper doll + bag context-menu Sell entry, CostumePanel, CollectionPanel, ArenaPanel, BattlefieldPanel, **PetPanel**, **OpalsShopPanel**, **BountyPanel**, **FriendsPanel**, **GauntletHUD**, **GauntletResultModal**, CraftingPanel, ShopPanel with bag-source confirm popup, MarketPanel with buy confirmation + status lifecycle + **PASS pill**, BagViewPanel with `bagPanelOffset` knob, EmoticonPanel, QuantitySelector, PlayerContextMenu, ChatIdleOverlay, BossHPBar, **NpcDialoguePanel** with state-aware quest-tree picker, DeathOverlay with Fate's Grace button, **enchant_confirm_dialog**, **enchant_protect_confirm_dialog**, and more).
- **10 JSON Screens & 42 Theme Styles:** Parchment, HUD dark, dialog, tab, scrollbar, **14 chrome styles** (panel_chrome.default / title_pill / close_x + 8 panel variants + tooltip; tab_rail.default / active / inactive). **Chrome default-on as of S127** — 25 per-panel + 3 manager `xxxUseChrome_` toggles flipped to `true`; **S136 extended this to 4 bespoke-shape widgets via 49 new styled fields** (no new manager toggles, no new View-menu items — propagation walk in `engine/ui/ui_manager.cpp:350-362` extends to `MenuTabBar` / `SettingsPanel` / `DPad` / `SkillArc`). Legacy `drawMetallicCircle` paths preserved verbatim under `else` for instant rollback. Full serialization of layout properties, fonts, colors, and inline style overrides. Ctrl+S dual-save (build + source dir) via atomic write. Hot-reload with 0.5s polling + suppress-after-save guard.
- **Paper Doll System:** `PaperDollCatalog` singleton with JSON-driven catalog (`assets/paper_doll.json`) — body/hairstyle/equipment sprites per gender with style name strings, direction-aware rendering with per-layer depth offsets and frame clamping, texture caching, editor preview panel with live composite + Browse-to-assign. Used in game HUD, character select, and character creation.
- **Zero-ImGui Game Client:** All HUD, nameplates, and floating text render via SDFText + SpriteBatch. ImGui is compiled out of shipping builds entirely.
- **Mobile-First, No Right-Click Design:** Every interaction works on touch — long-press, drag-and-drop, tap, confirm popups. The polymorphic skill-arc loadout, the enchant flow, the protect-craft flow, and the new bag-source NPC sell are all touch-friendly — drag-only or two-tap context menus, no right-clicks required.
- **120+ UI tests.**

---

## 🔒 Server & Networking

**Headless 20 Hz server** (`FateServer`) with max **2,000 concurrent connections**. **47 handler files**, **17 DB repository files**, **12 startup caches**, **15-min idle timeout**, graceful shutdown with player save flush. Every game action is server-validated — zero trust client. **PROTOCOL_VERSION 17** — v9 reliability rebuild + v10 interact-site framework + v11 site-string-id swap + v12 `CharacterFlags` client mirror + v13 `SvConsumableCooldown` + v14 explicit-stone enchant + v15 `CmdCraftProtectStone` + v16 polymorphic skill-bar bindings (`CmdAssignSlot` rename + `kind`/`instanceId` extension) + v17 `CmdShopSellFromBag` (0x50) for bag-source NPC sells.

<details>
<summary><b>🔐 Transport & Encryption</b></summary>

| Property | Value |
|----------|-------|
| Protocol | Custom reliable UDP (`0xFA7E`, **v17**), Win32 + POSIX |
| Encryption | **Noise_NK handshake** — two X25519 DH ops (`es` + `ee`, BLAKE2b-512 derivation, protocol-name domain separator) + **XChaCha20-Poly1305 AEAD** (key-derived 48-bit session nonce prefix OR'd with 16-bit packet sequence, 16-byte tag, separate tx/rx keys). Symmetric rekey every 65K packets / 15 min, **epoch-gated** (4-byte LE epoch payload, gated by `tryAdvanceRekeyEpoch` so retransmits dedupe instead of desyncing keys). Anonymous-DH fallback removed — every session is authenticated against the server's static identity key |
| Server Identity | Long-term X25519 static key (`config/server_identity.key`); public key distributed with client for MITM prevention. Key file is `chmod 0600` on POSIX, locked-DACL on Windows |
| Auth Hardening | **64-bit session tokens** generated via libsodium CSPRNG (`PacketCrypto::randomBytes`) — zero `std::mt19937`. **`CmdAuthProof` (0xD8)**: auth token is encrypted under the Noise session key *after* handshake — never traverses the wire in plaintext. Timing-safe comparisons via `sodium_memcmp`. Per-account login rate-limit with exponential backoff, auth-token TTL. **`AuthPhase` state machine** (HandshakePending → ProofReceived → Authenticated) gates non-system packet handlers — game commands are dropped until proof verifies |
| DB-Backed Sessions | **`auth_sessions` table** (mig 089) with PK `token`, partial unique index on `(account_id) WHERE activated_at IS NOT NULL`. `consumeAndActivate` is atomic: SELECT-FOR-UPDATE, DELETE-RETURNING any prior active session, NOTIFY `session_kicked` payload `{node, acct, cid}`. **`SessionListener`** runs `LISTEN session_kicked` on a dedicated `pqxx::connection` (NOT pooled — LISTEN state lives on a single backend), filters by `node == myServerNode_`, drops kick events into a thread-safe queue drained by `ServerApp::tick`. **Multi-process ready** — login on node B kicks the session on node A within ~1s |
| Fail-Closed Encrypt | Both `NetClient::sendPacket` and `NetServer::sendPacket` drop + LOG_ERROR on encrypt failure rather than fall back to plaintext. `payloadSize` bounds-checked before `sendTo` AND before `trackReliable` |
| IPv6 | Dual-stack with IPv4 fallback (DNS64/NAT64 — iOS App Store mandatory). Auth TCP uses `getaddrinfo(AF_UNSPEC, ...)` so DNS names work and SNI/X509 hostname verification is wired |
| Channels | Unreliable (movement, combat events), ReliableOrdered (critical), ReliableUnordered |
| Packets | 26-byte header (v9+: ackBits widened 32→64-bit), RTT estimation (EWMA 0.875/0.125), retransmission delay `max(0.2s, 2*RTT)`, zero-copy retransmit. Pending-packet queue 2048 slots, 75% congestion threshold. **`SvEntityEnterBatch`** coalesce, **`CmdAckExtended`** out-of-window recovery, epoch-gated `Rekey`. **v13–v17 packets:** `SvConsumableCooldown` (0xE9), `CmdCraftProtectStone` (0xEA), `SvCraftProtectStoneResult` (0xEB), polymorphic `CmdAssignSlot` (0x36, was `CmdAssignSkillSlot`), `CmdShopSellFromBag` (0x50) |
| Socket Buffers | 1 MB kernel send/recv (`SO_SNDBUF`/`SO_RCVBUF`) — prevents silent drops during burst replication |
| Payload | 1200 B UDP standard (`MAX_PAYLOAD_SIZE=1174` after v9's 26-byte header); large reliables up to 16 KB (handler buffers bumped 4K→16K across 9 sites) |
| Critical-Lane Bypass | **9 load-bearing opcodes** (`SvEntityEnter`, `SvEntityLeave`, `SvPlayerState`, `SvZoneTransition`, `SvDeathNotify`, `SvRespawn`, `SvKick`, `SvScenePopulated`, `SvEntityEnterBatch`) bypass reliable-queue congestion check — eliminates "invisible mob" + "death overlay never fires" symptoms under load |
| Rate Limiting | Per-client, per-packet-type token buckets (**55+ packet types** across 14 categories), violation decay, auto-disconnect at 100 violations |
| Anti-Replay | Economic nonce system (trade/market, single-use uint64, 60s expiry, cleaned on disconnect), connection cookies (FNV-1a time-bucketed anti-spoof — not a cryptographic MAC), atomic dungeon-ticket claim |
| Auth Security | TLS 1.2+ with AEAD-only ciphers, shipping enforces `SSL_VERIFY_PEER` (no self-signed), login rate limiting (5 attempts → 5-min IP lockout, 15 attempts → 15-min username lockout), bcrypt timing-oracle defense (dummy hash run on unknown usernames), version gate |
| Auto-Reconnect | `ReconnectPhase` state machine, exponential backoff (1s→30s cap), 60s total timeout |
| Idle Timeout | 15-min inactivity auto-disconnect, per-client activity tracking, system chat warning before kick |
| Event Return Points | Centralized scene/position restore on DC from instanced content (dungeon/arena/battlefield) |
| Mass-DC Hardening (S128) | **Per-tick disconnect budget** (`MAX_DISCONNECTS_PER_TICK=16`), async session removal + trade cancel via `dbDispatcher_`, **DbDispatcher backlog** (FIFO-preserving, 8192 hard cap, threshold instrumentation, drains in events phase), **bumped JobSystem to 4 workers default** (`FATE_JOB_WORKERS` env override) |

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
- **🆕 Premium State Replication (S132)** — `CharacterPreview` (login char-select) + `SvPlayerStateMsg` (in-game live state) extended with `merchantPassExpiresAtUnix` + `maxMarketplaceListings`. Append-only — no protocol bump needed (lockstep deploy).

</details>

<details>
<summary><b>💾 Persistence & Database</b></summary>

| Layer | Detail |
|-------|--------|
| **Circuit Breaker** | 3-state (Closed → Open → HalfOpen), 5 failures → 30s cooldown, single-probe pattern |
| **Priority Flushing** | 4 tiers: IMMEDIATE (0s — gold/inventory/trades), HIGH (5s — level-ups/PK/zone transitions), NORMAL (60s — position), LOW (300s — pet/bank). 1s dedup, 10/tick drain |
| **Auto-Save** | 30s staggered per-player with `forceSaveAll=true`. Maximum 30s data loss window on crash. Event-triggered HIGH priority saves on zone transition and level up |
| **Fiber-Based `DbDispatcher`** | Header-only, runs on JobSystem worker threads (4 default, env-configurable). Covers async player load (18 queries as single job), disconnect saves, market browse/list, persistence saves, maintenance, expired-death cleanup, and all 5 MetricsCollector DB ops. **Bounded backlog (8192 hard cap, FIFO-preserving, threshold-instrumented).** **Zero game-thread blocking.** |
| **Async Saves** | Disconnect saves snapshot all data instantly on game thread, dispatch single-transaction write to worker fiber. Epoch bumps invalidate stale in-flight periodic saves. `PlayerLockMap` preserves mutexes across worker fibers (fast-reconnect stale-data protection) |
| **Inventory Save Pattern (S116)** | Four-step orphan-DELETE → NULL-positions → UPSERT-by-`instance_id` → trailing-DELETE pattern. The NULL-positions step dodges `uq_character_inventory_slot` partial unique index on swap-induced position collisions. `instance_id` is stable across saves (was previously `DEFAULT gen_random_uuid()` which regenerated UUIDs every save and drifted memory ↔ DB) |
| **Dirty Flags** | `PlayerDirtyFlags` at **95+ mutation sites**. Async error re-dirties for retry. Batched mob death persistence (single DB transaction per scene per tick regardless of kill count — 27 round-trips → 1) |
| **Connection Pool** | Thread-safe (min 5, max 50, +10 overflow). **Read-replica fallthrough** via `FATE_READ_DATABASE_URL` (sized 2/20). Per-tick DB call diagnostics via `DbPool::Guard` RAII (elapsed + call count in slow-tick logs) |
| **Async Player Load** | Fiber-based non-blocking player data load on connect — 18 queries packed into single `PlayerLoadResult` job. `playerEntityId == 0` gates packet handlers during load. Zero tick stalls during login storms |
| **Persistence Contract Test** | `tests/test_persistence_contract.cpp` round-trips every mutable column in characters / inventory / bank / pets / costumes / quests / skills / **polymorphic skill_bar (Skill + Item bindings)** against a live DB (gated on `FATE_DB_HOST` env). Catches save-method drift across every subsystem repository |

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
- **Live Inspector** — Edit all 56 component types live with **full undo/redo**. Sprite preview thumbnails. Reflection-driven generic fallback via `FATE_REFLECT`. SemiBold headings, separator lines. **🆕 Skill Loadout Strip + Strip Chrome inspector blocks (S135)** — 28 Inspector entries covering visibility, layout, slot/page selector, useChrome toggle, all 14 chrome color/thickness/font fields. **🆕 Bespoke Widget Chrome inspectors (S136)** — 49 new fields across 4 widgets (`MenuTabBar` 11, `SettingsPanel` 16, `DPad` 9, `SkillArc` 13), each routed through `checkUndoCapture(uiMgr)` for undo parity. Every entry undo-captured.
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
- **Animation Editor** — Aseprite-first import pipeline with auto-sibling discovery, layered paper-doll preview (5-layer composite), variable frame duration, onion skinning, content pipeline conventions. Sprite Sheet Slicer (color-coded direction lanes, hit frame "H" badges, mousewheel zoom, frame info tooltips). 3-direction authoring → 4-direction runtime.
- **Tile Palette** — Recursive subdirectory scan, scrollable grid, brush size (1-5), 4-layer dropdown (Ground / Detail / Fringe / Collision), layer visibility toggles.
- **Dialogue Node Editor** — Visual node-based dialogue trees via imnodes. Speaker/text nodes, choice pins, JSON save/load (atomic), node position persistence.
- **UI Editor** — Full WYSIWYG for all 65+ widget types: colored type-badge hierarchy, property inspector for every widget, selection outline, viewport drag, undo/redo with full screen JSON snapshots. Ctrl+S dual-save + hot-reload safe pointer revalidation.
- **Network Panel** — Editor dock surfacing client-side metrics: protocol banner (`v17`), encryption status, RTT (color-coded), reliable queue depth (color-coded against 2048 cap), dropped-non-critical count, AOI entity count, host:port, Connect/Disconnect button.
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

Concrete performance, security, and reliability gains shipped in the latest phases (Sessions 115–139).

| Win | Impact |
|---|---|
| 🆕 **Skill Loadout Strip — Drag-Bind Repair + Decoupling (S139)** | Seven sub-fixes against the S135 strip. Two distinct root causes in the drop pipeline: `UIManager::handleRelease` was calling `acceptsDrop` *before* setting `dropLocalPos` (so `InventoryPanel::acceptsDrop` saw stale `{0,0}` and rejected the drop), and `InventoryPanel::onRelease` was clearing the shared `DragPayload` before UIManager could route to `onDrop`. Plus polish: page-dot hit testing now includes draws outside `bounds_`, slots render as true circles via `slotIsCircle` (default on), drop-highlight ring follows the cursor across slots via `onDragUpdate`, and the inventory grid no longer auto-shrinks with strip height/padding. **Client-only — no protocol bump, no server restart.** 5 new strip tests + 1 pre-existing test repaired. **1687/1688 green** |
| 🆕 **Bag-Item NPC Sell Pipeline (S138, protocol 16 → 17)** | Closes the long-standing "only sells one bag item then sell vanishes" gap. New first-class wire packet `CmdShopSellFromBag = 0x50` (`{npcId, bagSlot, bagSubSlot, quantity}`) — chosen over an `isBagItem` discriminator on `CmdShopSellMsg` because it matches the existing `CmdEquip` / `CmdEquipFromBag` precedent and keeps the legacy top-level path byte-identical. Server `processShopSellFromBag` mirrors `processShopSell`'s NPC-proximity / soulbound / ownership / sell-price / loadout-sweep / persist flow but validates bag container + sub-slot and emits **both** `SvInventorySync` AND `SvBagContentsForSlot` post-credit so the bag mirror never goes stale. Rate-limit shares the `CmdShopSell` budget |
| 🆕 **Chrome Regression Sweep (S138)** | Six discrete close-X / chrome interaction bugs swept in one session: CollectionPanel's close-X recursed into stack-overflow (client locked up, server timed out the silent connection); SettingsPanel + GuildPanel (menu-tab copy) + CostumePanel close-Xs were silently no-ops with `onClose` never wired; ShopPanel chrome dropped sell-confirm clicks because `onPressChrome` had `if (showSellConfirm_) return true;` swallowing the popup; the inventory close-X didn't tear down the surrounding shop UX. Plus SkillArc Action no longer bleeds the legacy sword icon through chrome's flat fill, and AA-edge ring on Attack/PickUp no longer reads as a black border when chrome border matches the fill |
| 🆕 **Bespoke Widget Chrome — Checkpoint 8 (S136)** | `MenuTabBar` / `SettingsPanel` / `DPad` / `SkillArc` joined the chrome system through 49 new styled fields (11+16+9+13) routed via existing `panelUseChrome_` / `hudUseChrome_` propagation walks at `engine/ui/ui_manager.cpp:350-362`. Chrome path replaces `drawCircle` / `drawRing` / `drawMetallicCircle` with `RoundedRectParams` equivalents; legacy `drawMetallicCircle` paths preserved verbatim under `else` (3 occurrences in SkillArc) for instant rollback. Defaults mirror legacy colors byte-for-byte. **No new manager toggles, no new View-menu items** — extends existing infrastructure. Subagent-driven development (implementer + spec compliance + code-quality reviewers) per task. Client-only |
| 🆕 **Inventory Bag Panel — Geometry Fix + `bagPanelOffset` Knob (S137)** | Three nested issues unwound. (A) Brown panel was *centered* in the grid area while slots inside were *top-anchored*, so layouts drifted with bag size — fix anchors panel at top of grid area, slots/title/hit-test all derive from `bagRect.x/y + panelPad + …` regardless of size. (B) Local-vs-absolute coordinate space mismatch between `getBagPanelRect()` (local) and `renderBagView` (absolute) created a "can't click top slots" regression — fix translates local to absolute consistently in render, hit-test stays local. (C) New `Vec2 bagPanelOffset` inspector knob for fine-tuning. Client-only |
| 🆕 **Polymorphic 20-slot skill loadout (S135)** | Drag potions/recall scrolls/fated potions/food onto any of the 20 loadout slots from inventory or open bag. New `SkillLoadout` model owns canonical state; `SkillArc` HUD refactored to thin renderer; new `SkillLoadoutStrip` widget embedded in `InventoryPanel`. **14 stale-binding sweep sites** (trade/sell/market/destroy/bank/consume/craft/bag/revive/teleport/inventory-merge/soul-anchor) keep bindings coherent. Migs 112 (`item_definitions.cooldown_ms`) + 113 (`character_skill_bar` polymorphic columns). 56 new tests across `test_skill_loadout` + `test_skill_loadout_strip` + `test_skill_bar_polymorphic` |
| 🆕 **7-tier enhance-stone economy + protected stones as physical items (S134)** | Stone tier ladder consolidated 8→7 with `mat_enhance_stone_ancient` orphaned (kept in `item_definitions` with deprecation marker), epic re-banded as Lv 51-60, legendary re-banded as Lv 61-70. **Drag-driven enchant flow** carries explicit `stoneSlot` on the wire (no first-match-by-id fallback). **Protected stones become real `_protected` items** that live in inventory and persist across logout — atomic post-consume-aware space check + rollback path. Two confirm-dialog popups gate every attempt. Risky-tier failure now destroys equipment (no more `isBroken` flag). Mig 114, protocol 13→14→15 |
| 🆕 **Locked loot economy (S130–S133, migs 103–111)** | Avg trash drops/hour collapsed from **12.27 → 1.26** (Pass A). Endgame elite legendary/epic rates corrected on TheVoid + SkySanctuary mobs. Crystal Caverns trash backfilled (was starved). **Enhance-stone systemic +10 level offset bug fixed** — every mob now drops the canonical 10-level band tier. Apex stone supply pyramid (worldboss 0.03 → boss 0.005 → trash 0.0001 trickle). Per-tier trash rate ladder: basic 0.002 / refined 0.0015 / superior 0.001 / greater 0.001 / master 0.0007 / ancient 0.0005. Locked targets in `project_loot_rebalance_targets.md` |
| 🆕 **Merchant Pass + Recall Scroll wired end-to-end (S132)** | `item_merchant_pass` (5000 opals, 30 days, **stacking from current expiry**) gates the Marketplace `ListItem` flow. Per-character `max_marketplace_listings` column (default 7) replaces the hardcoded constant — future "Listing Slot Extender" SKUs can `+= grant`. Mig 105. Character-select PREMIUM pill, market-panel `PASS Nd` / `NO PASS` status pill, premium-item tooltips. `recall_scroll` purchasable for 500 opals OR 500 gold |
| 🆕 **Chrome system default-on (S125–S127)** | 25 per-panel + 3 manager `xxxUseChrome_` toggles flipped to `true`. View-menu items kept as legacy rollback. 13 close-X chrome'd (28 fields). FateStatusBar Menu/Chat/overlay (10 fields). Inter SemiBold across the board, parchment HUD dark dialog scrollbar tab tooltip all reconciled. **1610/1610 tests** at S127 |
| 🆕 **Mass-disconnect game-thread protection (S128)** | Pre-S128 a 100-client timeout cluster blocked the game thread for 20–40 seconds (sync session DELETE × 300 ms RTT × 100 clients). Fix: `scheduleSessionRemoval` + `scheduleTradeCancel` route through `dbDispatcher_`; per-tick disconnect budget (`MAX_DISCONNECTS_PER_TICK=16`) spreads work across ticks; **DbDispatcher backlog** (8192 hard cap, FIFO-preserving, threshold-instrumented, drains in events phase); JobSystem bumped 2→4 workers default with `tryPushFireAndForget` non-blocking variant |
| **Phase 3C ambient NPC mini-chains shipped (S119)** | 30 new quests across 10 chains, applying the state-machine dialogue pattern at scale. Cameo-precedence preserved on overlapping NPC roles via `autoAdvance` ordering. **1599/1599 tests / 10,585 assertions green.** Zero migrations, zero protocol bump — entirely additive content using existing assets |
| **`item_definitions` type-taxonomy hygiene pass (S120)** | Closed 4 long-standing data-shape gaps: lowercase `type='material'` (1 row), empty-subtype Materials (17 rows reclassified), `Material/Material` tautology (1 row), `type='Equipment'` orphan ladder (24 rows deleted in S118). DB landed at 816 items across 6 canonical types and 22 Material subtypes. Test seeder patched to `ON CONFLICT DO UPDATE` so future fixture writes self-heal |
| **Server-authoritative PvP zone gate restored (S115)** | `combat_handler.cpp` was reading `CharacterStats::isInPvPZone` — a per-character flag that defaulted false and was never written anywhere on the server. PvP was silently disabled game-wide. Wired `inSafeZone` to `sceneCache_.isPvPEnabled(currentScene)`, deleted 3 dead-code paths, restored a broken `MetricsCollector` counter, added 8 new contract tests |
| **Atomic-save swap-conflict fix (S116)** | `Inventory::moveItem` is implemented as `std::swap` — both items keep their `instance_id`s, only `slot_index` exchanges. Re-save tripped `uq_character_inventory_slot` partial unique index because Postgres validates per-statement. Fix: NULL the position columns on preserved rows between the orphan-DELETE and UPSERT loop. Two new regression tests cover top-slot + bag-slot swaps |
| **Demo v2 — Play / Observe / Scene-dropdown / File menu / Ctrl+S** | Open-source `FateDemo` reaches feature parity with the proprietary editor's runtime control loop. Configurable `AppConfig::onObserveStart` hook lets downstream apps swap in network spectate or other observer flows |
| **PROTOCOL v9 → v17 in 8 sessions** | v9: 64-bit ACK window, `SvEntityEnterBatch` coalesce, `CmdAckExtended`, epoch-gated rekey. v10: interact-site packets. v11: site-string-id swap. v12: `CharacterFlags` client mirror. v13: `SvConsumableCooldown`. v14: explicit-stone enchant on the wire. v15: `CmdCraftProtectStone` + `SvCraftProtectStoneResult`. v16: polymorphic `CmdAssignSlot` (skill OR item bindings) + `LoadoutSlotWire` element type + `CmdUseConsumable.source` byte. v17: `CmdShopSellFromBag` for bag-source NPC sells |
| **DB-backed `auth_sessions` + cross-process LISTEN/NOTIFY kick** | Sessions survive process crashes. Multi-process deployment ready: login on node B kicks the existing session on node A within ~1s via Postgres NOTIFY |
| **Fail-closed encrypt symmetry on both client + server send paths** | Drops + LOG_ERROR on encrypt failure rather than silently falling back to plaintext (which the peer would reject and force infinite retransmits) |
| **`AuthPhase` state machine** | `HandshakePending` → `ProofReceived` → `Authenticated`. Non-system handlers reject packets until proof verifies — closes the theoretical window where malformed proof bytes could reach game logic |
| **Atomic write helper for every editor JSON save** | Scene saves, UI screens, dialogue nodes, animation templates / framesets / packed meta / `.meta.json` siblings — all route through `writeFileAtomic` (`.tmp` + rename + cross-volume copy fallback). Failed Save-As no longer corrupts destination |
| **`IAssetSource` + PhysicsFS VFS** | Unified read path for textures, JSON, shaders, scenes, audio, dialogue, server scene scans, shutdown config. Two implementations (DirectFs + Vfs), behind `option(FATE_USE_VFS …)` |
| **Shipping-build CI guard** — `scripts/check_shipping.ps1` wraps `VsDevCmd.bat` + `cmake --preset x64-Shipping`; every `imgui.h`/`ImGui::*`/editor member requires explicit `FATE_SHIPPING`/`EDITOR_BUILD` guard | Stripped ImGui-free shipping exe verified at 5.7 MB; regressions fail fast in CI |
| **Persistence contract test** — every mutable column round-trips against a live DB | Catches save-method drift across every subsystem repository (gated on `FATE_DB_HOST` env so CI without DB still passes). `TestAccountGuard` RAII wrapper added in S120 ensures `ctest_*` rows clean up on `REQUIRE`-throw unwind |
| **`MAX_STACK_SIZE = 9999` global removed** | `Inventory::addItem` / `moveItem` / `moveBagItem` honor per-item `max_stack` and split oversized drops into multiple capped chunks. 25 server call sites + 8 test files updated |
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
- **Cooldown rendering is binary** post-S135 (full ring while cooling, off when 0) for both Skill and Item paths. Pre-S135 had proportional radial fill via per-frame `cooldownTotal` push that was removed when `SkillArc` was refactored to a thin renderer. Threading `cooldownTotalsMs_` through `SkillLoadout` restores it — small refactor flagged as follow-up.
- **One pre-existing test failure** — `tests/test_enchant_system.cpp:118-119` expects `mat_enhance_stone_ancient` for Lv 51-60 but mig 114 (S134) retired ancient → `getRequiredStone` returns epic. One-line test fix; independent of any active arc.

---

## 🌱 From Engine Demo to Full Game

The open-source repo builds and runs as an editor/engine demo. To develop a full game on top of this engine, you would create the following directories (which the CMake system auto-detects):

**Game Logic (`game/`):**
- `game/components/` — Game-specific ECS components (transform, sprite, animator, colliders, combat stats, inventory, equipment, pets, factions, **interact sites**, **CharacterFlags**, etc.)
- `game/systems/` — Game systems that operate on components (combat, AI/mob behavior, skill execution, spawning, loot, party, nameplates, **interact-site triggers**, etc.)
- `game/shared/` — Data structures shared between client and server (item definitions, faction data, skill tables, mob stats, **dialogue trees**, **quest definitions**, **enchant ladder**, **skill loadout**)
- `game/data/` — Static game data catalogs (paper doll definitions, skill trees, enchant tables, opals shop catalog)
- An entry point (`game/main.cpp` or similar) with a class inheriting from `fate::App`

**Server (`server/`):**
- Request handlers for every game action (auth, movement, combat, trade, inventory, chat, party, guild, arena, dungeons, **interact sites**, **bounty board**, **enchant + protected-stone craft**, **loadout sweep**)
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
| **fate_tests** | 1,687 unit tests (doctest, 10,973 assertions) | When `tests/` sources present |

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

The engine maintains exceptional stability through **1,687 passing test cases** across **214 test files**, powered by `doctest`. **10,973 assertions** with one pre-existing tier-bracket fixture failure (independent of any active arc).

```bash
# Run all tests:
./build/Debug/fate_tests.exe

# Target specific suites:
./build/Debug/fate_tests.exe -tc="Death Lifecycle"
./build/Debug/fate_tests.exe -tc="PacketCrypto"
./build/Debug/fate_tests.exe -tc="PersistenceQueue*"
./build/Debug/fate_tests.exe -tc="SkillVFX*"
./build/Debug/fate_tests.exe -tc="SkillLoadout*"
./build/Debug/fate_tests.exe -tc="SkillBarPolymorphic*"
./build/Debug/fate_tests.exe -tc="EnchantSystem*"
./build/Debug/fate_tests.exe -tc="CollisionGrid*"
./build/Debug/fate_tests.exe -tc="AsepriteImporter*"
./build/Debug/fate_tests.exe -tc="PetSystem*"
./build/Debug/fate_tests.exe -tc="PetAutoLoot*"
./build/Debug/fate_tests.exe -tc="InteractSite*"
./build/Debug/fate_tests.exe -tc="SessionRepository*"
./build/Debug/fate_tests.exe -tc="DialogueTree*"
./build/Debug/fate_tests.exe -tc="AtomicWrite*"
```

Coverage spans: combat formulas, encryption/decryption, entity replication, inventory operations, skill systems, **polymorphic skill-bar persistence**, **stale-binding sweep semantics**, **enchant ladder + protected-stone craft**, quest progression (incl. honor formula + milestone collections), economic nonces, arena matchmaking, dungeon lifecycle, VFX pipeline, compressed textures, UI layout, collision grids, async asset loading, Aseprite import pipeline, animation frame durations, costume system, collection system, pet system, pet auto-loot, **interact-site validator + packets + dialogue conditions + Q155/Q700 retrofits**, **session repository + listener cross-process kick**, **atomic-write durability**, **persistence contract** (live-DB column round-trip), and more.

---

## 📐 Architecture at a Glance

```
engine/                    # 92,358 LOC — Core engine (20 subsystems, 379 files)
 render/                   #   Sprite batching, SDF text, lighting, bloom, paper doll, VFX, Metal RHI
 net/                      #   Custom UDP (v17), Noise_NK crypto, AuthProof, replication, AOI, interpolation
 ecs/                      #   Archetype ECS (4096 reserve), 56 components, reflection, serialization
 ui/                       #   65+ widgets (incl. SkillLoadoutStrip w/ S139 drag-bind repair, ConfirmDialog chrome, FriendsPanel, BountyPanel), JSON screens, themes, viewport scaling, panel + bespoke widget chrome
 editor/                   #   ImGui editor, undo/redo, Aseprite animation editor, asset browser, Play/Observe/Scene toolbar, 49-field bespoke widget chrome inspectors (MenuTabBar/SettingsPanel/DPad/SkillArc)
 tilemap/                  #   Chunk VBOs, texture arrays, Blob-47 autotile, 4-layer
 scene/                    #   Async loading, versioning, prefab variants
 asset/                    #   IAssetSource (DirectFs / Vfs), hot-reload, fiber async, LRU VRAM cache, compressed textures
 input/                    #   Action map, touch controls, 6-frame combat buffer
 audio/                    #   SoLoud, 3-bus, spatial audio, 10 game events
 job/                      #   Fiber system, MPMC queue, scratch arenas, FATE_JOB_WORKERS env override
 memory/                   #   Zone/frame/scratch arenas, pool allocators
 spatial/                  #   Fixed grid, spatial hash, collision bitgrid
 core/                     #   Structured errors, Result<T>, CircuitBreaker, atomic_write
 particle/                 #   CPU emitters, per-particle lifetime/color lerp
 platform/                 #   Device info, RAM tiers, thermal polling
 profiling/                #   Tracy integration, spdlog, rotating file sink
 vfx/                      #   SkillVFX player, JSON definitions, 4-phase compositing
 vfs/                      #   PhysicsFS, ZIP mount, overlay priority
 telemetry/                #   Metric collection, JSON flush, HTTPS stub
 game/                     #   SkillLoadout model (canonical UI-facing facade for the polymorphic 20-slot bar)

game/                      # 41,385 LOC — Game logic layer (119 files)
 shared/                   #   19,319 LOC of pure gameplay across 75 files (zero engine deps)
   combat_system           #   Hit rate, armor, crits, class advantage, PvP balance, scene-cache PvP gate
   skill_manager           #   60+ skills, polymorphic loadout, cooldowns, cast times, resource types
   mob_ai                  #   Cardinal movement, threat, leash, L-shaped chase
   status_effects          #   DoTs, buffs, shields, source-tagged removal
   inventory               #   16 slots, equipment, nested bags, stacking, UUID instances (stable per S116), per-item max_stack
   trade_manager           #   2-step security, slot locking, atomic transfer
   arena_manager           #   1v1/2v2/3v3, matchmaking, AFK detection, honor
   gauntlet                #   Event scheduler, divisions, wave spawning, MVP
   faction_system          #   5 factions, guards, innkeeper, faction-aware targeting
   pet_system              #   Active pet equip, XP share, stat bonuses, auto-loot
   opals_system            #   Currency, shop catalog, direct-credit drops, AdMob rewards
   enchant_system          #   7-tier ladder, protected stones as physical items, drag-driven flow
   quest_manager           #   167 quests, dialogue trees, interact sites, honor + opals + ambient grind
   dialogue_registry       #   npcs/ + quests/ + sites/ scan, condition evaluator
   ...                     #   +25 more systems (guild, party, crafting, collections, market, merchant pass, etc.)
 components/               #   ECS component wrappers + InteractSiteComponent + CharacterFlagsComponent + QuestGiverComponent
 systems/                  #   12 ECS systems (combat, render, movement, mob AI, pet follow, spawn, interact site...)
 data/                     #   Paper doll catalog, NPC definitions, quest data, opals shop catalog, premium item descriptions

server/                    # 38,450 LOC — Headless authoritative server (142 files)
 handlers/                 #   47 packet handler files (incl. shop_handler w/ processShopSellFromBag, loadout_sweep, interact_site, crafting w/ enchant + protect-craft)
 db/                       #   17 repository files, fiber DbDispatcher (8192 backlog cap), session_repo + listener
 cache/                    #   12 startup caches (item/loot/recipe/pet/costume/collection/guard/scene/spawn-zone/...)
 auth/                     #   TLS auth server (bcrypt + dummy-hash timing-oracle defense, starter equipment, login rate limiting, CSPRNG tokens)
 *.h/.cpp                  #   ServerApp, SpawnCoordinator, DungeonManager, RateLimiter, GM commands, MetricsCollector, ShutdownManager

tests/                     # 32,264 LOC — 1,687 passing test cases across 214 files (10,973 assertions)

ads_ssv_server/            # Standalone rewarded-video verifier (ECDSA-signed HTTPS callbacks)
Docs/migrations/           # 120 numbered SQL migrations (schema + content seeds)
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

<img src="https://img.shields.io/badge/C%2B%2B-204%2C500%2B_lines-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++ LOC" />
<img src="https://img.shields.io/badge/files-865-2ea44f?style=flat-square" alt="Files" />
<img src="https://img.shields.io/badge/tests-1%2C687_passing-2ea44f?style=flat-square" alt="Tests" />
<img src="https://img.shields.io/badge/assertions-10%2C973-success?style=flat-square" alt="Assertions" />
<img src="https://img.shields.io/badge/protocol-v17-blueviolet?style=flat-square" alt="Protocol" />
<img src="https://img.shields.io/badge/crypto-Noise__NK%20%2B%20AuthProof%20%2B%20DB__sessions-critical?style=flat-square" alt="Crypto" />
<img src="https://img.shields.io/badge/platforms-5-orange?style=flat-square" alt="Platforms" />
<img src="https://img.shields.io/badge/game_systems-50%2B-blueviolet?style=flat-square" alt="Game Systems" />
<img src="https://img.shields.io/badge/UI_widgets-65%2B-ff69b4?style=flat-square" alt="UI Widgets" />
<img src="https://img.shields.io/badge/chrome_widgets-29-7E57C2?style=flat-square" alt="Chrome Widgets" />
<img src="https://img.shields.io/badge/quests-167-E040FB?style=flat-square" alt="Quests" />
<img src="https://img.shields.io/badge/factions-5-red?style=flat-square" alt="Factions" />
<img src="https://img.shields.io/badge/scenes-27-FFB300?style=flat-square" alt="Scenes" />
<img src="https://img.shields.io/badge/NPCs-67_placed-orange?style=flat-square" alt="NPCs" />
<img src="https://img.shields.io/badge/items-816-yellowgreen?style=flat-square" alt="Items" />
<img src="https://img.shields.io/badge/admin_commands-44-4CAF50?style=flat-square" alt="GM Commands" />
<img src="https://img.shields.io/badge/handlers-47-9cf?style=flat-square" alt="Handler Files" />
<img src="https://img.shields.io/badge/DB_repos-17-informational?style=flat-square" alt="DB Repos" />
<img src="https://img.shields.io/badge/migrations-120-FF7043?style=flat-square" alt="Migrations" />
<img src="https://img.shields.io/badge/dialogue_trees-183-9C27B0?style=flat-square" alt="Dialogue Trees" />
<img src="https://img.shields.io/badge/loadout_slots-20-00BCD4?style=flat-square" alt="Loadout Slots" />
<img src="https://img.shields.io/badge/enchant_tiers-7-FFC107?style=flat-square" alt="Enchant Tiers" />

<br><br>

[![GitHub](https://img.shields.io/badge/GitHub-wFate-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/wFate)

<br>

</div>
