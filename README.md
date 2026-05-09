<div align="center">

# ⚔️ FateMMO Game Engine

[![CI](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/wFate/FateMMO_GameEngine/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/wFate/FateMMO_GameEngine?style=flat-square&label=🎮%20Release&color=gold)](https://github.com/wFate/FateMMO_GameEngine/releases/tag/FateMMOv2)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue?style=flat-square&logo=cplusplus)
![Lines of Code](https://img.shields.io/badge/LOC-263%2C000%2B-brightgreen?style=flat-square)
![Files](https://img.shields.io/badge/files-1%2C022%2B-2ea44f?style=flat-square)
![Tests](https://img.shields.io/badge/tests-2%2C505_passing-brightgreen?style=flat-square)
![Protocol](https://img.shields.io/badge/protocol-v26-blueviolet?style=flat-square)
![Hot Reload](https://img.shields.io/badge/⚡_hot--reload-FateGameRuntime-FF5722?style=flat-square)
![Gameplay2D](https://img.shields.io/badge/🎮_gameplay2d-20_components_%2B_11_systems-00BCD4?style=flat-square)
![Migrations](https://img.shields.io/badge/SQL_migrations-126-FF7043?style=flat-square)
![Security](https://img.shields.io/badge/security-Noise__NK%20%2B%20AuthProof%20%2B%20DB__sessions-critical?style=flat-square)
![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20iOS%20%7C%20Android%20%7C%20Linux%20%7C%20macOS-orange?style=flat-square)

**A custom 2D MMORPG engine built entirely in C++23.** Mobile-first landscape gameplay, a fully integrated Unity-style editor, server-authoritative multiplayer, **live C++ behavior hot-reload**, and a public **`engine/gameplay2d/` layer** that turns a fresh clone into a playable arena — all from a single codebase, zero middleware, zero third-party game frameworks, zero AAA framework license fees.

> 🧱 **263,000+ lines** across **1,022+ source files** — engine (110K, **incl. new public gameplay2d layer**), game (51K), server (47K), tests (56K), shaders (~800)

🌐 [**www.FateMMO.com**](https://www.FateMMO.com) &nbsp;·&nbsp; 🎬 [**Watch the Showcase**](https://www.youtube.com/watch?v=9zS-RVbranE) &nbsp;·&nbsp; 📘 [**Demo Manuals**](Documents/Demo/README.md) &nbsp;·&nbsp; 🚀 [**Quick Start**](Documents/Demo/quick-start.md)

</div>

---

### 🚀 Demo v3 — On the Runway

> **Clone, build, play.** The headline change for v3 is the new public **`engine/gameplay2d/`** layer — **20 demo-safe components + 11 systems** that turn a fresh checkout of the open-source repo into a playable top-down arena. Pair that with **live C++ behavior hot-reload** (`FateGameRuntime.dll`), an admin-only **MobAI live-tuning inspector** (protocol v26), a real MTSDF text pipeline, an AOI re-enablement campaign, and the demo engine has never been a more inviting starting point for a custom 2D MMORPG.
>
> 🎯 **Headline drops since v2:**
> - 🎮 **Public Gameplay2D Layer — `engine/gameplay2d/` (20 components + 11 systems)** — A fresh clone of the open-source repo now boots into a playable arena instead of an empty grid. **Components:** `Collider2D` (Box/Circle, layer/mask, trigger), `TriggerArea2D`, `CharacterController2D` (Local / ServerAuthoritative / AISimulated), `SpriteAnimator2D`, `CameraFollow2D`, `Health` / `Damageable` / `Attack` (faction-filtered), `Targetable`, `Nameplate`, `Interactable` / `NPC2D`, `Zone2D` / `Portal2D`, `SpawnPoint2D` / `SpawnZone2D`, `NetworkIdentity` / `ReplicatedTransform2D` / `InterestSource`, `Mob2D`. **Systems:** character controller (axis-by-axis sweep against `CollisionGrid` + `Collider2D`), sprite animator, camera follow, trigger overlap, F-to-interact, health/damage/regen + attack arcs, nameplate render, portal/zone tracking, spawn-zone respawn, click-target picker, mob aggro/chase/leash brain. **Demo:** `examples/demo_app.cpp` rewritten to wire the whole loop — WASD move, F interact, Space attack, walk-into portal, auto-respawning slime, HP-bar nameplates above every entity. Coexists cleanly with the proprietary `game/` layer thanks to distinct type names.
> - 🛰️ **MobAI Live-Tuning Inspector (protocol v26)** — Three new admin/dev-only opcodes — `CmdAdminMobAITuneApply` (`0x54`), `CmdAdminMobAITuneSubscribe` (`0x55`), `SvAdminMobAITuneState` (`0xF1`) — let an authorized client live-tune a selected mob's `MobAIComponent` from the editor inspector. Slider drag streams at up to ~10 Hz with a `finalApply=1` mouse-up fence; subscribe pulls a 5 Hz read-only diagnostics snapshot. Runtime-only, no DB writes; failures route through `SvAdminResult` (`0xC3`). Bump rejects mismatched clients at handshake.
> - 🤝 **Server-Authoritative Trade State (protocol v25)** — `SvTradeState` (`0xF0`) pushes the full owner / partner offer state on session-start / AddItem / RemoveItem / SetGold / Lock / Unlock so both clients render slot contents from server truth instead of painting optimistically off the legacy `SvTradeUpdate` ack (which carried no slot payload). Trade UX now matches the rest of the protocol's "server is the source of truth" stance.
> - 👤 **Equipment Style Replication on Enter (protocol v23 → v24)** — `SvEntityEnterMsg` now carries `armorStyle` / `hatStyle` / `weaponStyle` for `entityType==0` (player) and a real `characterId` for social handler routing. Closes a window where remote players rendered without equipment until they re-equipped (the bit-13 delta gate never fired for newly-entered ghosts because `lastSentState` matched current state at enter), and fixes silent-no-op trade/party/social/guild commands targeting other players.
> - ⚡ **Live C++ Hot-Reload — `FateGameRuntime.dll` (Stable C ABI)** — A pure C ABI (`engine/module/fate_module_abi.h`) draws a hard line between `FateEngine.exe` (host) and a separately-compiled `FateGameRuntime.dll` (reloadable behavior module). Behaviors auto-register via `FATE_AUTO_BEHAVIOR(NAME, vtable)` — drop a new `.cpp` under `game/runtime/behaviors/`, save, and the source-watcher kicks `cmake --build … --target FateGameRuntime`. The next-frame swap preserves state, failed reloads roll back to the previous module, replicated entities are skipped, and struct-size + version mismatches refuse to bind. **Editor surface:** Window menu → Hot Reload panel. **Forced OFF in shipping** so production binaries carry zero reload paths.
> - 🛰️ **Boss-Script Telegraph Foundation (protocol v21)** — Two critical-lane opcodes — `SvAOETelegraphStartBatch` (`0xEE`) and `SvAOETelegraphCancelBatch` (`0xEF`) — coalesce telegraphed AOE windups so an entire boss phase of overlapping shapes ships in a handful of packets instead of one-per-shape. A 4th boss-script queue (telegraphs) drains between summons and the start-flush at a quiescent tick-tail point; `ctx.enqueueTelegraphedAOE` auto-stamps caster + scene metadata; lethal-ordering rules cancel telegraphs the moment the caster dies.
> - 🛠️ **In-Game Admin Tools Panel + Typed Grant Pipeline (protocol v22)** — The HUD `ADMIN` button opens a full operator workbench. Three typed wire packets — `CmdAdminGrantItem` (`0x51`), `CmdAdminSetLevel` (`0x52`), `CmdAdminAddSkillPoints` (`0x53`) — replace the brittle chat-mediated `/additem` flow; results ride the existing `SvAdminResult` (`0xC3`). Shared `performAdmin*` helper closes five long-standing bugs in the legacy path.
> - ⚡ **Drain Arc — Clean Shutdown Hardening + JSONB Inventory Batch** — Pre-arc Ctrl-C took **70.6 seconds** with DB jobs orphaned and final scene saves skipped. Six sub-arcs landed continuously: per-character `AsyncSaveState` in-flight gate (31 stacked saves → max 1 in-flight + coalesced follow-up), JSONB-batched `character_inventory` UPSERT (89 per-row exec_params → one `jsonb_to_recordset` round-trip; ~6.2s → ~500ms), batched scene mob-death persistence (17–37s → 0.57s for 181 mobs), tri-state `DbDispatcher::QuiesceResult { Clean, DrainedWithFailures, TimedOut }`, and a `dispatchVoidNonCritical` lane so telemetry never poisons a clean drain. **Final wall time: 70.6s → 2.06s.**
> - 🐉 **Boss-Script Foundation — Deferred-Everything Discipline** — `BossScriptComponent` + 8-variant `TargetSelector` + a `BossScriptVTable` (`onSpawn` / `onAggro` / `onTargetLost` / `onSoftLeash` / `onTick` / `onHealthThresh` / `onDeath`) let designers script encounters without touching engine code. Every hook drains from a queue at a quiescent tick-tail point — no script body ever runs while ECS storage is iterating or damage is resolving. Threshold-walking is per-hit at enqueue time so two same-tick hits each get correct attribution.
> - 🐾 **MobAI Polish — Random Escape Lanes + Drag-Leash Drop-Aggro** — Per-episode random side-lane offsets give pursued-then-evading mobs natural-looking arcs instead of pinned axes. Three-layer drag-leash drop-aggro fix at contact radius + `+32 px` hysteresis at both resolver sites kills 20-tile sticky aggro after a kited disengage.
> - 👁️ **AOI Re-Enablement Campaign — Distance Gate + Sticky Band + Stats** — Long-standing AOI flicker is being unwound in shipped, audited stages. Phase 2 added `/aoi_stats` GM command + `AOIStats` counters (PRE-DIRTY cadence). Phase 3 wired the 640 / 1280 px hysteresis gate with `MIN_VISIBLE_TICKS=10` (0.5 s @ 20 Hz). Stage D.1 added a sticky-band 640/1280 every-3rd-tick cadence sub-tier on `SvEntityUpdateBatch`.
> - 🔤 **Single-Pass Text-Effects Pipeline + Real MTSDF Atlas** — Nameplates were faking outline by stamping each string 9–25 times in 8 directions. Replaced with a single shader-side composite (outline + shadow + fill in one pass) over a real `msdf-atlas-gen v1.4` MTSDF bake (177 glyphs, 512×512, 4 px range) — outline expansion math operates on actual signed-distance data instead of bitmap coverage.
> - 🛡️ **Patrol Layer for Stationary & Routed Guards** — `GuardComponent` gained a runtime patrol block (Stationary / PatrolRoute / PatrolArea) + a `GuardSystem` with proper FSM gating. Edit-time validator renders inline warnings for diagonal segments, broken loop closures, and hard-leash-too-small; scene-view overlay draws numbered anchor dots + polylines with frustum culling and a 256-mob-per-frame cap.
> - ✨ **Bespoke Widget Chrome — Checkpoint 8** — Chromes the 4 widgets the previous arc skipped because they aren't rectangular shells: `MenuTabBar`, `SettingsPanel`, `DPad`, `SkillArc`. **49 new styled fields** routed through existing chrome propagation walks. Legacy paths preserved verbatim under `else` for instant rollback.

---

### 🎉 Demo v2 → v3 — From Editor Loop to Playable Arena

> **Demo v2** shipped the full editor-runtime control loop — ▶️ Play / Pause / Resume / Stop, 👁️ Observe, 🗺️ Scene dropdown, 📁 File menu + Ctrl+S, all routed through atomic-write JSON serialization.
>
> **Demo v3** turns the editor into a *playable starting point*. The new public **`engine/gameplay2d/`** layer adds 20 demo-safe components and 11 systems that wire directly into the existing ECS — drop `Collider2D` + `CharacterController2D` on an entity and you have a moving body with collision; add `Mob2D` + `Health` + `Damageable` and you have a faction-filtered melee mob with aggro and leash. The rewritten `examples/demo_app.cpp` shows the full wiring on a single page. Build the engine, launch `FateDemo`, and you're playing a top-down arena before you've authored a single asset of your own.
>
> 📦 [**Download FateMMO_Demo_v2.zip**](https://github.com/wFate/FateMMO_GameEngine/releases/tag/FateMMOv2) &nbsp;·&nbsp; ⭐ [**Star the repo**](https://github.com/wFate/FateMMO_GameEngine) &nbsp;·&nbsp; 🌐 [**FateMMO.com**](https://www.FateMMO.com) &nbsp;·&nbsp; 🎬 [**YouTube Showcase**](https://www.youtube.com/watch?v=9zS-RVbranE)

---

## 💎 Key Highlights

| 🏗️ | **263,000+ LOC** of hand-written C++23 across engine, game, server, tests, and shaders — no code generation, no middleware, no AAA framework license fees |
|:---:|:---|
| 🎮 | **Public `engine/gameplay2d/` layer** — **20 demo-safe components + 11 systems** for the open-source build. `Collider2D`, `CharacterController2D`, `SpriteAnimator2D`, `CameraFollow2D`, `Health` / `Damageable` / `Attack`, `Targetable`, `Nameplate`, `Interactable` / `NPC2D`, `Zone2D` / `Portal2D`, `SpawnZone2D`, `Mob2D` (Idle/Chase/Attack/Return brain), and replication-shaped data (`NetworkIdentity`, `ReplicatedTransform2D`, `InterestSource`). A fresh clone boots into a playable arena |
| ⚡ | **Live C++ Hot-Reload** — `FateGameRuntime.dll` swaps in next frame with state preserved. Pure C ABI between host and module, struct-size + version validation, automatic rollback on failure, replicated-entity skip, save → auto-build → next-frame swap with optional source-watcher |
| 🛰️ | **MobAI Live-Tuning Inspector (protocol v26)** — `CmdAdminMobAITuneApply` (`0x54`) / `CmdAdminMobAITuneSubscribe` (`0x55`) / `SvAdminMobAITuneState` (`0xF1`). ~10 Hz slider drag with `finalApply=1` mouse-up fence; 5 Hz read-only diagnostics snapshot. Runtime-only, no DB writes |
| 🤝 | **Server-Authoritative Trade State (protocol v25)** — `SvTradeState` (`0xF0`) pushes the full owner / partner offer state on every mutation so trade UX renders from server truth instead of optimistic client paint |
| 🛠️ | **In-Game Admin Tools Panel + Typed Grant Pipeline (protocol v22)** — HUD `ADMIN` button opens the operator workbench. Three new typed packets (`CmdAdminGrantItem` / `CmdAdminSetLevel` / `CmdAdminAddSkillPoints`) replace chat-mediated `/additem`. Shared `performAdmin*` helper closes 5 long-standing legacy bugs |
| 🛰️ | **Boss-Script Telegraph Foundation (protocol v21)** — `SvAOETelegraphStartBatch` (`0xEE`) + `SvAOETelegraphCancelBatch` (`0xEF`) coalesce telegraphed AOE windups into critical-lane batches. 4th boss-script queue drains at a quiescent tick-tail point |
| 🐉 | **Boss-Script Foundation** — DB-driven (`mob_definitions.script_id`) script bindings with deferred-everything execution: every hook drains at a quiescent tick-tail point, never inline during ECS iteration. Lethal-ordering bulletproof; parallel AI worker paths gate scripts out cleanly |
| 🛡️ | **Guard Patrol Layer** — Stationary / PatrolRoute / PatrolArea modes with cardinal-only movement, BFS connectivity validation, hard-leash auto-extend, edit-time validator, and a frustum-culled scene-view overlay capped at 256 mobs/frame |
| 🔐 | **Noise_NK cryptography** — two X25519 DH ops + XChaCha20-Poly1305 AEAD, forward secrecy, symmetric epoch-gated rekeying, **encrypted AuthProof** so auth tokens never leave the client in the clear |
| 🛡️ | **Hardened v26 protocol** — 64-bit CSPRNG session tokens, timing-safe auth comparisons, anti-replay nonces, 64-bit ACK window, `CmdAckExtended` recovery, DB-backed `auth_sessions` with cross-process `LISTEN/NOTIFY` kick |
| ⚡ | **Drain Arc — clean shutdown 70s → 2s** — Per-character async-save in-flight gate, JSONB-batched inventory UPSERT (12s → 0.5s), batched scene mob-death persistence (17–37s → 0.57s for 181 mobs), tri-state `QuiesceResult` honesty pass, and a `dispatchVoidNonCritical` lane so telemetry never poisons a clean drain |
| 👁️ | **AOI Re-Enablement Campaign** — Phase 2 telemetry (`/aoi_stats` + `AOIStats` counters), Phase 3 distance gate (640 / 1280 px hysteresis, `MIN_VISIBLE_TICKS=10`), Stage D.1 sticky-band sub-tier (every-3rd-tick) on `SvEntityUpdateBatch`. Audit-driven; truth for byte savings stays NetStats `0xBA` |
| 🔤 | **Real multi-channel SDF text** — Offline `msdf-atlas-gen` MTSDF bake with single-pass shader-side outline + shadow compositing. Replaces 9-pass fake-outline pixel stamping. 4 registered fonts via `FontRegistry`, world-space and screen-space APIs |
| 📦 | **Critical-lane batched updates (v18 / v19 / v21)** — `SvSkillResultBatch` (0xEC) coalesces multi-target AOE results; `SvEntityLeaveBatch` (0xED) coalesces AOI deactivation bursts; `SvAOETelegraphStartBatch / CancelBatch` (0xEE / 0xEF) coalesce telegraph windups. Per-opcode `NetServer::OpcodeStats` for instant root-cause visibility |
| 🐾 | **Pet lifecycle cache + replay** — Cached pet-state snapshot replays at the moment of local-player creation in every new scene, closing a window where two independent timing dropouts silently discarded the packet |
| 🛡️ | **Replicated-entity save filter** — Runtime `Entity::setReplicated(true)` flag on every server-spawned entity; editor scene-save skips them. Inspector badges (`[REPLICATED]`/`[AUTHORED]`) + promote-on-edit on real content delta |
| 🌐 | **Single-instance UDP port exclusivity (Windows)** — `SO_EXCLUSIVEADDRUSE` on both IPv6 dual-stack and IPv4 paths; duplicate server launches refuse to bind instead of co-binding and silently splitting datagrams |
| 🎮 | **50+ server-authoritative game systems** — combat, skills, polymorphic loadouts, inventory, trade, guilds, arenas, dungeons, pets, costumes, collections, premium currency economy, marketplace |
| 🪡 | **Polymorphic 20-Slot Loadout** — drag potions / recall scrolls onto any slot from main inventory OR inside an open bag. Bindings reference `instance_id`, persist across logout, per-item cooldowns, **14 stale-binding sweep sites** keep everything coherent across trade/sell/destroy/craft/bank flows |
| 🛒 | **Bag-Item NPC Sell — `CmdShopSellFromBag = 0x50` (protocol v17)** — first-class wire packet for selling items in bag sub-slots; server emits both inventory + bag-content sync post-credit so the bag mirror never goes stale |
| ✨ | **Bespoke Widget Chrome — Checkpoint 8** — `MenuTabBar` / `SettingsPanel` / `DPad` / `SkillArc` joined the chrome system through 49 new styled fields. Legacy paths preserved verbatim under `else` for instant rollback |
| ▶️ | **Play / Observe / Scene-dropdown** — full editor-runtime loop in the open-source demo, with snapshot/restore on Play and configurable Observer hooks |
| 🌳 | **Branching dialogue-tree quests** — state-aware NPC trees, **interact-site framework** (cairns / altars / shrines / gravestones), DB-bound rewards on every turn-in |
| 🗄️ | **DB-driven content engine** — **126 numbered SQL migrations**, hand-tuned item catalog, **17 PostgreSQL repository files** + **12 startup caches** with fiber-based async dispatch (zero game-thread blocking) |
| 🖥️ | **Full Unity-style editor** with live inspector, undo/redo, Aseprite animation editor, atomic-write JSON saves, asset browser, dialogue-node editor, and 29 device profiles |
| 📱 | **5-platform support** from a single codebase — Windows, **macOS (native Metal, ProMotion 120fps)**, iOS, Android, Linux |
| 🧪 | **2,505 automated tests** across **259 test files** keeping every subsystem honest |
| 🎨 | **65+ custom UI widgets** with 42 theme styles + chrome-default-on, JSON-driven screens, viewport scaling, layout-class variants for tablet/compact/base, and zero-ImGui shipping builds |
| 🐾 | **Active pet system** — equip one pet, share XP, gain stat bonuses, auto-loot within radius. Pet roster includes Turtle, Wolf, Hawk, Panther, and Fox across four rarity tiers |
| 📡 | **Protocol v26 custom UDP** — critical-lane bypass for 11 load-bearing opcodes, batched enter / leave / skill-result / telegraph coalesce, 32-bit delta compression, fiber-based async DB dispatch, polymorphic loadout assignment, typed admin grant pipeline, server-authoritative trade snapshot, MobAI live tuning |
| 👁️ | **Admin observer/spectate mode** — log in without a character and roam any live scene with full replication |
| 💰 | **Premium currency economy + AdMob rewarded video** with ECDSA-signed server-side verification |
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

The demo opens straight into a **playable arena** wired up by the new public **`engine/gameplay2d/`** layer — WASD to move (collision aware), F to talk to the villager, Space to attack, walk into the portal, hunt the auto-respawning slime. From there, hit **▶️ Play** in the Scene viewport for a snapshot-protected play session, **👁️ Observe** to live-preview a scene with chrome hidden, or pick a scene from the dropdown to load it.

> 📘 **Building your own game on the engine?** Start with the [**Demo Manuals**](Documents/Demo/README.md). Recommended path: [**Quick Start**](Documents/Demo/quick-start.md) → [**Editor User Guide**](Documents/Demo/editor-user-guide.md) → tutorials [**My First MMORPG Map**](Documents/Demo/tutorials/first-map.md) and [**Local Client + Server**](Documents/Demo/tutorials/local-client-server.md). Deeper dives: [Architecture](Documents/Demo/architecture-manual.md), [Asset Pipeline](Documents/Demo/asset-pipeline.md), [Networking Protocol](Documents/Demo/networking-protocol.md), [API Reference](Documents/Demo/api-reference.md), [Troubleshooting](Documents/Demo/troubleshooting.md), [Publishing Guide](Documents/Demo/publishing-guide.md).

> 💡 **Prefer a pre-built binary?** Grab [**FateMMO_Demo_v2.zip**](https://github.com/wFate/FateMMO_GameEngine/releases/tag/FateMMOv2) from the releases page — no build required. (v3 ships when the gameplay2d arena clears its live smoke pass.)

> **Full game build:** The proprietary game client, server, and tests build automatically when their source directories are present. The open-source release includes the complete engine library, editor, and public gameplay2d layer.

---

## 🛠️ Tech Stack & Architecture

| Category | Technology & Innovation |
|----------|-----------|
| **Language** | Modern C++23 (MSVC, GCC 14, Clang 18). `std::expected`, structured bindings, fold expressions throughout |
| **Graphics RHI** | `gfx::Device` abstraction with **OpenGL 3.3 Core** & native **Metal** backends (iOS + macOS). Pipeline State Objects, typed 32-bit handles, collision-free uniform cache. Zero-batch-break SpriteBatch (10K capacity, hash-based dirty-flag sort skip), palette swap shaders, nestable scissor clipping stack, nine-slice rendering. Metal: 9 MSL shader ports, CAMetalLayer, ProMotion 120fps triple-buffering |
| **SDF Text** | True **MTSDF font rendering** — uber-shader with 4 styles (Normal/Outlined/Glow/Shadow), offline `msdf-atlas-gen` atlases, `median(r,g,b)` + `screenPxRange` for resolution-independent edges at any zoom. **4 registered fonts** (Inter-SemiBold, Fredoka-SemiBold, PressStart2P, PixelifySans) via `FontRegistry` singleton. Multi-font rendering via zero-copy `activeGlyphs_` pointer swap. World-space and screen-space APIs, UTF-8 decoding |
| **Render Pipeline** | 11-pass RenderGraph: GroundTiles → Entities → Particles → SkillVFX → SDFText → DebugOverlays → Lighting → BloomExtract → BloomBlur → PostProcess → Blit |
| **Hot-Reload Module** | **`FateGameRuntime.dll`** — pure C ABI (`engine/module/fate_module_abi.h`), `FateBehaviorVTable` dispatch, struct-size + version validation, shadow-copy LoadLibrary swap, `BeginReload` veto preserves state, replicated-entity skip, optional env-gated source-watcher (`FATE_HOTRELOAD_SOURCE_DIR` + `FATE_HOTRELOAD_BUILD_CMD`). **Forced OFF in shipping** so production carries zero reload paths |
| **Editor** | Dear ImGui (docking) + ImGuizmo + ImPlot + imnodes. Custom dark theme (Inter font family, FreeType LightHinting). **Scene viewport toolbar** (Play/Pause/Resume/Stop, Observe, Scene dropdown). Property inspectors, visual node editors, **Aseprite-first animation editor** with layered paper-doll preview, sprite slicing, tile painting, play-in-editor, undo/redo (200 actions). **Hot-Reload panel** (Window menu) with module name, build id, reload count, last error, Reload-Now button, live build-tail log. Every editor-authored JSON write goes through `writeFileAtomic` (`.tmp` + rename + cross-volume fallback) |
| **Networking** | Custom reliable UDP (`0xFA7E`, **PROTOCOL_VERSION 26**), **Noise_NK handshake** (two X25519 DH ops) + **XChaCha20-Poly1305 AEAD** encryption (key-derived 48-bit nonce prefix, symmetric rekeying every 65K packets / 15 min, epoch-gated). **Auth hardening**: 64-bit CSPRNG session tokens (libsodium), encrypted `CmdAuthProof` (0xD8) so the token never rides plaintext, timing-safe comparisons, per-account login lockout with exponential backoff. **DB-backed sessions** (`auth_sessions` table) with `LISTEN/NOTIFY session_kicked` cross-process kick. IPv6 dual-stack, 3 channel types, **26-byte header with 64-bit ACK bitfield** (v9+), RTT-based retransmission, 1 MB kernel socket buffers, 2048-slot pending queue with **critical-lane bypass for 11 load-bearing opcodes**, **batched enter / leave / skill-result / telegraph coalesce**, **CmdAckExtended** out-of-window recovery, **typed admin grant pipeline** + **MobAI live-tuning inspector** (v26) + **server-authoritative trade snapshot** (v25) + **equipment-style replication on enter** (v23/v24) |
| **Database** | PostgreSQL (libpqxx), **17 repository files**, **12 startup caches**, **fiber-based `DbDispatcher`** with **bounded backlog + per-tick pump** (async player load, disconnect saves, metrics, maintenance — zero game-thread blocking, hard cap 8192 with FIFO-preserving cancel-on-drop), connection pool (5–50) + **read-replica fallthrough** (`FATE_READ_DATABASE_URL`) + circuit breaker, priority-based 4-tier dirty-flag flushing, 30s staggered auto-save. **JSONB-batched inventory UPSERT** cuts per-player save 12s → 0.5s. `PlayerLockMap` for concurrent mutation serialization. **126 numbered SQL migrations** under `Docs/migrations/`. Inventory saves: orphan-DELETE → NULL positions → UPSERT-by-`instance_id` four-step pattern with stable instance UUIDs |
| **ECS** | Data-oriented archetype ECS, contiguous SoA memory, **56+ registered components**, generational handles, prefab variants (JSON Patch), compile-time `CompId`, Hot/Warm/Cold tier classification, `FATE_REFLECT` macro with field-level metadata, `ComponentFlags` trait policies, RAII iteration depth guard, 4096-archetype reserve capacity. `World::processDestroyQueue(scope)` tags every flush with caller intent + per-type entity breakdown for diagnosability |
| **Memory** | Zone arenas (256 MB, O(1) reset), double-buffered frame arenas (64 MB), thread-local scratch arenas (Fleury conflict-avoidance), lock-free pool allocators, debug occupancy bitmaps, ImPlot visualization panels |
| **Audio** | SoLoud (SDL2 backend, 32 virtual voices, OGG streaming, 2D spatial audio, 3 buses, 10 game events wired). All loads route through `IAssetSource::readBytes` → `Wav::loadMem(copy=true)` so packaged `.pak` archives work transparently |
| **Async & Jobs** | Win32 fibers / minicoro, **4 workers default** (configurable via `FATE_JOB_WORKERS` env), 32-fiber pool, lock-free MPMC queues, counter-based suspend/resume with fiber-local scratch arenas. Fiber-based async scene, asset, and DB loading — zero frame stalls. `tryPushFireAndForget` non-blocking variant for dispatcher producers |
| **Spatial** | Fixed power-of-two grid (bitshift O(1) lookup), Mueller-style 128px spatial hash, per-scene packed collision bitgrid (1 bit/tile, negative coord support, `isBlockedRect` AABB queries). Server loads from scene JSON at startup — zero rubber-banding |
| **Asset Pipeline** | Generational handles (20+12 bit), hot-reload (300ms debounced), fiber async decode + main-thread GPU upload, failed-load caching (prevents re-attempts), **`IAssetSource` abstraction** (`DirectFsSource` for loose files / `VfsSource` for PhysicsFS-backed `.pak`), compressed textures (ETC2 / ASTC 4x4 / ASTC 8x8) with KTX1 loader, VRAM-budgeted LRU cache (512 MB, O(N log N) eviction). Behind `option(FATE_USE_VFS …)` so behavior is byte-identical until per-platform flip |

---

## 🗡️ Game Systems

The full game built on top of this engine ships **50+ server-authoritative systems** spanning combat, skills, inventory, trade, guilds, arenas, dungeons, pets, costumes, collections, marketplace, and a premium currency economy — all wired with priority-based DB persistence, dirty-flag tracking at **95+ mutation sites**, and async auto-save. Gameplay logic is fully server-authoritative; the client is rendering, prediction, and input only.

<details>
<summary><b>🔥 Combat, PvP & Classes</b></summary>

- **Optimistic Combat** — Attack windups play immediately to hide latency; 3-frame windup (300ms), hit frame with predicted damage text + procedural lunge offset. `CombatPredictionBuffer` ring buffer (32 slots). Server reconciles final damage.
- **Combat Core** — Hit rate with coverage system (per-class `mob_hit_rate` for over-leveled-player damage floor), spell resists, block, armor reduction, 3x3 class advantage matrix (hot-reloadable JSON), crit system with class scaling.
- **Skill Manager** — Skillbook learning, cooldowns, cast-time system (server-ticked, CC/movement interrupts, fizzle on dead target), **polymorphic 20-slot loadout** (Skill / Item / Empty), passive bonuses, resource types (Fury/Mana/None). `SvSkillDefs` sends full class catalog on login.
- **Skill-Arc Consumable Bindings + Drag-Bind Repair** — Drag potions, recall scrolls, or food onto any of the 20 loadout slots from the inventory grid OR an open bag overlay. Tap consumes one stack and triggers a per-item-type cooldown. Bindings reference stable `instance_id`s, persist across logout via the polymorphic `character_skill_bar` schema, and auto-clear when the bound stack is destroyed via the **14-site stale-binding sweep**. `SkillLoadout` UI-facing facade owns canonical state; `SkillArc` HUD widget is a thin renderer over the model.
- **Skill VFX Pipeline** — Composable visual effects: JSON definitions with 4 optional phases (Cast/Projectile/Impact/Area). Sprite sheet animations + particle embellishments. 32 max active effects.
- **Status Effects** — DoTs (bleed/burn/poison), buffs, shields, invuln, transform, bewitch, source-tagged removal, stacking.
- **Crowd Control** — Stun/freeze/root/taunt with priority hierarchy, immunity checks, **diminishing returns** (per-source 15s window, 50% duration reduction per repeat, immune after 3rd application).
- **PK System** — Status transitions (White → Purple → Red → Black), decay timers, cooldowns, same-faction targeting restricted to PK-flagged players.
- **Honor & Rankings** — PvP honor gain/loss tables, per-player-pair tracking. PvE honor on every quest turn-in via formula `(5 + reqLvl×2 + chainLen×10) × tierMul`. Global/class/guild/honor/mob-kill/collection leaderboards with faction filtering, cached and paginated.
- **Arena & Battlefield** — 1v1/2v2/3v3 queue matchmaking, AFK detection (30s), 3-min matches, honor rewards. 4-faction PvP battlefields. `EventScheduler` FSM (2hr cycle, 10min signup). Reconnect grace (180s) for battlefield/dungeon, arena DC = forfeit.
- **Two-Tick Death** — Alive → Dying (procs fire) → Dead (next tick), guaranteeing kill credit without race conditions. Replicated as 3-state `deathState` with critical-lane bypass so death overlays always fire.
- **Cast-Time System** — Server-ticked `CastingState`, interruptible by CC/movement, fizzles on dead targets. Replicated via delta compression.
- **3-Tier Shield Bar** — Mana Shield depletes outer-first across 3 color tiers. Replicated via `SvCombatEvent.absorbedAmount` for instant client-side decrement.
- **Combat Leash** — Boss/mini-boss mobs reset to full HP and clear threat table after 5s idle with no aggro target, or 60s while actively aggroed (allows kiting bosses to safe spots). Engaged mobs use `contactRadius * 2` for target re-acquisition.

</details>

<details>
<summary><b>🐉 Boss-Script Foundation & Patrol Layer</b></summary>

- **DB-Driven Script Bindings** — `mob_definitions.script_id` (mig 119) maps any boss to a registered `BossScriptVTable`. Per-instance overrides on `spawn_zones.instances_json`. Empty / NULL = legacy combat behavior, no script bound.
- **Deferred-Everything Discipline** — Every script hook (`onAggro`, `onTargetLost`, `onSoftLeash`, `onTick`, `onHealthThresh`) routes through a queue drained at a **quiescent tick-tail point** in `server_app::tick`. No script body ever runs while ECS storage is iterating, damage is resolving, or the AI loop is mid-traversal. `onSpawn` fires inline at lifecycle creation; `onDeath` fires inline before reward/loot side effects with `deathHookFired` idempotency.
- **8 Target Selectors** — `HighestThreat` / `Closest` / `Furthest` / `LastAttacker` / `Random` / `LowestHP` / `HighestHP` / `ClassPreference` plus a `selectByMenu` dispatcher with deterministic tie-break. Resolver sequential branch picks via the script's selector when one is bound; parallel AI worker path explicitly gates scripts out and falls back to highest-score targeting.
- **Lethal-Ordering Contract** — Any queued event for a mob that's `!isAlive` at flush time is dropped per-entry. `onDeath` supersedes pending threshold/transition/tick fires on the dying mob. Mixing dead and live mobs in the same flush still fires the live ones.
- **Per-Hit Threshold Snapshot** — Threshold-walking happens at enqueue time in `applyMobDamage`; `nextThresholdIdx` advances at hit, queue holds one entry per crossing with its own snapshot. Two same-tick hits each get correct attribution; collapse impossible.
- **Transient Mob Cleanup** — `summonAdds` from a script tags every spawn with `Entity::setReplicated(true)` + `TransientMobComponent`. `sweepDeadTransientMobs` runs once per tick across the main world and every active dungeon-instance world to keep the ledger clean.
- **Boss-Script Inspector** — Editor inspector "Boss Script" collapsing header surfaces script id, phase, threshold ladder (with crossed-vs-pending coloring), hook bindings, selector args, and three debug buttons: **Force Advance Phase**, **Reset Thresholds**, **Grant Invuln 5s**.
- **Patrol Layer (Phase 6)** — `GuardComponent` runtime block (Stationary / PatrolRoute / PatrolArea), `GuardSystem` with proper FSM gating (combat yields motion, combat-exit dwells `kCombatExitDwell=1.0s` before resuming, physical arrival at home anchor checked before re-acquiring the cycle). Cardinal-only stepping, dt-based step budget, step clamped to remaining axis distance — physically cannot overshoot.
- **Edit-Time Validator** — `validatePatrolEditTime` mirrors the server's `applyPatrolToGuard` validator and renders inline orange warnings in the editor for diagonal segments, broken loop closures, disconnected PatrolArea components, missing origin anchors, and hard-leash-too-small. Green checkmark when valid.
- **Scene-View Patrol Overlay** — Two stacked passes (live-entity from local `World`, authored fallback from `ContentBrowserPanel.spawnList()`) with frustum culling and a `MAX_DEBUG_MOBS_PER_FRAME=256` cap. Numbered anchor dots + polylines for routes, translucent cell rectangles for areas, orange dotted current-segment line during active movement.

</details>

<details>
<summary><b>🌳 Quest Trees & Interact-Sites</b></summary>

- **Dialogue-Tree Framework** — Every quest is a JSON-authored branching tree (`assets/dialogue/quests/<id>.json`) with `offer` / `inProgress` / `turnIn` sub-trees, 12-action vocabulary (`AcceptQuest`, `CompleteQuest`, `OpenShop`, `OpenScreen`, `SetFlag`, `Goto`, `EndDialogue`, etc.), and condition-gated visibility per choice (`HasItem`, `HasFlag`, `HasInventorySpace`, `HasCompletedQuest`, `HasActiveQuest`). **183 dialogue JSON files**: 124 quest trees + 33 NPC trees + 26 interact-site trees.
- **11 Objective Types** — `Kill`, `Collect`, `Deliver`, `TalkTo`, `Interact`, `PvP`, `Explore`, `KillInParty`, `CompleteArena`, `CompleteBattlefield`, `PvPKillByStatus`. Prerequisite chains, max 10 active per character.
- **Interact-Site Framework** — `InteractSiteComponent` placed on world entities (cairns, altars, shrines, gravestones, ward-stones) progresses `ObjectiveType::Interact` quests via player click, sets `CharacterFlagsComponent` flags, and triggers tree dialogues with first-visit + revisit nodes. **Wire packet (`CmdInteractSite` 0xE2 / `SvInteractSiteResult` 0xE3)** keys off `siteStringId` directly — no PIDs, identical scene-JSON resolution on both sides.
- **Honor + Premium Currency on Every Turn-In** — `QuestRewards` carries opt-in honor + premium currency rewards. Both are applied in `QuestManager::turnInQuest` after objective verification + Collect/Deliver consumption. Granted rewards echo back to chat as `[Quest]` lines.
- **Branching NPC Dialogue** — State-aware quest-tree picker at every NPC. Priority: turnIn > inProgress > offer > NPC ambient > legacy greeting. Driven by `QuestGiverComponent.questIds`.
- **`CharacterFlagsComponent` client mirror (v12)** — `SvCharacterFlagsSnapshot` (login) + `SvCharacterFlagDelta` (per-mutation) replicate the player's flag set so dialogue `HasFlag` conditions evaluate locally without round-trips. Enables hard-branching trees on the client.

</details>

<details>
<summary><b>✨ Progression, Items & Collections</b></summary>

- **Fixed Stats & XP** — Gray-through-red level scaling, 0%–130% XP multipliers. Base stats fixed per class to balance the meta, elevated only by gear, collections, and the active pet.
- **Collections System** — DB-driven passive achievement tracking across multiple categories (Items / Combat / Progression / Quest milestones). Permanent additive stat bonuses across many stat types. Costume rewards on completion. `SvCollectionSync` + `SvCollectionDefs` packets.
- **7-Tier Enchant Economy** — `mat_enhance_stone_basic → legendary` rebanded into clean 10-level windows (1-10, 11-20, …, 61-70). Drag-driven flow — `CmdEnchantMsg` carries explicit `stoneSlot` (no fallback to first-match-by-id; rejects on tier mismatch). Risky-tier failure destroys equipment rather than flagging `isBroken`. Gold costs scale across the ladder. Server-wide broadcast on successful high-tier enchants. (Success-rate tables intentionally omitted from public docs.)
- **Protected Stones as Physical Items** — Drag a Protection Scroll onto a base enhancement stone → server crafts a real `_protected` item that lives in inventory, persists across logout, and survives moves/trades. Atomic post-consume-aware space check (walks main inv + every open bag), rollback on placement failure. Two confirm-dialog popups gate every attempt. Same-container constraint: only `inv-stone → inv-equip` and `same-bag stone → same-bag equip` drags are wired (cross-container drags silently cancel).
- **Socket System** — Accessory socketing (Ring / Necklace / Cloak), weighted stat rolls, server-authoritative with re-socket support.
- **Core Extraction** — Equipment disassembly into 7-tier crafting cores based on rarity and enchant level. Common excluded.
- **Crafting** — 4-tier recipe book system (Novice / Book I / II / III) with ingredient validation, level/class gating, gold costs. `RecipeCache` loaded at startup. **Bag-aware** — `canAddItem` verifies sub-slot space before commit.
- **Consumables Pipeline** — Many effect types fully wired: HP/MP Potions, SkillBooks (class/level validated), Stat Reset, Town Recall (blocked in combat/instanced content), XP-grant items, EXP Boost Scrolls, cross-scene party teleport, Soul Anchor (auto-consumed on death to prevent XP loss), revive-in-place, **Merchant Pass** (30-day marketplace listing access, stacking from current expiry).
- **Costumes & Closet** — DB-driven cosmetic system. 5 rarity tiers, per-slot equipping, master show/hide toggle, paper-doll integration. Multiple grant paths: mob drops, collection rewards, shop purchase. Full replication via 32-bit delta field mask.
- **💎 Premium Currency** — Direct-credit mob drops, DB persistence, `SvPlayerState` replication, menu-driven shop with server-validated purchases against JSON catalog. QoL items priced as grind goals, not power.
- **Per-Item Stack Caps** — `Inventory::addItem` / `moveItem` / `moveBagItem` honor per-item `max_stack` and split oversized drops across multiple slots.

</details>

<details>
<summary><b>🌍 Economy, Social & Trade</b></summary>

- **Inventory & Bags** — 16 fixed slots + 10 equipment slots. Nested container bags (1–10 sub-slots). Auto-stacking consumables/materials. Drag-to-equip/stack/swap/destroy with full server validation. UUID v4 item instance IDs (stable across saves). Tooltip data synced. **Configurable pickup priority** (Inventory-First / Bag-First) via Settings panel. **Bag-source NPC sell pipeline** — long-press a bag sub-slot to surface the Sell context entry; the new `CmdShopSellFromBag` (0x50) packet routes to a dedicated server handler that validates bag container + sub-slot + soulbound state and emits both inventory + bag-content sync post-credit so the bag mirror never goes stale.
- **Bank & Vault** — Persistent DB storage for items and gold. Flat deposit fee. Full `ItemInstance` metadata preserved through deposit/withdraw. Gold withdraw cap check prevents silent loss.
- **Merchant Pass + Marketplace** — `item_merchant_pass` gates the Marketplace `ListItem` flow with `merchantPassExpiresAtUnix > now()` check. Stacking semantics: `expires_at = max(now, current_expires) + 30 days` so consuming an active pass extends from existing expiry. Per-character `max_marketplace_listings` column replaces the hardcoded constant — future "Listing Slot Extender" SKUs can `+= grant`. Character-select **PREMIUM pill** + market-panel **`PASS Nd` / `NO PASS` status pill**.
- **Market & Trade** — Peer-to-peer 2-step security trading (Lock → Confirm → Execute). 8 item slots + gold. Slot locking prevents market/enchant during trade. Auto-cancel on zone transition. Marketplace with 2% tax, status lifecycle (Active / Sold / Expired / Completed), seller-claim gold flow, jackpot pools, atomic buy via `RETURNING`. **Marketplace expiry auto-return for offline sellers** — `tickMaintenance` builds online-character set every 5 min, fiber dispatches `autoReturnExpiredOffline(onlineSet, limit=50)` which re-INSERTs items into the seller's first free main slot (preserving original `instance_id`).
- **Premium Currency Shop** — In-game menu tab (no NPC needed). JSON catalog (`assets/data/opals_shop.json`) — bags, revive charges, pet eggs, **Merchant Pass**, **Recall Scroll**, and more. Server validates every purchase, returns updated balance.
- **Crafting** — 4-tier recipe book with ingredient validation and level/class gating.
- **Guilds & Parties** — Ranks, 16x16 pixel symbols, XP contributions, ownership transfer. 3-player parties with per-member XP bonuses and loot modes (FreeForAll / Random per-item).
- **Friends & Chat** — 50 friends, 100 blocks, online status with live `currentScene` enrichment. **`FriendsPanel`** with 3 tabs (Friends / Requests / Blocked) and inline Whisper / Party-invite / Remove / Accept / Decline / Cancel / Unblock actions. 7 chat channels (Map / Global / Trade / Party / Guild / Private / System), cross-faction garbling, server-side mutes (timed), profanity filtering (leetspeak normalization). **21 per-prefix system broadcast colors**. Long-press → Whisper UX prefills `/w <name> ` in the chat input.
- **Bounties** — PvE bounty board (max 10 active, 48hr expiry), 2% tax, guild-mate protection, 12hr guild-leave cooldown, party payout splits. **`BountyPanel`** with Active / My Bounties / History tabs surfaced via interact-site Bounty Boards in faction villages.
- **Economic Nonces** — `NonceManager` with random uint64 per-client, single-use replay prevention, 60s expiry. Wired into trade and market handlers.

</details>

<details>
<summary><b>🌍 World Architecture</b></summary>

- **Handcrafted World** — Adventure zones, faction villages, instanced dungeon floors, sub-arenas for boss rotation, transit zones. Authored scenes ship as JSON under `assets/scenes/`; per-floor dungeon templates spin per-party instances at runtime.
- **Faction Identity** — Each faction fields its own playstyle, color palette, and personality tone. Faction guards (mage / archer / boss-tier warrior) defend villages and gate enemy-faction passage through key zones; PvP is enabled in contested territories. Specifics around the deeper world lore are reserved for in-game discovery.
- **Innkeeper NPCs** — Bind your respawn point at any faction inn. `recallScene` persisted to DB. Recall Scrolls teleport to last bound inn.
- **Aurora Gauntlet** — Multi-zone PvP tower with hourly faction-rotation buff (wall-clock `hour%4` rotation). Aether Stone + gold entry. World boss with extended respawn cadence and a deep loot table. Live `GauntletHUD` (TopRight) + `GauntletResultModal` top-10 leaderboard.
- **Fate Guardian** — Rotating world boss with server-wide event broadcasts. Multi-spawn-point support (1-of-N random) prevents same-scene consecutive spawns.

</details>

<details>
<summary><b>🏰 World, AI & Dungeons</b></summary>

- **Mob AI** — Cardinal-only movement with L-shaped chase pathing, axis locking, wiggle unstuck, roam/idle phases, threat-based aggro tables (8-slot bounded `AggroLedger` with timeout-based lazy invalidation), `shouldBlockDamage` callback (god mode). **Server-side DEAR** — mobs in empty scenes skipped entirely, distance-based tick scaling. Wall collision is intentional — enables classic lure-and-farm positioning tactics.
- **Sequential + Parallel AI** — Main world non-Normal mobs route through the sequential path; high-population scenes scale into the parallel `tickMobGroup` worker path. Boss-script hooks gate cleanly out of the parallel branch and resume on the next sequential pass.
- **Spawns & Zones** — `SceneSpawnCoordinator` per-scene lifecycle (activate on first player, teardown on last leave), `SpawnZoneCache` from DB (circle/square shapes), respawn timers, death persistence via `ZoneMobStateRepository` (prevents boss respawn exploit). **Dungeon-template scenes are death-flag-gated** (`persistDeathState=false`) so per-party instances don't poison the global state. `createMobEntity()` static factory. Collision-validated spawn positions. Per-entity stat overrides via `spawn_zones.instances_json` — HP/damage/attackRange/leashRadius/respawnSeconds/hpRegenPerSec per-instance.
- **Hard-Leash + Soft-Leash** — Bosses opt-in to `hard_leash_radius` (mig 117) for arena-room safety; out-of-radius first observation arms a 2s grace window before invuln re-engages. Patrol layer auto-extends the leash to match patrol footprint.
- **Quest System** — 11 objective types with prerequisite chains, branching NPC dialogue trees (enum-based actions + conditions), max 10 active per character.
- **Instanced Dungeons** — Per-party ECS worlds, 10-minute timers, boss rewards, daily tickets (per-dungeon), invite system (30s timeout), celebration phase. Reconnect grace (180s). Event locks prevent double-enrollment. Per-minute chat timer. Per-dungeon HP×1.5 / damage×1.3 multipliers for 3-player party tuning.
- **Aurora Gauntlet** — Multi-zone PvP with hourly faction-rotation buff, wall-clock `hour%4` rotation. Aether Stone + gold entry. World boss with deep loot table. Zone scaling with steep drop-offs at the cap. Death ejects to Town. Live HUD + result modal with top-10 leaderboard.
- **🐾 Active Pet System** — Equip one pet at a time; active pet shares XP from mob kills, contributes to `equipBonus*` (HP + crit rate + XP bonus) via `PetSystem::applyToEquipBonuses` (recalc on equip/unequip/level). Pet roster includes 🐢 **Turtle**, 🐺 **Wolf**, 🦅 **Hawk**, 🐆 **Panther**, and 🦊 **Fox** across four rarity tiers. Premium tier knobs (interval/itemsPerTick/radius) on `pet_definitions`. Client-side `PetFollowSystem` keeps the active pet trailing the player with a 4-state machine (Following / Idle / MovingToLoot / PickingUp). Server-authoritative auto-looting (per-rarity tick, per-rarity radius, ownership + party aware). DB-level unique partial index enforces one active pet per character. Pet eggs purchasable from the premium currency shop.
- **Loot Pipeline** — Server rolls → ground entities → spatial replication → pickup validation → 90s despawn. Per-player damage attribution, live party lookup at death, strict purge on DC/leave. Epic/Legendary/Mythic server-wide broadcast; party loot broadcast to all members.
- **NPC System** — 10 NPC types: Shop, Bank, Teleporter (with item/gold/level costs), Guild, Dungeon, Arena, Battlefield, Story (branching dialogue), QuestGiver, Innkeeper (respawn binding). Proximity validation on all interactions. `EntityHandle`-based caching for zone-transition safety.
- **Event Return Points** — Centralized system prevents players from being stranded after disconnecting from instanced content. Return point set on event entry, cleared on normal exit, re-set on grace rejoin.
- **Trade Cleanup** — Active trades cancelled on disconnect, partner inventory trade-locks released via `unlockAllTradeSlots()`, preventing permanently locked slots.
- **👁️ Admin Observer / Spectate Mode** — Admin-role accounts can `/spectate <scene>` into any live scene *without a character entity* — replication still fires, ghost entities interpolate, MobAI continues ticking via `sceneObserverCounts_` presence refcount. `SvSpectateAck` (0xD9) returns typed status. Sentinel log `observer_only_ticked=0` detects regressions. Perfect for live-ops debugging, boss-fight reviews, and anti-cheat spot checks.

</details>

---

## 🎨 Retained-Mode UI System

Custom data-driven UI engine with **viewport-proportional scaling** (`screenHeight / 900.0f`) for pixel-perfect consistency across all devices. Anchor-based layout (12 presets + percentage sizing), JSON screen definitions, 9-slice rendering, two-tier color theming, virtual `hitTest` overrides for mobile-optimized touch targets. **Layout-class variant system** so a screen authored against iPhone 17 Pro can ship `<screen>.tablet.json` / `<screen>.compact.json` siblings without forking widget code — runtime auto-classifier picks per device aspect, base file is the fallback.

- **65+ Widget Types:** 25 Engine-Generic (Panels, ScrollViews, ProgressBars, Checkboxes, ConfirmDialogs, NotificationToasts, LoginScreen, ImageBox, TextInput with masked password mode, **PanelChrome / TabRail / DebugChromePanel / Divider** chrome primitives) and **40+ Game-Specific** (**chrome'd DPad / SkillArc / MenuTabBar / SettingsPanel** via the bespoke-widget pass, **SkillArc** 4-page polymorphic C-arc with cursor-tracking drop-highlight, **SkillLoadoutStrip** for in-panel drag/drop loadout management with circle slots + page-dot hit testing, FateStatusBar, InventoryPanel with paper doll + bag context-menu Sell entry, CostumePanel, CollectionPanel, ArenaPanel, BattlefieldPanel, **PetPanel**, **OpalsShopPanel**, **BountyPanel**, **FriendsPanel**, **GauntletHUD**, **GauntletResultModal**, CraftingPanel, ShopPanel with bag-source confirm popup, MarketPanel with buy confirmation + status lifecycle + **PASS pill**, BagViewPanel with `bagPanelOffset` knob, EmoticonPanel, QuantitySelector, PlayerContextMenu, ChatIdleOverlay, BossHPBar, **NpcDialoguePanel** with state-aware quest-tree picker, DeathOverlay, **enchant_confirm_dialog**, **enchant_protect_confirm_dialog**, and more).
- **JSON Screens & 42 Theme Styles:** Parchment, HUD dark, dialog, tab, scrollbar, **14 chrome styles** (panel_chrome.default / title_pill / close_x + 8 panel variants + tooltip; tab_rail.default / active / inactive). **Chrome default-on** — 25 per-panel + 3 manager `xxxUseChrome_` toggles set to `true`; **extended to 4 bespoke-shape widgets via 49 new styled fields** (no new manager toggles, no new View-menu items). Legacy paths preserved verbatim under `else` for instant rollback. Full serialization of layout properties, fonts, colors, and inline style overrides. Ctrl+S **dirty-document-only save** (S148 — three independent dirty buckets, no spurious writes) with atomic write. Hot-reload with 0.5s polling + suppress-after-save guard.
- **Paper Doll System:** `PaperDollCatalog` singleton with JSON-driven catalog (`assets/paper_doll.json`) — body/hairstyle/equipment sprites per gender with style name strings, direction-aware rendering with per-layer depth offsets and frame clamping, texture caching, editor preview panel with live composite + Browse-to-assign. Used in game HUD, character select, and character creation.
- **Zero-ImGui Game Client:** All HUD, nameplates, and floating text render via SDFText + SpriteBatch. ImGui is compiled out of shipping builds entirely.
- **Mobile-First, No Right-Click Design:** Every interaction works on touch — long-press, drag-and-drop, tap, confirm popups. The polymorphic skill-arc loadout, the enchant flow, the protect-craft flow, and the bag-source NPC sell are all touch-friendly — drag-only or two-tap context menus, no right-clicks required.
- **120+ UI tests.**

---

## 🔒 Server & Networking

**Headless 20 Hz server** (`FateServer`) with max **2,000 concurrent connections**. **58 handler files**, **17 DB repository files**, **12 startup caches**, **15-min idle timeout**, graceful shutdown with player save flush — wall time **70s → 2s** post-Drain-Arc. Every game action is server-validated — zero trust client. **PROTOCOL_VERSION 26** — v9 reliability rebuild → v10 interact-sites → v11 site-string-id swap → v12 `CharacterFlags` client mirror → v13 `SvConsumableCooldown` → v14 explicit-stone enchant → v15 `CmdCraftProtectStone` → v16 polymorphic skill-bar bindings → v17 `CmdShopSellFromBag` (0x50) → v18 `SvSkillResultBatch` (0xEC) AOE coalesce → v19 `SvEntityLeaveBatch` (0xED) AOI deactivation coalesce → v20 `CmdAckExtended` envelope-compatibility → **v21** `SvAOETelegraphStartBatch` (0xEE) + `SvAOETelegraphCancelBatch` (0xEF) telegraph foundation → **v22** typed admin grant pipeline (`CmdAdminGrantItem` 0x51 / `CmdAdminSetLevel` 0x52 / `CmdAdminAddSkillPoints` 0x53) → **v23** equipment styles in `SvEntityEnterMsg` → **v24** `characterId` for player enter (social handler routing) → **v25** `SvTradeState` (0xF0) server-authoritative trade snapshot → **v26** `CmdAdminMobAITuneApply` (0x54) / `CmdAdminMobAITuneSubscribe` (0x55) / `SvAdminMobAITuneState` (0xF1) live MobAI tuning.

<details>
<summary><b>🔐 Transport & Encryption</b></summary>

| Property | Value |
|----------|-------|
| Protocol | Custom reliable UDP (`0xFA7E`, **v26**), Win32 + POSIX |
| Encryption | **Noise_NK handshake** — two X25519 DH ops (`es` + `ee`, BLAKE2b-512 derivation, protocol-name domain separator) + **XChaCha20-Poly1305 AEAD** (key-derived 48-bit session nonce prefix OR'd with 16-bit packet sequence, 16-byte tag, separate tx/rx keys). Symmetric rekey every 65K packets / 15 min, **epoch-gated** (4-byte LE epoch payload, gated by `tryAdvanceRekeyEpoch` so retransmits dedupe instead of desyncing keys). Anonymous-DH fallback removed — every session is authenticated against the server's static identity key |
| Server Identity | Long-term X25519 static key (`config/server_identity.key`); public key distributed with client for MITM prevention. Key file is `chmod 0600` on POSIX, locked-DACL on Windows |
| Auth Hardening | **64-bit session tokens** generated via libsodium CSPRNG (`PacketCrypto::randomBytes`). **`CmdAuthProof` (0xD8)**: auth token is encrypted under the Noise session key *after* handshake — never traverses the wire in plaintext. Timing-safe comparisons via `sodium_memcmp`. Per-account login rate-limit with exponential backoff, auth-token TTL. **`AuthPhase` state machine** (HandshakePending → ProofReceived → Authenticated) gates non-system packet handlers — game commands are dropped until proof verifies |
| DB-Backed Sessions | **`auth_sessions` table** with PK `token`, partial unique index on `(account_id) WHERE activated_at IS NOT NULL`. `consumeAndActivate` is atomic: SELECT-FOR-UPDATE, DELETE-RETURNING any prior active session, NOTIFY `session_kicked` payload `{node, acct, cid}`. **`SessionListener`** runs `LISTEN session_kicked` on a dedicated `pqxx::connection` (NOT pooled), filters by `node == myServerNode_`, drops kick events into a thread-safe queue drained by `ServerApp::tick`. **Multi-process ready** — login on node B kicks the session on node A within ~1s |
| Fail-Closed Encrypt | Both `NetClient::sendPacket` and `NetServer::sendPacket` drop + LOG_ERROR on encrypt failure rather than fall back to plaintext. `payloadSize` bounds-checked before `sendTo` AND before `trackReliable` |
| IPv6 | Dual-stack with IPv4 fallback (DNS64/NAT64 — iOS App Store mandatory). Auth TCP uses `getaddrinfo(AF_UNSPEC, ...)` so DNS names work and SNI/X509 hostname verification is wired. **`SO_EXCLUSIVEADDRUSE`** on Windows so duplicate server launches refuse to bind instead of co-binding |
| Channels | Unreliable (movement, combat events), ReliableOrdered (critical), ReliableUnordered |
| Packets | 26-byte header (v9+: ackBits widened 32→64-bit), RTT estimation (EWMA 0.875/0.125), retransmission delay `max(0.2s, 2*RTT)`, zero-copy retransmit. Pending-packet queue 2048 slots, 75% congestion threshold. **`SvEntityEnterBatch`** coalesce, **`CmdAckExtended`** out-of-window recovery, epoch-gated `Rekey`. **v13–v22 packets:** `SvConsumableCooldown` (0xE9), `CmdCraftProtectStone` (0xEA), `SvCraftProtectStoneResult` (0xEB), polymorphic `CmdAssignSlot` (0x36), `CmdShopSellFromBag` (0x50), `SvSkillResultBatch` (0xEC), `SvEntityLeaveBatch` (0xED), `SvAOETelegraphStartBatch` (0xEE), `SvAOETelegraphCancelBatch` (0xEF), `CmdAdminGrantItem` (0x51), `CmdAdminSetLevel` (0x52), `CmdAdminAddSkillPoints` (0x53) |
| Socket Buffers | 1 MB kernel send/recv (`SO_SNDBUF`/`SO_RCVBUF`) — prevents silent drops during burst replication |
| Payload | 1200 B UDP standard (`MAX_PAYLOAD_SIZE=1174` after v9's 26-byte header); large reliables up to 16 KB (handler buffers bumped 4K→16K across 9 sites) |
| Critical-Lane Bypass | **11 load-bearing opcodes** (`SvEntityEnter`, `SvEntityLeave`, `SvPlayerState`, `SvZoneTransition`, `SvDeathNotify`, `SvRespawn`, `SvKick`, `SvScenePopulated`, `SvEntityEnterBatch`, `SvSkillResultBatch`, `SvEntityLeaveBatch`) bypass reliable-queue congestion check |
| Rate Limiting | Per-client, per-packet-type token buckets (**55+ packet types** across 14 categories), violation decay, auto-disconnect at 100 violations |
| Anti-Replay | Economic nonce system (trade/market, single-use uint64, 60s expiry, cleaned on disconnect), connection cookies (FNV-1a time-bucketed anti-spoof — not a cryptographic MAC), atomic dungeon-ticket claim |
| Auth Security | TLS 1.2+ with AEAD-only ciphers, shipping enforces `SSL_VERIFY_PEER` (no self-signed), login rate limiting, bcrypt timing-oracle defense (dummy hash run on unknown usernames), version gate |
| Auto-Reconnect | `ReconnectPhase` state machine, exponential backoff (1s→30s cap), 60s total timeout |
| Idle Timeout | 15-min inactivity auto-disconnect, per-client activity tracking, system chat warning before kick |
| Per-Opcode Telemetry | `NetServer::OpcodeStats` per-opcode counters dump on every 5s tick-stats line and on congestion-fire — instant root-cause visibility for any future packet flood |
| Mass-DC Hardening | **Per-tick disconnect budget** (`MAX_DISCONNECTS_PER_TICK=16`), async session removal + trade cancel via `dbDispatcher_`, **DbDispatcher backlog** (FIFO-preserving, 8192 hard cap, threshold instrumentation, drains in events phase), **bumped JobSystem to 4 workers default** (`FATE_JOB_WORKERS` env override) |

</details>

<details>
<summary><b>📡 Replication & AOI</b></summary>

- **Area of Interest** — Spatial-hash culling (128px cells), 640px activation / 768px deactivation (hysteresis). Scene-filtered. Optional `visibilityFilter` callback (GM invisibility).
- **Delta Compression** — **32-bit field mask** (17 fields: position, animFrame, flipX, HP/maxHP, moveState, animId, statusEffects, deathState, casting, target, level, faction, equipVisuals, pkStatus, honorRank, costumeVisuals). Only dirty fields serialized. Expanded from 16-bit for costume support.
- **Batched Updates** — Multiple entity deltas packed into single `SvEntityUpdateBatch` packets (~90% header overhead reduction vs per-entity packets). ~50 deltas packed into 2-3 batched packets per tick.
- **`SvEntityEnterBatch` Coalesce (v9)** — Initial replication sends one batch packet per `MAX_PAYLOAD_SIZE` budget instead of one `SvEntityEnter` per entity. Batch is critical-lane (bypasses congestion).
- **`SvSkillResultBatch` Coalesce (v18)** — Multi-target AOE skill results coalesce into one batch packet so heavy AOEs no longer spam individual reliables and overrun the pending queue. Critical-lane.
- **`SvEntityLeaveBatch` Coalesce (v19)** — AOI deactivation bursts coalesce into one batch packet. Critical-lane.
- **Tiered Frequency** — Near 20 Hz / Mid 7 Hz / Far 4 Hz / Edge 2 Hz. HP + deathState changes force-sent regardless of tier. Near tier covers full viewport diagonal for smooth visible-mob updates.
- **Scene-Scoped Broadcasts** — Combat packets (skill results, auto-attacks, DoT ticks, emoticons) are scene-filtered, not global. `SvCombatEvent` demoted ReliableOrdered → Unreliable (reduced queue saturation ~30–40 reliables/sec).
- **Scene Population Sync** — `SvScenePopulated` handshake ensures loading screen stays up until all initial entity data arrives. Eliminates mob pop-in after zone transitions. 5s client-side safety timeout.
- **Ghost Lifecycle** — Robust enter/leave/destroy pipeline with `recentlyUnregistered_` bridge, `processDestroyQueue("scope")` with per-type breakdown, full disconnect cleanup.
- **NPC Replication** — `SvEntityEnterMsg` extended with npcId + npcStringId + targetFactions; entityType=2 uses `createGhostNPC` factory (no `EnemyStatsComponent`, keeps NPCs out of mob spatial hash).
- **Character Flags Mirror (v12)** — `SvCharacterFlagsSnapshot` (login) + `SvCharacterFlagDelta` (per-mutation) replicate `CharacterFlagsComponent.flags` so dialogue `HasFlag` conditions evaluate locally without server round-trips.
- **Pet Lifecycle Cache + Replay** — Cached `SvPetState` snapshot replays at the moment of local-player creation in every new scene, closing a window where two independent timing dropouts silently discarded the packet.

</details>

<details>
<summary><b>💾 Persistence & Database</b></summary>

| Layer | Detail |
|-------|--------|
| **Circuit Breaker** | 3-state (Closed → Open → HalfOpen), 5 failures → 30s cooldown, single-probe pattern |
| **Priority Flushing** | 4 tiers: IMMEDIATE (0s — gold/inventory/trades), HIGH (5s — level-ups/PK/zone transitions), NORMAL (60s — position), LOW (300s — pet/bank). 1s dedup, 10/tick drain |
| **Auto-Save** | 30s staggered per-player with `forceSaveAll=true`. Maximum 30s data loss window on crash. Event-triggered HIGH priority saves on zone transition and level up |
| **Fiber-Based `DbDispatcher`** | Header-only, runs on JobSystem worker threads (4 default, env-configurable). Covers async player load (18 queries as single job), disconnect saves, market browse/list, persistence saves, maintenance, expired-death cleanup, and all 5 MetricsCollector DB ops. **Bounded backlog (8192 hard cap, FIFO-preserving, threshold-instrumented).** **Zero game-thread blocking.** |
| **Async Save Coalescing** | Per-character `AsyncSaveState` in-flight gate ensures max 1 save in-flight + 1 coalesced follow-up. Replaces a previous bug where 31 stacked `player.saveAsyncAtomic` jobs could pile up for one player. 8 direct callsites migrated onto the gate |
| **JSONB Inventory Batch** | `character_inventory` UPSERT collapsed from 89 per-row exec_params to a single `jsonb_to_recordset` round-trip. ~6.2s → ~500ms per save |
| **Batched Mob-Death Persistence** | Per-row INSERT loop in 3 sites → batched JSONB. 17–37s → 0.57s for 181 mobs. Three sites unified |
| **Inventory Save Pattern** | Four-step orphan-DELETE → NULL-positions → UPSERT-by-`instance_id` → trailing-DELETE pattern. The NULL-positions step dodges `uq_character_inventory_slot` partial unique index on swap-induced position collisions. `instance_id` is stable across saves |
| **Dirty Flags** | `PlayerDirtyFlags` at **95+ mutation sites**. Async error re-dirties for retry. Batched mob death persistence (single DB transaction per scene per tick regardless of kill count) |
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
- **⚡ Hot-Reload Panel (Window menu)** — Module name + build id + reload count + failure count + last error + Reload-Now button + read-only play-mode-reload status (mutator compile-gated behind `FATE_HOTRELOAD_EXPERIMENTAL_PLAYMODE`). Build status row when source-watch enabled: Idle / Running / Succeeded / Failed (rc=N) + last 4 KB of redirected stdout/stderr. Source watcher kicks `cmake --build … --target FateGameRuntime` on debounced source change when `FATE_HOTRELOAD_SOURCE_DIR` + `FATE_HOTRELOAD_BUILD_CMD` env vars are set.
- **File Menu + Ctrl+S (v2)** — New Scene (gated `!inPlayMode_`), Open Scene submenu (lists `assets/scenes/*.json`), Save (Ctrl+S, gated on a current scene path), and Save As... (validated name input). **Dirty-document-only save (S148)** — three independent dirty buckets (`sceneDirty_`, `playerPrefabDirty_`, `dirtyScreens_`) drive three independent save branches. A one-pixel UI offset edit no longer touches the player prefab or current scene. Every editor-authored JSON write goes through `engine/core/atomic_write.{h,cpp}::writeFileAtomic` — `.tmp` + rename, parent-dir auto-create, copy+remove fallback for cross-volume targets, tmp cleanup on failure.
- **Entity Hierarchy** — Grouped by name+tag, color-coded (player/ground/obstacle/mob/boss), error badges, tree indentation guides.
- **Live Inspector** — Edit all 56+ component types live with **full undo/redo**. Sprite preview thumbnails. Reflection-driven generic fallback via `FATE_REFLECT`. SemiBold headings, separator lines. **Behavior (hot reload) drawer** — Behavior-name `InputText` + `Pick` popup that enumerates the live BehaviorRegistry, per-field type-detected widgets (float `DragFloat`, int `DragInt`, bool `Checkbox`), Add-Field row, read-only display of `runtimeFields`. **Boss Script + Guard Patrol inspectors** with debug buttons (Force Advance Phase / Reset Thresholds / Grant Invuln 5s / Force Resume Patrol / Skip Dwell). **49-field Bespoke Widget Chrome inspectors** (`MenuTabBar` 11, `SettingsPanel` 16, `DPad` 9, `SkillArc` 13). Every entry undo-captured.
- **Scene Interaction** — Click to select (depth-priority, closest-center), drag to move, sticky selection. Ground tiles locked (inspect-only). Entity selection auto-clears if destroyed by gameplay/network/undo.
- **Create / Delete / Duplicate** — Menu + keyboard shortcuts, deep copy via JSON serialization, locked entity protection.
- **8 Tile Tools** — Move (W), Resize (E), Rotate (R), Paint (B), Erase (X), Flood Fill (G), RectFill (U), LineTool (L). All tool-paused-only with compound undo. Collision-layer Rect/Line tools now stamp without requiring a selected palette tile, surfacing missing-precondition status in the HUD instead of silently dropping clicks.
- **Play-in-Editor** — Green/Red Play/Stop buttons. Full ECS snapshot + restore round-trip. Camera preserved. Ctrl+S blocked during play.
- **200-action Undo/Redo** — Tracks moves, resizes, deletes, duplicates, tile paint, all inspector field edits. Handle remap after delete+undo. **Domain-routed dirty marking** — Scene / PlayerPrefab / UIScreen domains route to the right dirty bucket via `UndoCommand::domain()` virtual.
- **Input Separation** — Clean priority chain: Paused = ImGui → Editor → nothing. Playing = ImGui (viewport-excluded) → UI focused node → Game Input. Tool shortcuts paused-only, Ctrl shortcuts always. Key-UP events always forwarded to prevent stuck keys.
- **Device Profiles + Layout-Class Variants** — 29 device presets (iPhone SE through iPhone 17 Pro, iPad Air/Pro, Pixel 9, Samsung S24/S25, desktop resolutions, ultrawide). Safe area overlay with notch/Dynamic Island insets. `setInputTransform(offset, scale)` maps window-space to FBO-space for correct hit testing across all resolutions. **Layout-class variant system** auto-classifies devices into Base / Compact / Tablet and loads `<screen>.tablet.json` / `<screen>.compact.json` siblings when present, falling back to the canonical base file. Editor `Fork → Tablet Variant` / `Fork → Compact Variant` buttons clone the base into a sibling so per-device tuning never clobbers the iPhone-17-Pro baseline.

</details>

<details>
<summary><b>🧩 Panels & Browsers</b></summary>

- **Asset Browser** — Unity-style: golden folder icons, file type cards with colored accent strips, sprite thumbnails with checkerboard, breadcrumb nav, search, lazy texture cache, drag-and-drop, context menu (Place in Scene / Open in Animation Editor / Open in VS Code / Show in Explorer).
- **Animation Editor** — Aseprite-first import pipeline with auto-sibling discovery, layered paper-doll preview (5-layer composite), variable frame duration, onion skinning, content pipeline conventions. Sprite Sheet Slicer (color-coded direction lanes, hit frame "H" badges, mousewheel zoom, frame info tooltips). 3-direction authoring → 4-direction runtime.
- **Tile Palette** — Recursive subdirectory scan, scrollable grid, brush size (1-5), 4-layer dropdown (Ground / Detail / Fringe / Collision), layer visibility toggles.
- **Dialogue Node Editor** — Visual node-based dialogue trees via imnodes. Speaker/text nodes, choice pins, JSON save/load (atomic), node position persistence.
- **UI Editor** — Full WYSIWYG for all 65+ widget types: colored type-badge hierarchy, property inspector for every widget, selection outline, viewport drag, undo/redo with full screen JSON snapshots. Ctrl+S dual-save + hot-reload safe pointer revalidation.
- **Network Panel** — Editor dock surfacing client-side metrics: protocol banner (`v26`), encryption status, RTT (color-coded), reliable queue depth (color-coded against 2048 cap), dropped-non-critical count, AOI entity count, host:port, Connect/Disconnect button.
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

Concrete performance, security, and reliability gains shipped in the latest waves of engine work.

| Win | Impact |
|---|---|
| 🆕 🎮 **Public Gameplay2D Layer — `engine/gameplay2d/` (20 components + 11 systems)** | The demo's biggest UX win since v2 shipped the editor-runtime loop. A fresh clone of the open-source repo now boots into a playable arena instead of an empty grid. 20 demo-safe components (`Collider2D`, `CharacterController2D`, `SpriteAnimator2D`, `CameraFollow2D`, `Health` / `Damageable` / `Attack`, `Targetable`, `Nameplate`, `Interactable` / `NPC2D`, `Zone2D` / `Portal2D`, `SpawnZone2D`, `Mob2D`, plus `NetworkIdentity` / `ReplicatedTransform2D` / `InterestSource` for replication-shaped data) + 11 systems wire the whole loop. Distinct type names mean both the public layer and the proprietary `game/` layer coexist when `FATE_HAS_GAME=1`. **23 new dedicated tests** (collider shape-aware overlap, mob aggro revalidation, trigger overlap, spawn-zone respawn, sprite animator JSON round-trip, target picker priority/distance tiebreaker, SDF-text readiness gate). Demo entry point rewritten end-to-end |
| 🆕 🛰️ **MobAI Live-Tuning Inspector (protocol v26)** | Three new admin/dev-only opcodes — `CmdAdminMobAITuneApply` (`0x54`), `CmdAdminMobAITuneSubscribe` (`0x55`), `SvAdminMobAITuneState` (`0xF1`) — let an authorized client live-tune the selected mob's `MobAIComponent` from the editor inspector. Slider drag streams at up to ~10 Hz with a `finalApply=1` mouse-up fence; subscribe pulls a 5 Hz read-only diagnostics snapshot. Runtime-only (no DB writes); failures route through `SvAdminResult` (`0xC3`). Bump rejects mismatched clients at handshake |
| 🆕 🤝 **Server-Authoritative Trade State (protocol v25)** | `SvTradeState` (`0xF0`) pushes the full owner / partner offer state on every mutation — session-start, AddItem, RemoveItem, SetGold, Lock, Unlock — so both clients render slot contents from server truth instead of painting optimistically off the legacy `SvTradeUpdate` ack (which carried no slot payload) |
| 🆕 👤 **Equipment-Style Replication on Enter (protocol v23 → v24)** | `SvEntityEnterMsg` carries `armorStyle` / `hatStyle` / `weaponStyle` for `entityType==0` (player) and a real `characterId` for social handler routing. Closes a window where remote players rendered without equipment until they re-equipped, and fixes silent-no-op trade/party/social/guild commands targeting other players |
| 🆕 ⚡ **Live C++ Hot-Reload — `FateGameRuntime.dll` (Stable C ABI)** | Pure C ABI between `FateEngine.exe` (host) and a separately-compiled `FateGameRuntime.dll` (reloadable game module). Behaviors auto-register via `FATE_AUTO_BEHAVIOR(NAME, vtable)` — drop a new `.cpp` under `game/runtime/behaviors/`, save, and the source-watcher kicks `cmake --build … --target FateGameRuntime` for you. The next-frame swap preserves state; failed reloads roll back; replicated entities skipped. ABI v3 schema (`describeFields`) supports float / int / bool with min/max/default/tooltip. Editor surface: Window menu → Hot Reload panel. Forced OFF in shipping. **20+ ABI + lifecycle + automation tests green** |
| 🆕 🛰️ **Boss-Script Telegraph Foundation (protocol v21)** | Two new critical-lane opcodes — `SvAOETelegraphStartBatch` (`0xEE`) and `SvAOETelegraphCancelBatch` (`0xEF`) — coalesce telegraphed AOE windups so a full boss phase ships in a handful of packets instead of one-per-shape. New 4th boss-script queue (telegraphs) drains between summons and the start-flush at a quiescent tick-tail point. `ctx.enqueueTelegraphedAOE` auto-stamps caster + scene metadata. Cancel-on-caster-death and idempotent start emission |
| 🆕 🛠️ **In-Game Admin Tools Panel + Typed Grant Pipeline (protocol v22)** | HUD `ADMIN` button opens the operator workbench. Three new typed wire packets — `CmdAdminGrantItem` (`0x51`), `CmdAdminSetLevel` (`0x52`), `CmdAdminAddSkillPoints` (`0x53`) — replace the chat-mediated `/additem` flow; results ride the existing `SvAdminResult` (`0xC3`). Shared `performAdmin*` helper closes 5 long-standing legacy bugs (bag capacity init, dirty flag, IMMEDIATE persist, weak instanceId, isBag detection). Initial-load wiring fixed: button now binds on first login, not just on JSON reload |
| 🆕 👁️ **AOI Re-Enablement Campaign — Distance Gate + Sticky Band + Stats** | Long-standing AOI flicker is being unwound in shipped, audited stages. Phase 2 added a `/aoi_stats` GM command + `AOIStats` counters (PRE-DIRTY cadence wording so operators don't misread numbers as byte savings). Phase 3 wired the 640 / 1280 px hysteresis gate with `MIN_VISIBLE_TICKS=10` (0.5s @ 20Hz). Stage D.1 added a sticky-band 640/1280 every-3rd-tick cadence sub-tier on top of `SvEntityUpdateBatch`. Truth for byte savings stays NetStats `0xBA` |
| 🆕 🐉 **Boss-Script Foundation — Deferred-Everything Discipline** | `BossScriptComponent` + 8-variant `TargetSelector` + `BossScriptVTable` (7 hooks). Every script body runs from a queue drained at a quiescent tick-tail point — never inline during ECS iteration or damage resolution. Lethal-ordering bulletproof (`onDeath` supersedes any threshold the killing hit happened to cross). Parallel AI worker paths gate scripts out cleanly. Threshold-walking moved to enqueue-time so two same-tick hits each get correct attribution. **+26 dedicated tests** |
| 🆕 🛡️ **Patrol Layer for Stationary & Routed Guards (mig 118)** | `GuardComponent` runtime patrol block + `GuardSystem` with proper FSM gating. Cardinal-only stepping, dt-based step budget, step clamped to remaining axis distance — physically cannot overshoot. BFS connectivity validation collapses unreachable cells to Stationary. Edit-time validator (`validatePatrolEditTime`) renders inline orange warnings for diagonal segments, broken loop closures, disconnected components, hard-leash-too-small. Scene-view overlay (numbered anchor dots + polylines + active-segment line) with frustum culling and 256-mob-per-frame budget. **42 new validator + system tests** |
| 🆕 ⚡ **Drain Arc — Async Save Coalescing + JSONB Inventory Batch (protocol v19 → v20)** | Pre-arc Ctrl-C took **70.6 seconds** with 14 DB jobs orphaned + final scene save skipped. Six sub-arcs (D.1–D.5.1, E.1) shipped continuously: per-character `AsyncSaveState` in-flight gate (31 stacked saves for one player → max 1 in-flight + coalesced follow-up), 8 direct callsite migrations, lock-wait + section-timing instrumentation, **JSONB-batched `character_inventory` UPSERT** (89 per-row exec_params → single `jsonb_to_recordset` round-trip; ~6.2s → ~500ms), batched scene mob-death persistence (17–37s → 0.57s for 181 mobs). **Final wall time: 70.6s → 2.06s.** `PROTOCOL_VERSION` envelope-bumped to v20 |
| 🆕 🔤 **Real MTSDF Atlas + Single-Pass Text Effects** | Nameplates were faking outline by drawing each string 9–25 times in 8-directional pixel offsets. Replaced with single shader-side composite (outline + shadow + fill in one pass) plus a real `msdf-atlas-gen v1.4` MTSDF bake (177 glyphs, 512×512, 4 px range). Outline expansion math now operates on actual signed distance data instead of bitmap coverage. Editor-authored 0..8 outline thickness pixel→SDF unit conversion fix so values no longer saturate at max |
| 🆕 🐾 **Pet Lifecycle Cache + Replay (zone transitions)** | Equipped pet sprite vanished after every zone transition and never returned until the next discrete pet event. Two independent timing dropouts in the `LoadingScene → InGame → local-player-create` window were silently discarding `SvPetState`. Fixed with a cache-and-replay around the packet, mirroring the `pendingPlayerState_` / `pendingRespawn_` precedents. Triple-pass live trace verified pet visibly follows in every scene |
| 🆕 📦 **AOE Crash Fix + Critical-Lane Batch Opcodes (protocol v17 → v18 → v19)** | `SvSkillResultBatch` (0xEC, v18) coalesces multi-target AOE skill results so heavy AOEs no longer spam individual reliables and overrun the pending queue. `SvEntityLeaveBatch` (0xED, v19) does the same for AOI deactivation bursts. Both critical-lane (bypass congestion). Plus `MobAI::lastTickInterval` jitter gate w/ `MAX_CACHED_INTERVAL` cap and `SvLootPickup` added to critical-lane. Phase-A `NetServer::OpcodeStats` per-opcode telemetry — instant root-cause visibility for any future packet flood |
| 🆕 🛡️ **Replicated-Entity Save Filter + Pet Bake Repair** | Runtime `Entity::setReplicated(true)` flag set by every server-spawned entity factory; editor scene-save skips replicated entities. Inspector badges (`[REPLICATED]` / `[AUTHORED]`) make state visible. Promote-on-edit clears the flag on real content delta so authored edits persist. Closes a class of "phantom NPCs in authored scene" hazards |
| 🆕 🖼️ **UI Layout-Class Variant System + Editor Camera Zoom Restore** | Layout class variants let widget styling fork by device profile / theme without forking widget code (`<screen>.tablet.json` / `<screen>.compact.json` siblings, base file is the fallback). Editor camera zoom now restores correctly after Play→Stop instead of jumping to default fit |
| 🆕 🌐 **Drop-Routing Fall-Through + Strip Cross-Rect Hit Test + Server Port Exclusivity** | UIManager drop walk now mirrors `handlePress` (walks the screen stack instead of stopping at the first non-interactive root). `InventoryPanel::hitTest` override reaches strip slots that paint outside `computedRect_` via origin offset. **`SO_EXCLUSIVEADDRUSE` on Windows UDP** — duplicate `FateServer` instances now refuse to bind instead of co-binding and splitting datagrams between processes |
| 🆕 🪡 **Skill Loadout Strip — Drag-Bind Repair + Decoupling** | Seven sub-fixes against the original strip. Two distinct root causes in the drop pipeline: `UIManager::handleRelease` was calling `acceptsDrop` *before* setting `dropLocalPos` (stale `{0,0}` rejected the drop), and `InventoryPanel::onRelease` was clearing the shared `DragPayload` before UIManager could route to `onDrop`. Plus polish: page-dot hit testing now includes draws outside `bounds_`, slots render as true circles, drop-highlight ring follows the cursor across slots, inventory grid no longer auto-shrinks with strip height/padding |
| 🆕 🛒 **Bag-Item NPC Sell Pipeline (protocol 16 → 17)** | Closes the long-standing "only sells one bag item then sell vanishes" gap. New first-class wire packet `CmdShopSellFromBag = 0x50` (`{npcId, bagSlot, bagSubSlot, quantity}`) — chosen over an `isBagItem` discriminator on `CmdShopSellMsg` because it matches the existing `CmdEquip` / `CmdEquipFromBag` precedent and keeps the legacy top-level path byte-identical. Server `processShopSellFromBag` mirrors `processShopSell`'s NPC-proximity / soulbound / ownership / sell-price / loadout-sweep / persist flow but validates bag container + sub-slot and emits **both** `SvInventorySync` AND `SvBagContentsForSlot` post-credit so the bag mirror never goes stale |
| 🆕 🧰 **Editor Ctrl+S — Dirty-Document-Only Save (S148)** | Pre-fix, every Ctrl+S unconditionally re-wrote the player prefab + the current scene + the focused UI screen; a one-pixel UI offset edit would touch all three files. Post-fix, three independent dirty buckets (`sceneDirty_`, `playerPrefabDirty_`, `dirtyScreens_`) drive three independent save branches. Marking is automatic via a callback wired into `UndoSystem::push/undo/redo` — the callback inspects each `UndoCommand`'s `domain()`. Dirty bits are cleared only after the matching write actually succeeded; partial failures leave the bit set so the next Ctrl+S retries |
| 🆕 ✨ **Bespoke Widget Chrome — Checkpoint 8** | `MenuTabBar` / `SettingsPanel` / `DPad` / `SkillArc` joined the chrome system through 49 new styled fields (11+16+9+13) routed via existing `panelUseChrome_` / `hudUseChrome_` propagation walks. Chrome path replaces `drawCircle` / `drawRing` / `drawMetallicCircle` with `RoundedRectParams` equivalents; legacy paths preserved verbatim under `else` (3 occurrences in SkillArc) for instant rollback. Defaults mirror legacy colors byte-for-byte. **No new manager toggles, no new View-menu items** |
| 🆕 🪡 **Polymorphic 20-slot skill loadout** | Drag potions/recall scrolls/food onto any of the 20 loadout slots from inventory or open bag. New `SkillLoadout` model owns canonical state; `SkillArc` HUD refactored to thin renderer; new `SkillLoadoutStrip` widget embedded in `InventoryPanel`. **14 stale-binding sweep sites** keep bindings coherent. Two migrations (`item_definitions.cooldown_ms` + `character_skill_bar` polymorphic columns). 56 new tests across `test_skill_loadout` + `test_skill_loadout_strip` + `test_skill_bar_polymorphic` |
| **Mass-disconnect game-thread protection** | Pre-arc, a 100-client timeout cluster blocked the game thread for 20–40 seconds (sync session DELETE × 300 ms RTT × 100 clients). Fix: `scheduleSessionRemoval` + `scheduleTradeCancel` route through `dbDispatcher_`; per-tick disconnect budget (`MAX_DISCONNECTS_PER_TICK=16`) spreads work across ticks; **DbDispatcher backlog** (8192 hard cap, FIFO-preserving, threshold-instrumented, drains in events phase); JobSystem bumped 2→4 workers default with `tryPushFireAndForget` non-blocking variant |
| **Demo v2 — Play / Observe / Scene-dropdown / File menu / Ctrl+S** | Open-source `FateDemo` reaches feature parity with the proprietary editor's runtime control loop. Configurable `AppConfig::onObserveStart` hook lets downstream apps swap in network spectate or other observer flows |
| **PROTOCOL v9 → v26 evolution** | v9: 64-bit ACK window, `SvEntityEnterBatch` coalesce, `CmdAckExtended`, epoch-gated rekey. v10: interact-site packets. v11: site-string-id swap. v12: `CharacterFlags` client mirror. v13: `SvConsumableCooldown`. v14: explicit-stone enchant on the wire. v15: `CmdCraftProtectStone` + `SvCraftProtectStoneResult`. v16: polymorphic `CmdAssignSlot` + `LoadoutSlotWire` + `CmdUseConsumable.source`. v17: `CmdShopSellFromBag` for bag-source NPC sells. v18: `SvSkillResultBatch` AOE coalesce. v19: `SvEntityLeaveBatch` AOI deactivation coalesce. v20: `CmdAckExtended` envelope-compatibility bump. v21: `SvAOETelegraphStartBatch` (0xEE) + `SvAOETelegraphCancelBatch` (0xEF) telegraph foundation. v22: typed admin grant pipeline. **v23: armor/hat/weapon style fields in `SvEntityEnterMsg`. v24: `characterId` for player enter (social routing). v25: `SvTradeState` (0xF0) server-authoritative trade snapshot. v26: `CmdAdminMobAITuneApply` (0x54) / `CmdAdminMobAITuneSubscribe` (0x55) / `SvAdminMobAITuneState` (0xF1) live MobAI tuning** |
| **DB-backed `auth_sessions` + cross-process LISTEN/NOTIFY kick** | Sessions survive process crashes. Multi-process deployment ready: login on node B kicks the existing session on node A within ~1s via Postgres NOTIFY |
| **Atomic write helper for every editor JSON save** | Scene saves, UI screens, dialogue nodes, animation templates / framesets / packed meta / `.meta.json` siblings — all route through `writeFileAtomic` (`.tmp` + rename + cross-volume copy fallback). Failed Save-As no longer corrupts destination |
| **`IAssetSource` + PhysicsFS VFS** | Unified read path for textures, JSON, shaders, scenes, audio, dialogue, server scene scans, shutdown config. Two implementations (DirectFs + Vfs), behind `option(FATE_USE_VFS …)` |
| **Shipping-build CI guard** — `scripts/check_shipping.ps1` wraps `VsDevCmd.bat` + `cmake --preset x64-Shipping`; every `imgui.h`/`ImGui::*`/editor member requires explicit `FATE_SHIPPING`/`EDITOR_BUILD` guard | Stripped ImGui-free shipping exe; regressions fail fast in CI |
| **Persistence contract test** — every mutable column round-trips against a live DB | Catches save-method drift across every subsystem repository (gated on `FATE_DB_HOST` env so CI without DB still passes). `TestAccountGuard` RAII wrapper ensures `ctest_*` rows clean up on `REQUIRE`-throw unwind |
| **Critical-lane bypass extended to 11 load-bearing opcodes** | Death overlays + initial-entry replication + AOE skill results + AOI deactivation bursts never get strangled under load |
| **`MetricsCollector` sync DB → async `DbDispatcher`** (5 methods) | Eliminated invisible ~300–1500ms periodic stall that manifested as client-side ~6.5s mob-freeze stutter |
| **UDP socket buffers** 64KB → 1MB (`SO_RCVBUF`/`SO_SNDBUF`) | Silent packet drops during burst replication eliminated |
| **Pending-packet queue cap** 256 → 2048 | Handles initial replication bursts in 231-entity scenes without drops |
| **Packet buffers** 4KB → 16KB across 9 handler files | Large inventory-sync / collection-sync / market-listings payloads no longer truncated |
| **`ArchetypeStorage`** reserve 256 → 4096 + no-`Archetype&`-across-`emplace_back` | Relogin crash eliminated; client hit archetype #759 mid-migration |
| **Client frame pacer** (VSync off + `SDL_Delay` @ 240 FPS target) | MSVC Debug→Release profile + pacer restored 94 → 250 FPS on Win11/DWM |

---

## ⚠️ Known Issues

These are tracked issues in the open-source engine build. Contributions addressing any of these are welcome.

**Build warnings (non-blocking):**
- Unused parameter warnings in virtual base class methods (`world.h`, `app.h`) — intentional empty defaults for overridable hooks
- `warn_unused_result` on `nlohmann::json::parse` in `loaders.cpp` — the call validates JSON syntax; return value is intentionally discarded

**Architectural:**
- **AOI (Area of Interest) re-enablement is in progress** — Phase 2 telemetry (`/aoi_stats` + `AOIStats` counters) and Phase 3 distance gate (640 / 1280 px hysteresis, `MIN_VISIBLE_TICKS=10`) have shipped. Stage D.1 added a sticky-band 640/1280 every-3rd-tick cadence sub-tier on `SvEntityUpdateBatch`. Audit-driven; truth for byte savings stays NetStats `0xBA`. Replication still sends all entities until the live cutover smoke clears.
- **Fiber backend on non-Windows** uses minicoro, which is less battle-tested than the Win32 fiber path. Monitor for stack overflow on deep call chains.
- **Hot-reload is Windows-only this slice.** `performSwap` returns "Hot reload only implemented on Windows in this slice" elsewhere. Linux/Mac needs `dlopen`/`dlsym`/`dlclose` + a non-Windows watcher backend.
- **Hot-reload is single-config Ninja-only.** `$<TARGET_FILE_DIR:FateEngine>` resolves cleanly; multi-config (Visual Studio generator) needs an adjustment.
- **Hot-reload play-mode swap disabled by default + UI mutator removed.** Toggle requires recompiling with `FATE_HOTRELOAD_EXPERIMENTAL_PLAYMODE=1`. Combat / AOI / network packet dispatch are not yet proven quiesced at the safe-frame point during play.
- **Metal shader still loads from disk** under `FATE_USE_VFS=ON` — `gfx::Device::createShaderFromFiles` needs a from-memory entry point before the VFS flag flips on Apple platforms.
- **Cooldown rendering is binary** post-loadout-refactor (full ring while cooling, off when 0) for both Skill and Item paths. The previous proportional radial fill via per-frame `cooldownTotal` push was removed when `SkillArc` was refactored to a thin renderer. Threading `cooldownTotalsMs_` through `SkillLoadout` restores it.
- **One pre-existing test failure** — `tests/test_palette.cpp` is order-dependent (palette test passes in isolation, full-suite ordering can flake). Independent of any active arc.

---

## 🌱 From Engine Demo to Full Game

The open-source repo builds and runs as an editor/engine demo. The new public **`engine/gameplay2d/`** layer (20 components + 11 systems) covers the foundation surface area for most 2D MMORPG / action-RPG starts — collision, controllers, animators, attacks, faction-filtered damage, NPCs, portals, spawn zones, mob brains, click targeting, nameplates, replication-shaped data. Build your game on top of it by creating the following directories (which the CMake system auto-detects):

**Game Logic (`game/`):**
- `game/components/` — Game-specific ECS components that go *beyond* the public gameplay2d shapes (custom equipment, factions, pets, **interact sites**, **CharacterFlags**, etc.)
- `game/systems/` — Game systems that operate on components (combat, AI/mob behavior, skill execution, spawning, loot, party, nameplates, **interact-site triggers**, **GuardSystem**, etc.)
- `game/shared/` — Data structures shared between client and server (item definitions, faction data, skill tables, mob stats, **dialogue trees**, **quest definitions**, **enchant ladder**, **skill loadout**, **boss-script types**, **patrol authoring**)
- `game/data/` — Static game data catalogs (paper doll definitions, skill trees, enchant tables, premium-currency shop catalog)
- `game/runtime/` — **Reloadable behavior DLL sources.** Pure C ABI through `engine/module/fate_module_abi.h`. Drop new behaviors under `game/runtime/behaviors/`; CONFIGURE_DEPENDS glob auto-picks them up
- An entry point (`game/main.cpp` or similar) with a class inheriting from `fate::App`

**Server (`server/`):**
- Request handlers for every game action (auth, movement, combat, trade, inventory, chat, party, guild, arena, dungeons, **interact sites**, **bounty board**, **enchant + protected-stone craft**, **loadout sweep**, **shop sell from bag**)
- Database repositories (PostgreSQL via libpqxx) including **session repository + listener** for cross-process kicks
- Boss scripts under `server/bosses/` registered into `BossScriptRegistry`
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

> 📘 **The [Demo Manuals](Documents/Demo/README.md) walk through the engine surface area** — Quick Start, Editor User Guide, Asset Pipeline, Architecture Manual, Networking Protocol, API Reference, step-by-step Tutorials (start with [My First MMORPG Map](Documents/Demo/tutorials/first-map.md) and [Local Client + Server](Documents/Demo/tutorials/local-client-server.md)), Troubleshooting, and the Publishing Guide. Start there before adding your `game/` sources.

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

# Shipping build (strips editor + ImGui + hot-reload, release optimizations, TLS cert pinning enforced):
cmake --preset x64-Shipping
cmake --build --preset x64-Shipping

# VFS-enabled build (PhysicsFS-backed asset reads, optional .pak overlay):
cmake -B build -DFATE_USE_VFS=ON
cmake --build build

# Hot-reload module (rebuild this whenever you change game/runtime/*.cpp):
cmake --build build --target FateGameRuntime

# Shipping CI sanity (wraps VsDevCmd.bat; fails fast on any imgui/editor leak):
pwsh scripts/check_shipping.ps1                        # defaults: x64-Shipping + FateEngine
pwsh scripts/check_shipping.ps1 -Preset x64-Release    # dev-release verify
pwsh scripts/check_shipping.ps1 -Target FateServer     # server-only rebuild
```

> 💡 **Shipping guard rule:** every `#include "imgui.h"` / `ImGui::*` / `ImVec*` / editor-only `Editor::instance()` member is wrapped in `#ifndef FATE_SHIPPING` (ImGui) or `#ifdef EDITOR_BUILD` (editor-only members). Hot-reload is forced OFF in shipping. Headers never include ImGui directly — UI-hover queries route through `UIManager::pressedNode()` instead.

### Output Targets

| Target | Description | Availability |
|--------|-------------|--------------|
| **fate_engine** | Core engine static library (incl. public `gameplay2d/` layer) | Always (open-source) |
| **FateDemo** | Playable arena demo — editor UI + Play/Observe/Scene-dropdown + Hot-Reload panel + gameplay2d wired | Open-source build |
| **FateGameRuntime** | Reloadable behavior DLL (pure C ABI to host) | Open-source build (when `game/runtime/` present) |
| **FateEngine** | Full game client | When `game/` sources present |
| **FateServer** | Headless authoritative server | When `server/` + PostgreSQL present |
| **fate_tests** | 2,505 unit tests (doctest) | When `tests/` sources present |

### Platform Matrix

| Platform | Status | Details |
|----------|--------|---------|
| **Windows** | Primary | MSVC (VS 2026 / VS 18), primary development target. Hot-reload supported |
| **macOS** | Supported | CMake, full Metal rendering, minicoro fibers, ProMotion 120fps. Hot-reload pending `dlopen` backend |
| **iOS** | Pipeline Ready | CMake Xcode generator, Metal/GLES 3.0, CAMetalLayer, TestFlight script, DNS64/NAT64 |
| **Android** | Pipeline Ready | Gradle + NDK r27, SDLActivity, `./gradlew installDebug` |
| **Linux** | CI Verified | GCC 14, Clang 18 — builds green on every push |

---

## 🧪 Testing

The engine maintains exceptional stability through **2,505 passing test cases** across **259 test files**, powered by `doctest`.

```bash
# Run all tests:
./build/Debug/fate_tests.exe

# Target specific suites:
./build/Debug/fate_tests.exe -tc="Gameplay2D*"
./build/Debug/fate_tests.exe -tc="*Trigger2DSystem*"
./build/Debug/fate_tests.exe -tc="HotReload*"
./build/Debug/fate_tests.exe -tc="BossScript*"
./build/Debug/fate_tests.exe -tc="GuardPatrol*"
./build/Debug/fate_tests.exe -tc="GuardSystem*"
./build/Debug/fate_tests.exe -tc="Death Lifecycle"
./build/Debug/fate_tests.exe -tc="PacketCrypto"
./build/Debug/fate_tests.exe -tc="PersistenceQueue*"
./build/Debug/fate_tests.exe -tc="SkillLoadout*"
./build/Debug/fate_tests.exe -tc="SkillBarPolymorphic*"
./build/Debug/fate_tests.exe -tc="EnchantSystem*"
./build/Debug/fate_tests.exe -tc="CollisionGrid*"
./build/Debug/fate_tests.exe -tc="AsepriteImporter*"
./build/Debug/fate_tests.exe -tc="InteractSite*"
./build/Debug/fate_tests.exe -tc="SessionRepository*"
./build/Debug/fate_tests.exe -tc="AtomicWrite*"
./build/Debug/fate_tests.exe -tc="LayoutClass*"
```

Coverage spans: **public gameplay2d layer (collider shape-aware overlap, mob aggro revalidation, trigger overlap, spawn-zone respawn, sprite-animator JSON round-trip, target-picker priority/distance tiebreaker, SDF-text readiness gate)**, **hot-reload ABI + lifecycle**, **boss-script foundation (selectors / lifecycle / damage gates / queue contracts / transient sweep)**, **patrol validator + system runtime + dt-based stepping**, combat formulas, encryption/decryption, entity replication, inventory operations, skill systems, **polymorphic skill-bar persistence**, **stale-binding sweep semantics**, **enchant ladder + protected-stone craft**, quest progression, economic nonces, arena matchmaking, dungeon lifecycle, VFX pipeline, compressed textures, UI layout (incl. **layout-class variants**), collision grids, async asset loading, Aseprite import pipeline, costume system, collection system, pet system, pet auto-loot, **interact-site validator + packets + dialogue conditions**, **session repository + listener cross-process kick**, **atomic-write durability**, **persistence contract** (live-DB column round-trip), and more.

---

## 📐 Architecture at a Glance

```
engine/                    # 110,000+ LOC — Core engine (438 files)
 gameplay2d/               # 🎮 NEW PUBLIC LAYER — 20 demo-safe components + 11 systems + register entry point
                           #   Collider2D, CharacterController2D, SpriteAnimator2D, CameraFollow2D, Health/Damageable/Attack,
                           #   Targetable, Nameplate, Interactable/NPC2D, Zone2D/Portal2D, SpawnZone2D, Mob2D,
                           #   NetworkIdentity/ReplicatedTransform2D/InterestSource (replication-shaped data, no protocol)
 render/                   #   Sprite batching, SDF text (4 fonts via FontRegistry), lighting, bloom, paper doll, VFX, Metal RHI
 net/                      #   Custom UDP (v26), Noise_NK crypto, AuthProof, replication, AOI, interpolation
 ecs/                      #   Archetype ECS (4096 reserve), 56+ components, reflection, serialization
 ui/                       #   65+ widgets (incl. SkillLoadoutStrip w/ drag-bind pipeline, ConfirmDialog chrome, FriendsPanel, BountyPanel), JSON screens, themes, viewport scaling, panel + bespoke widget chrome, layout-class variants
 editor/                   #   ImGui editor, undo/redo, Aseprite animation editor, asset browser, Play/Observe/Scene toolbar, Hot-Reload panel, BossScript + Guard inspectors, MobAI live-tuning inspector
 module/                   #   Hot-reload manager, BehaviorComponent + BehaviorRegistry, FateBehaviorVTable C ABI, shadow-copy LoadLibrary swap
 tilemap/                  #   Chunk VBOs, texture arrays, Blob-47 autotile, 4-layer
 scene/                    #   Async loading, versioning, prefab variants
 asset/                    #   IAssetSource (DirectFs / Vfs), hot-reload, fiber async, LRU VRAM cache, compressed textures
 input/                    #   Action map, touch controls, 6-frame combat buffer
 audio/                    #   SoLoud, 3-bus, spatial audio, 10 game events
 job/                      #   Fiber system, MPMC queue, scratch arenas, FATE_JOB_WORKERS env override
 memory/                   #   Zone/frame/scratch arenas, pool allocators
 spatial/                  #   Fixed grid, spatial hash, collision bitgrid
 core/                     #   Structured errors, Result<T>, CircuitBreaker, atomic_write, logger
 particle/                 #   CPU emitters, per-particle lifetime/color lerp
 platform/                 #   Device info, RAM tiers, thermal polling
 profiling/                #   Tracy integration, spdlog, rotating file sink (text + JSONL)
 vfx/                      #   SkillVFX player, JSON definitions, 4-phase compositing
 vfs/                      #   PhysicsFS, ZIP mount, overlay priority
 telemetry/                #   Metric collection, JSON flush, HTTPS stub
 game/                     #   SkillLoadout model (canonical UI-facing facade for the polymorphic 20-slot bar)

examples/                  # demo_app.cpp — wires the gameplay2d layer into a playable arena (player + walls + NPC + mob + portal + spawn zone)

game/                      # 50,600+ LOC — Game logic layer (162 files)
 runtime/                  #   FateGameRuntime DLL — reloadable behaviors via FATE_AUTO_BEHAVIOR(NAME, vtable)
 shared/                   #   Pure gameplay across many files (zero engine deps)
   combat_system           #   Hit rate, armor, crits, class advantage, PvP balance, scene-cache PvP gate
   skill_manager           #   Skills, polymorphic loadout, cooldowns, cast times, resource types
   mob_ai                  #   Cardinal movement, threat (8-slot AggroLedger), leash, L-shaped chase
   status_effects          #   DoTs, buffs, shields, source-tagged removal
   inventory               #   16 slots, equipment, nested bags, stacking, stable UUID instances, per-item max_stack
   trade_manager           #   2-step security, slot locking, atomic transfer
   arena_manager           #   1v1/2v2/3v3, matchmaking, AFK detection, honor
   gauntlet                #   Event scheduler, divisions, wave spawning, MVP
   faction_system          #   Factions, guards, innkeeper, faction-aware targeting
   pet_system              #   Active pet equip, XP share, stat bonuses, auto-loot
   opals_system            #   Premium currency, shop catalog, direct-credit drops, AdMob rewards
   enchant_system          #   7-tier ladder, protected stones as physical items, drag-driven flow
   quest_manager           #   Dialogue trees, interact sites, honor + currency on turn-in
   dialogue_registry       #   npcs/ + quests/ + sites/ scan, condition evaluator
   boss_script_types       #   8 selectors, vtable, deferred-everything queues, lethal-ordering
   guard_patrol_apply      #   Patrol authoring validator (BFS connectivity, hard-leash auto-extend, edit-time mirror)
   ...                     #   +25 more systems (guild, party, crafting, collections, market, merchant pass, etc.)
 components/               #   ECS component wrappers + InteractSiteComponent + CharacterFlagsComponent + QuestGiverComponent + GuardComponent + BossScriptComponent + TransientMobComponent
 systems/                  #   ECS systems (combat, render, movement, mob AI, pet follow, spawn, interact site, guard, ...)
 data/                     #   Paper doll catalog, NPC definitions, quest data, premium-currency shop catalog

server/                    # 46,900+ LOC — Headless authoritative server (163 files)
 handlers/                 #   58 packet handler files (incl. shop_handler w/ processShopSellFromBag, loadout_sweep, interact_site, crafting w/ enchant + protect-craft, telegraph + admin grant + MobAI live-tune + trade-state pipelines)
 db/                       #   17 repository files, fiber DbDispatcher (8192 backlog cap, JSONB inventory batch), session_repo + listener
 cache/                    #   12 startup caches (item/loot/recipe/pet/costume/collection/guard (w/ default_patrol)/scene/spawn-zone/...)
 auth/                     #   TLS auth server (bcrypt + dummy-hash timing-oracle defense, starter equipment, login rate limiting, CSPRNG tokens)
 bosses/                   #   BossScriptRegistry, lifecycle hooks, spawn seam, per-script registration umbrella
 *.h/.cpp                  #   ServerApp, SpawnCoordinator, DungeonManager, RateLimiter, GM commands, MetricsCollector, ShutdownManager

tests/                     # 55,500+ LOC — 2,505 passing test cases across 259 files
assets/shaders/            #    ~800 LOC — GLSL + Metal MSL (sprite, post-process, SDF text, lighting)

ads_ssv_server/            # Standalone rewarded-video verifier (ECDSA-signed HTTPS callbacks)
Docs/migrations/           # 126 numbered SQL migrations (schema + content seeds)
Documents/Demo/            # 📘 Demo manuals — start here when building your own game (Quick Start, Editor Guide, Asset Pipeline, Architecture, Networking, API Reference, Tutorials, Troubleshooting, Publishing)
Docs/Guides/               # Subsystem deep-dives (animation, UI, audio, skill VFX)
assets/dialogue/           # 183 dialogue trees: 124 quests + 33 NPCs + 26 sites
scripts/                   # check_shipping.ps1 (CI), run_server.ps1 (launcher)
```

---

## 🤝 Contributing

Contributions to the engine core are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on submitting issues, pull requests, and the `FATE_HAS_GAME` guard requirements for engine code. New to the codebase? Start with the [**Demo Manuals**](Documents/Demo/README.md).

## 📜 License

Apache License 2.0 — see [LICENSE](LICENSE) for details.

---

<div align="center">

<br>

<img src="https://img.shields.io/badge/%E2%9A%94%EF%B8%8F_Forged_by-Caleb_Kious-8B5CF6?style=for-the-badge&logoColor=white" alt="Forged by Caleb Kious" />

<br>

<sub>**Engine Architecture** | **Networking & Crypto** | **Server Systems** | **Editor Tooling** | **Cross-Platform** | **All Game Logic** | **Solo Developer**</sub>

<br><br>

<img src="https://img.shields.io/badge/C%2B%2B-263%2C000%2B_lines-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++ LOC" />
<img src="https://img.shields.io/badge/files-1%2C022%2B-2ea44f?style=flat-square" alt="Files" />
<img src="https://img.shields.io/badge/tests-2%2C505_passing-2ea44f?style=flat-square" alt="Tests" />
<img src="https://img.shields.io/badge/protocol-v26-blueviolet?style=flat-square" alt="Protocol" />
<img src="https://img.shields.io/badge/🎮_gameplay2d-20%2B11-00BCD4?style=flat-square" alt="Gameplay2D" />
<img src="https://img.shields.io/badge/⚡_hot--reload-FateGameRuntime-FF5722?style=flat-square" alt="Hot Reload" />
<img src="https://img.shields.io/badge/crypto-Noise__NK%20%2B%20AuthProof%20%2B%20DB__sessions-critical?style=flat-square" alt="Crypto" />
<img src="https://img.shields.io/badge/platforms-5-orange?style=flat-square" alt="Platforms" />
<img src="https://img.shields.io/badge/game_systems-50%2B-blueviolet?style=flat-square" alt="Game Systems" />
<img src="https://img.shields.io/badge/UI_widgets-65%2B-ff69b4?style=flat-square" alt="UI Widgets" />
<img src="https://img.shields.io/badge/chrome_widgets-29-7E57C2?style=flat-square" alt="Chrome Widgets" />
<img src="https://img.shields.io/badge/admin_commands-44-4CAF50?style=flat-square" alt="GM Commands" />
<img src="https://img.shields.io/badge/handlers-58-9cf?style=flat-square" alt="Handler Files" />
<img src="https://img.shields.io/badge/DB_repos-17-informational?style=flat-square" alt="DB Repos" />
<img src="https://img.shields.io/badge/migrations-126-FF7043?style=flat-square" alt="Migrations" />
<img src="https://img.shields.io/badge/dialogue_trees-183-9C27B0?style=flat-square" alt="Dialogue Trees" />
<img src="https://img.shields.io/badge/loadout_slots-20-00BCD4?style=flat-square" alt="Loadout Slots" />
<img src="https://img.shields.io/badge/enchant_tiers-7-FFC107?style=flat-square" alt="Enchant Tiers" />

<br><br>

[![GitHub](https://img.shields.io/badge/GitHub-wFate-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/wFate)

<br>

</div>
