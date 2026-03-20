# FateEngine - State & Features

## Engine Overview

Custom 2D game engine built in C++ for FateMMO. Designed for mobile-first landscape gameplay with a built-in Unity-style editor for scene building, tile painting, and rapid iteration. All game systems from the Unity/C# prototype have been ported to C++ as server-authoritative logic.

**Tech Stack:** C++20, SDL2, OpenGL 3.3 Core, Dear ImGui (docking), ImGuizmo, ImPlot, Tracy Profiler, nlohmann/json, stb_image, stb_truetype, Winsock2

**Build System:** CMake with FetchContent (auto-downloads all dependencies)

**Target:** Windows (development), iOS/Android (future), Linux server (future)

**Codebase:** ~37,600 lines across 225 files (engine/ 17,500 LOC, game/ 20,100 LOC)

### Build & Run

**Visual Studio (recommended):**
- Open the project folder in Visual Studio (CMake project)
- Build: `Ctrl+Shift+B`
- Run: `F5` (debug) or `Ctrl+F5` (no debugger)
- Output: `out/build/x64-Debug/FateEngine.exe`
- Log file: `out/build/x64-Debug/fate_engine.log`

**Command Line (alternative):**
- Old build dir: `build/Debug/` — may be stale if VS is used
- VS build dir: `out/build/x64-Debug/` — always current when building from VS
- Always run from the VS output directory when using Visual Studio

---

## Current Features

### Core Engine
| Feature | Status | Notes |
|---------|--------|-------|
| SDL2 Window | Done | 1280x720 default, resizable |
| OpenGL 3.3 Rendering | Done | Custom function loader, no GLAD dependency |
| Batched Sprite Renderer | Done | Sorts by depth + texture, 10k capacity, dirty-flag sort skip (hash-based) |
| 2D Orthographic Camera | Done | 480x270 virtual resolution (pixel art scale), 0.05x-8x zoom |
| Archetype ECS | Done | Contiguous SoA component storage, O(matching) forEach queries, generational handles, command buffer for deferred structural changes |
| Action Map Input | Done | 23 logical ActionIds, hardcoded WASD+arrow bindings, Gameplay/Chat context switching, 6-frame input buffer for combat skill queuing |
| Structured Logging | Done | Timestamped, categorized, console + file output |
| SDF Text Rendering | Done | MTSDF uber-shader (normal/outlined/glow/shadow styles), runtime atlas generation from TTF, UTF-8 decode, resolution-independent at any zoom |
| Tilemap System | Done | Tiled JSON loader, frustum-culled, collision layers |
| Coordinate System | Done | Tile-based coords (32px grid), pixel-to-tile conversion |
| Spatial Grid (Primary) | Done | Fixed power-of-two grid, bitshift cell lookup (zero hash computation), std::span query results, std::expected error handling |
| Spatial Hash (Fallback) | Done | Mueller-style 128px cells, counting-sort rebuild, for unbounded/sparse regions |
| Memory: Zone Arena | Done | 256 MB virtual reserve per scene, O(1) bulk reset on zone unload |
| Memory: Frame Arena | Done | Double-buffered 64 MB, per-frame temporaries, swap at frame start |
| Memory: Scratch Arenas | Done | Thread-local (2 per thread, 256 MB), Fleury conflict-avoidance, ScratchScope RAII |
| Memory: Pool Allocator | Done | Free-list on arena backing, O(1) alloc/dealloc, debug occupancy bitmap |
| Asset Hot-Reload | Done | Generational asset handles (20+12 bit), AssetRegistry with type-erased loaders, Windows file watcher (ReadDirectoryChangesW), 300ms debounced reload for textures/JSON/shaders |
| Allocator Visualization | Done | ImGui/ImPlot memory panel: arena watermark bars (color-coded), pool heat maps (per-block grid), frame arena timeline (300-sample ring buffer with high-water mark). Guarded by ENGINE_MEMORY_DEBUG |
| Zone Snapshots | Done | Persistent entity IDs (64-bit), serialization skeleton for mob/boss state across zone visits |
| Tracy Profiler | Done | On-demand profiling, named zones, frame marks, arena memory tracking |
| Component Registry | Done | Compile-time CompId, Hot/Warm/Cold tier classification, zero-RTTI macros |
| Reflection System | Done | FATE_REFLECT macro, field-level metadata (name, offset, type), auto-generated JSON serializers |
| Serialization Registry | Done | ComponentMetaRegistry maps string names → type-erased serialize/deserialize, aliases for backward compat. Enum/EntityHandle/Direction fields fully serialized (size-aware memcpy for enums) |
| Component Traits | Done | ComponentFlags (Serializable/Networked/Persistent/EditorOnly), per-component policy via component_traits<T> |
| Generic Inspector | Done | Reflection-driven ImGui widget fallback for any reflected component, manual inspectors take priority |
| Chunk Lifecycle | Done | 7-state machine (Queued→Loading→Setup→Active→Sleeping→Unloading→Evicted), ticket system, rate-limited transitions, double-buffered staging |
| AOI Groundwork | Done | Visibility sets with enter/leave/stay diffs, hysteresis (20% larger deactivation radius) |
| Ghost Entity Scaffold | Done | GhostFlag component, dedicated GhostArena for cross-zone proxy entities |
| Render Graph | Done | 10-pass pipeline: GroundTiles→Entities→Particles→SDFText→DebugOverlays→Lighting→BloomExtract→BloomBlur→PostProcess→Blit |
| Particle System | Done | CPU emitters, spawn rate/burst, gravity, per-particle lifetime/rotation/color lerp |
| 2D Lighting | Done | Ambient + point lights, light map FBO, additive accumulation, multiplicative composite |
| Post-Processing | Done | Bloom (extract + Gaussian blur + composite), vignette, color grading |
| Fiber Job System | Done | Win32 fibers, 4 workers, 32-fiber pool, lock-free MPMC queue, counter-based suspend/resume, fiber-local scratch arenas |
| Graphics RHI | Done | gfx::Device + CommandList + Pipeline State Objects, GL backend, typed 32-bit handles, uniform cache |
| Networking Transport | Done | Custom reliable UDP (Winsock2), ByteWriter/ByteReader, 16-byte packet header, 3 channels (unreliable/reliable-ordered/reliable-unordered), ack bitfields, RTT estimation |
| Entity Replication | Done | AOI-driven enter/leave/update, delta compression with field bitmasks, ghost entities, client-side position interpolation, spatial-indexed visibility |
| Client-Server Architecture | Done | Headless 20 tick/sec server (FateServer), NetClient/NetServer, session tokens, heartbeat/timeout |

### Editor (Dear ImGui)
| Feature | Status | Notes |
|---------|--------|-------|
| Toggle with F3 | Done | Auto-pauses game when opened |
| Entity Hierarchy | Done | Grouped by name+tag (collapsible), color-coded (player/ground/obstacle/mob/boss), error badges |
| Inspector Panel | Done | Edit all engine + game component properties live, sprite preview thumbnail, reflection-driven generic fallback |
| Project Browser | Done | Tabs: Sprites, Scripts, Scenes, Shaders, Prefabs |
| Right-Click Context Menus | Done | Open in VS Code, Show in Explorer, Copy Path, Delete |
| Asset Placement | Done | Click sprite thumbnail, click scene to stamp entities |
| Tile Palette | Done | Collapsible panel, load tilesets (recursive subdirectory scan), scrollable tile grid, paint/drag to place |
| Scene Interaction | Done | Click to select (depth-priority, closest-center), drag to move, sticky selection |
| Grid Overlay | Done | Tile-edge aligned (tiles sit inside grid cells), toggleable |
| Grid Snapping | Done | Ground tiles snap to grid, other entities move freely |
| Camera Pan | Done | Right-click drag to pan scene |
| Camera Zoom | Done | Mouse scroll wheel, 0.05x to 8x range |
| Play/Pause | Done | Toolbar button, auto-pause on editor open |
| Create/Delete Entities | Done | Menu + Delete key, works while paused |
| Duplicate Entity | Done | Full deep copy via JSON serialization, offset by 32px |
| Add Components | Done | Popup with engine, game systems, social, NPC, and player quest/bank sections |
| Collision Debug | Done | F2 toggle, green=static, yellow=dynamic, cyan=polygon |
| Scene Save | Done | File > Save Scene with custom name, saves to source + build dirs |
| Scene Load | Done | File > Load Scene lists all .json scenes, click to load |
| New Scene | Done | File > New Scene clears all entities |
| Save as Prefab | Done | Entity menu, modal dialog with name input |
| Remove Components | Done | Right-click any component header to remove it (all 24+ component types) |
| Resize Handles | Done | 8 drag handles (4 corners + 4 edges), E key for resize tool mode |
| Source Rect Editor | Done | UV region editing in Sprite inspector for tileset splicing |
| Undo/Redo | Done | Ctrl+Z/Ctrl+Y, 200 action history, tracks move/resize/delete/duplicate/tile paint |
| Tool Modes | Done | W=Move, E=Resize, B=Paint, X=Erase. Active tool highlighted in toolbar |
| Keyboard Shortcuts | Done | Ctrl+Z undo, Ctrl+Y redo, Ctrl+S save, Ctrl+D duplicate, Ctrl+A select all, Delete |
| Eraser Tool | Done | X key, click/drag to delete ground tiles with undo support |
| Layer Visibility | Done | Gnd/Obj toggles in toolbar to show/hide entity layers |
| Log Viewer | Done | In-editor log panel with level filters (DBG/INF/WRN/ERR), text search, color-coded |
| Memory Panel | Done | View > Memory: 3 tabs (Arena watermarks, Pool heat maps, Frame Timeline chart). ENGINE_MEMORY_DEBUG guarded |
| Command Console | Done | Type commands: help, list, count, find, delete, spawn, tp. Results in log viewer |
| Error Badges | Done | Red [!] in hierarchy for entities with missing textures |
| Panel Persistence | Done | ImGui saves window layout to imgui.ini, panels don't steal focus |
| Rotate Tool | Done | R key, ImGuizmo visual handles, undo support |
| Post-Process Panel | Done | Live tweaking bloom/vignette/color grading |
| Network Panel | Done | Connect/disconnect to server, host/port config, shows client ID and ghost count, docked in bottom panel |
| ImGuizmo | Done | Visual translate/scale/rotate handles on selected entities |

### Game UI (ImGui-based, in-game panels)
| Feature | Status | Notes |
|---------|--------|-------|
| Inventory Panel | Done | I key toggle, 15-slot grid with drag-and-drop, 10 equipment slots |
| Item Tooltips | Done | Hover shows name, rarity, rolled stats, enchant, socket, soulbound |
| Equipment Drag/Drop | Done | Drag between inventory and equipment slots, double-click to equip/unequip |
| Rarity Colors | Done | White/green/blue/purple/orange borders on item slots |
| Gold Display | Done | Formatted with K/M/B suffixes |
| Stats Tab | Done | HP/MP/XP bars, primary stats, derived stats, fury, honor, PvP |
| Skills Tab | Done | Learned skills list, drag-to-assign to skill bar, activate rank with skill points |
| Community Tab | Stub | Placeholder |
| Settings Tab | Stub | Placeholder |
| Skill Bar UI | Done | 5 slots x 4 pages (20 total) on right side, K toggle, [/] page switch, drag-to-assign, cooldown overlay, right-click clear |
| HUD Bars | Done | HP (green, top-left) / MP (blue, top-right) / XP (gold, bottom-center), positions adjustable in F3 editor HUD Layout panel |
| Debug Info Panel | Done | FPS, pos, entities, player stats — shown only in F3 editor (moved out of F1 HUD) |
| NPC Dialogue UI | Done | Click NPC to open, greeting + role buttons, quest accept/decline/complete, branching story dialogue. Wired to open Shop/Trainer/Bank/Teleporter UIs via callbacks |
| Shop UI | Done | Merchant buy/sell grid, gold display, buy checks player gold. Opens from NPC dialogue |
| Quest Log UI | Done | L key toggle, active quests with objective progress, abandon button, completed section |
| Skill Trainer UI | Done | Lists learnable skills, level/gold/SP requirements, greyed out if not met. Opens from NPC dialogue |
| Bank Storage UI | Done | Deposit/withdraw items and gold, fee display. Opens from NPC dialogue |
| Teleporter UI | Done | Destination list with costs and level requirements. Opens from NPC dialogue, triggers zone transition |
| Death Overlay UI | Done | "You have died" panel with 3 respawn options (Town/Map Spawn/Phoenix Down), countdown timer, XP/Honor loss display |
| Chat UI | Done | Chat button (top-right, unread indicator), panel locked to bottom 25% of viewport. 7-tab filter (All/Map/Global/Trade/Party/Guild/Private). Party/Guild blocked if not in one. Private requires `/username message`. System messages show on all tabs. Per-channel colors. All positioned via GameViewport |
| Login Screen | Done | Username/password login, registration with character name, class selection (Warrior/Mage/Archer), faction selection (Xyros/Fenor/Zethos/Solis), server host/port config |
| D-Pad | Planned | Mobile touch control, bottom-left |
| Action Buttons | Planned | Attack + skill circular buttons, bottom-right |

### Zone/Portal System
| Feature | Status | Notes |
|---------|--------|-------|
| ZoneComponent | Done | Named region with size, level range, PvP flag, zone type (town/zone/dungeon) |
| PortalComponent | Done | Trigger area, target scene/zone/spawn pos, fade transition |
| ZoneSystem | Done | Detects player in zones, triggers portal transitions, fade overlay |
| Zone Debug Rendering | Done | Blue outlines for zones, yellow for portals (F2 debug) |
| Same-Scene Portals | Done | Teleport + fade within one scene (e.g., Lighthouse floors) |
| Cross-Scene Portals | Done | targetScene field for map-to-map transitions |
| Editor Integration | Done | Zone + Portal in Add Component, full inspector editing, scene save/load |

### Components (Engine)
| Component | Status | Notes |
|-----------|--------|-------|
| Transform | Done | Position (px), scale, rotation, depth; tile coord display |
| SpriteComponent | Done | Texture (via AssetHandle), sourceRect (tileset support), spritesheet frames, tint, flip |
| Animator | Done | State machine, frame-based animation |
| PlayerController | Done | Cardinal movement, speed, facing, isLocalPlayer flag |
| BoxCollider | Done | AABB with offset, trigger/static flags, "Fit to Sprite" button |
| PolygonCollider | Done | SAT collision, vertex editing, make box/circle presets (auto-sized to sprite) |

### Components (Game — attached to player/mob entities)
| Component | Status | Notes |
|-----------|--------|-------|
| CharacterStatsComponent | Done | Wraps CharacterStats (HP/MP/XP/level/stats/fury/death/respawn) |
| CombatControllerComponent | Done | Target tracking, auto-attack state, attack cooldown |
| DamageableComponent | Done | Marker for entities that can receive damage |
| InventoryComponent | Done | Wraps Inventory (15 slots, equipment, gold) |
| SkillManagerComponent | Done | Wraps SkillManager (learning, cooldowns, 4x5 bar) |
| StatusEffectComponent | Done | Wraps StatusEffectManager (buffs, debuffs, DoTs, shields) |
| CrowdControlComponent | Done | Wraps CrowdControlSystem (stun/freeze/root/taunt) |
| TargetingComponent | Done | Selected target ID, target type, max range, click consumed flag |
| ChatComponent | Done | Wraps ChatManager (7-channel chat) |
| GuildComponent | Done | Wraps GuildManager (ranks, symbols, XP) |
| PartyComponent | Done | Wraps PartyManager (3-player, invites, loot mode) |
| FriendsComponent | Done | Wraps FriendsManager (50 friends, 100 blocks) |
| MarketComponent | Done | Wraps MarketManager (listings, jackpot) |
| TradeComponent | Done | Wraps TradeManager (two-step security) |
| NameplateComponent | Done | Display name, level, PK color, guild info, role subtitle for NPCs |
| EnemyStatsComponent | Done | Wraps EnemyStats (mob HP, threat table, scaling) |
| MobAIComponent | Done | Wraps MobAI (TWOM cardinal AI, L-shaped chase) |
| MobNameplateComponent | Done | Mob display name, level, boss/elite flags |
| NPCComponent | Done | NPC identity, greeting, interaction radius, face direction |
| QuestGiverComponent | Done | List of quest IDs this NPC offers |
| QuestMarkerComponent | Done | `?`/`!` marker state and tier for quest givers |
| ShopComponent | Done | Shop name and item inventory with buy/sell prices |
| SkillTrainerComponent | Done | Trainer class and learnable skill list |
| BankerComponent | Done | Storage slots and deposit fee config |
| GuildNPCComponent | Done | Guild creation cost and level requirement |
| TeleporterComponent | Done | Destination list with costs and level gates |
| StoryNPCComponent | Done | Branching dialogue tree with action/condition system |
| QuestComponent | Done | Wraps QuestManager (quest progress, active/completed tracking) |
| BankStorageComponent | Done | Wraps BankStorage (persistent bank item/gold storage) |
| FactionComponent | Done | Player faction (Xyros/Fenor/Zethos/Solis), permanent, set at creation |
| PetComponent | Done | Equipped pet instance, auto-loot radius, hasPet() check |
| SpawnPointComponent | Done | Player respawn marker (isTownSpawn flag), placed in scenes via editor |

### Systems
| System | Status | Notes |
|--------|--------|-------|
| MovementSystem | Done | WASD input (local player only), Box+Polygon collision (all combos) |
| AnimationSystem | Done | Timer-based frame updates |
| CameraFollowSystem | Done | Locked to local player, smooth (no pixel-snap jitter) |
| SpriteRenderSystem | Done | Frustum culled, depth sorted, respects sprite enabled flag |
| GameplaySystem | Done | Ticks StatusEffects, CrowdControl, HP/MP regen, PK decay, death visual (rotation + gray tint), respawn countdown, nameplates |
| MobAISystem | Done | Ticks MobAI for all mobs, scans for players, applies movement, fires attacks |
| CombatActionSystem | Done | TWOM Option B targeting, viewport-aware click/touch-to-target, auto-clear off-screen, player attacks, damage text, mob death/XP, same-faction PvP block, home village PK exception check, mob HP bars always visible |
| SpawnSystem | Done | Region-based mob spawning, death detection, respawn timers, zone containment, deferred entity creation (safe during archetype iteration) |
| NPCInteractionSystem | Done | Click-to-interact with NPCs, viewport-aware screen-to-world, range check, dialogue open/close, click consumption (prevents combat targeting) |
| QuestSystem | Done | Routes mob kills/item pickups/NPC talks to quest progress, event-driven quest marker updates on all NPCs |
| SpawnSystem | Done | Region-based mob spawning, death detection, respawn timers, zone containment |
| ZoneSystem | Done | Zone transitions, portal detection, fade effects |
| ParticleSystem | Done | CPU particle emitters, registered as ECS system |

### Entity Factory
| Feature | Status | Notes |
|---------|--------|-------|
| createPlayer() | Done | Assembles player with all 24 components (includes FactionComponent, PetComponent), faction selected at character creation (Xyros/Fenor/Zethos/Solis) |
| createMob() | Done | Assembles mob with EnemyStats, MobAI, StatusEffects, nameplate, procedural pixel art sprites (Slime/Goblin/Wolf/Mushroom/Forest Golem), trigger colliders |
| createNPC() | Done | Assembles NPC from NPCTemplate with composable role components (quest/shop/trainer/bank/guild/teleporter/story) |
| Class Configuration | Done | Warrior/Mage/Archer stats from CLAUDE.md class table (HP, STR, per-level gains) |
| Test Mob Spawning | Done | 5 test mobs: Slime, Goblin, Wolf, Mushroom (passive), Forest Golem (boss) |

### Prefab System
| Feature | Status | Notes |
|---------|--------|-------|
| Save Entity as Prefab | Done | JSON to assets/prefabs/, saves to both source + build dirs |
| Spawn from Prefab | Done | Click in Prefabs tab, click scene to place. Registry-based deserialization (no hardcoded types) |
| Prefab Library | Done | Auto-loads all .json from prefab directory on startup. Unknown components preserved as raw JSON |
| Editor Integration | Done | Prefabs tab, right-click to place/delete, tooltip shows components |
| Scene Versioning | Done | `"version": 1` header in scene files, forward-compat validation |
| Registry-Based Save/Load | Done | Scene and prefab serialization driven by ComponentMetaRegistry — adding new components requires zero file edits |
| Duplicate Entity | Done | Uses prefab serialization for deep copy |

### Tile Painting
| Feature | Status | Notes |
|---------|--------|-------|
| Tileset Loading | Done | Dropdown lists PNGs from assets/tiles/ (recursive subdirectory scan), auto-detects grid |
| Tile Selection | Done | Click tile in palette grid, highlights selected |
| Paint Mode | Done | Click or drag in scene to stamp tiles, auto-snaps to grid |
| Layer-Aware Painting | Done | Same-tileset overwrites existing tile; different-tileset creates overlay at higher depth (preserves ground under transparent tiles) |
| Undo Support | Done | Ctrl+Z undoes tile paint (both new tiles and tile updates) |
| Tileset Persistence | Done | sourceRect saved/loaded in scene JSON, tiles round-trip correctly |
| Bundled Tilesets | Done | Hyptosis 32x32 (CC-BY 3.0), LPC terrain/atlas (CC-BY-SA 3.0), Town tileset (CC-BY-SA 4.0), procedural tileset (generated at startup) |

---

## Game Systems (Ported from Unity Prototype)

All 27 game systems from the C#/Unity prototype have been converted to C++ and live in `game/shared/`. Total: **45 files, ~7,800 lines**, all compile with zero errors (197 test cases, 1777 assertions). Database repositories exist for all systems in `server/db/` (13 repos). All major systems are DB wired with message handlers, load-on-connect, save-on-disconnect, periodic maintenance, and async auto-save. Combat formula matches Unity prototype exactly (off-by-one fixed). Inventory saves after market/trade mutations prevent item duplication. Gauntlet has 3 divisions with real mob data.

### Core Gameplay (Fully Ported — Logic Identical to C#)
| System | Files | Lines | C# Source | Notes |
|--------|-------|-------|-----------|-------|
| Game Types & Enums | `game_types.h` | 372 | ItemEnums, ClassDefinition, constants | All enums, ClassDefinition struct, rarity/mob colors, all constants |
| Character Stats | `character_stats.h/.cpp` | 463 | NetworkCharacterStats (3,284L) | HP/MP/XP/level, stat calc with VIT multiplier, damage formulas, death/respawn, fury/mana |
| Enemy Stats | `enemy_stats.h/.cpp` | 268 | NetworkEnemyStats (1,036L) | Mob HP, threat table (damage attribution), scaling, death events |
| Combat System | `combat_system.h/.cpp` | 373 | CombatHitRateConfig + System (961L) | Hit rate with coverage, spell resist, block, armor reduction, PvP, class advantage |
| Mob AI | `mob_ai.h/.cpp` | 572 | ServerZoneMobAI (1,349L) | TWOM cardinal-only movement, L-shaped chase, axis locking, wiggle unstuck, roam/idle phases |
| Status Effects | `status_effects.h/.cpp` | 436 | StatusEffectManager (741L) | DoTs (bleed/burn/poison), buffs, shields, invuln, transform, bewitch, stacking |
| Crowd Control | `crowd_control.h/.cpp` | 218 | CrowdControlSystem (433L) | Stun/freeze/root/taunt with priority hierarchy, immunity checks |
| XP Calculator | `xp_calculator.h` | 49 | XPCalculator (158L) | Gray-through-red level scaling, 0%-130% XP multipliers |
| Honor System | `honor_system.h/.cpp` | 145 | HonorSystem (329L) | PvP honor gain/loss tables, 5-kills/hour tracking per player pair |
| Enchantment | `enchant_system.h` | 224 | EnchantmentSystem (601L) | +1 to +12 with success rates, protection scrolls, secret bonuses, stone tiers |
| Item Instance | `item_instance.h` | 121 | ItemInstance (403L) | Item data with rolled stats, sockets, enchant, soulbound |
| Item Stat Roller | `item_stat_roller.h/.cpp` | 340 | ItemStatRoller (399L) | Weighted stat rolling, exponential decay distribution, JSON serialization |
| Socket System | `socket_system.h` | ~170 | SocketSystem (351L) | Accessory socketing (Ring/Necklace/Cloak), weighted probability rolls (+1: 25%…+10: 0.5%), stat scroll validation, server-authoritative trySocket with re-socket support |

### NPC & Quest System (New — TWOM-Inspired)
| System | Files | Notes |
|--------|-------|-------|
| Quest Manager | `quest_manager.h/.cpp` | Quest progress tracking, accept/abandon/turn-in, 5 objective types (Kill/Collect/Deliver/TalkTo/PvP), max 10 active quests, prerequisite chains, client-side state sync (markCompleted/setProgress from server) |
| Quest Data | `quest_data.h` | Hardcoded quest registry with 6 starter quests, 4 TWOM tiers (Starter/Novice/Apprentice/Adept) |
| NPC Types | `npc_types.h` | NPCTemplate, ShopItem, TrainableSkill, TeleportDestination structs |
| Dialogue Tree | `dialogue_tree.h` | Branching dialogue with enum-based actions (GiveItem/GiveXP/GiveGold/SetFlag/Heal) and conditions (HasFlag/MinLevel/HasItem/HasClass) |
| Bank Storage | `bank_storage.h` | Persistent bank storage with item slots, gold deposit/withdraw, configurable fee |
| Serialization | `register_components.h` | Custom toJson/fromJson for all complex NPC/Quest components (shops, skills, dialogue trees, quest progress, bank storage). NPC entities fully persist across scene save/load and prefab round-trips |

See `Docs/QUEST_AND_NPC_GUIDE.md` for full guide on creating quests and NPCs.

### Game Systems (Ported — Needs Database Wiring)
| System | Files | Lines | C# Source | Notes |
|--------|-------|-------|-----------|-------|
| Inventory | `inventory.h/.cpp` | 410 | NetworkInventory | 15 slots, equipment map, gold, trade slot locking, stack/swap **(DB wired)** |
| Skill Manager | `skill_manager.h/.cpp` | 340 | PlayerSkillManager (2,079L) | Skill learning (skillbook + points), cooldowns, 4x5 skill bar **(DB wired — load/save on connect/disconnect)** |
| Party Manager | `party_manager.h/.cpp` | 410 | NetworkPartyManager | 3-player parties, +10%/member XP bonus, loot mode, invites **(runtime only — no persistence needed)** |
| Guild Manager | `guild_manager.h/.cpp` | 280 | NetworkGuildManager | TWOM guilds, ranks, 16x16 pixel symbols, XP contribution **(DB wired — load on connect)** |
| Friends Manager | `friends_manager.h/.cpp` | 377 | NetworkFriendsManager | 50 friends, 100 blocks, profile inspection, online status **(DB wired — init + last_online on connect/disconnect)** |
| Chat Manager | `chat_manager.h/.cpp` | 120 | NetworkChatManager | 7 channels (Map/Global/Trade/Party/Guild/Private/System), cross-faction garbling on public channels **(runtime only — no persistence needed)** |
| Trade Manager | `trade_manager.h/.cpp` | 354 | NetworkTradeManager | Two-step security (Lock->Confirm->Execute), 8 item slots + gold **(DB wired — full session flow: initiate/addItem/lock/confirm/execute/cancel, atomic item+gold transfer)** |
| Market Manager | `market_manager.h/.cpp` | 233 | NetworkMarketManager + MarketStructs | Marketplace with jackpot, merchant pass, tax system **(DB wired — list/buy/cancel, 2% tax to jackpot, offline seller credit, expiry maintenance)** |
| Gauntlet | `gauntlet.h/.cpp` | ~850 | GauntletManager + GauntletInstance + GauntletTeam + GauntletRegistry + GauntletConfig (10 files) | Full event scheduler (2hr cycle, 10min signup), division-based matchmaking, GauntletTeam per-team scoring/MVP, GauntletRegistry signup queues, BasicWaveConfig/BossSpawnConfig/LevelMobMapping for wave spawning, reward configs (winner/loser/performance), consolation for overflow, announcement callbacks, debug commands **(DB wired — config loaded at startup, ticked every frame, CmdGauntlet register/unregister/status, mob kill notifications routed)** |
| Faction System | `faction.h` | ~130 | FactionRegistry + FactionChatGarbler | 4 factions (Xyros/Fenor/Zethos/Solis), registry, deterministic chat garbling, same-faction checks, faction picker on registration screen |
| Pet System | `pet_system.h/.cpp` | ~120 | PetDefinition + PetInstance + PetSystem | Leveling, rarity-tiered stats (HP/Crit/XP bonus), XP sharing (50%), player-level cap |
| Stat Enchant System | `stat_enchant_system.h` | ~70 | StatEnchantSystem | Accessory enchanting (Belt/Ring/Necklace/Cloak), 6-tier roll table, HP/MP x10 scaling |
| Bounty System | `bounty_system.h` | ~200 | NetworkBountyManager + BountyService + BountyRepository | PvE bounty board (max 10 active, 50K-500M gold, 48hr expiry), 2% tax, guild-mate protection, 12hr guild-leave cooldown, party split on claim, cancel/refund, expiration processing **(DB wired — place/cancel handlers + expiry maintenance)** |
| Ranking System | `ranking_system.h` | ~170 | NetworkRankingManager + RankingRepository | Global/class/guild/honor leaderboards, paginated (50/page), 60s cache, PlayerRankInfo (global+class+guild rank), K/D ratio, honor rankings **(repo built, needs integration)** |
| Profanity Filter | `profanity_filter.h` | ~260 | ProfanityFilter (351L) | Leetspeak normalization (8 mappings), 50+ word list (EN+ES), 4 blocked phrases, 3 modes (Validate/Censor/Remove), character/guild name validation, chat filtering, word-boundary logic for short words |
| Input Validator | `input_validator.h` | ~75 | InputValidator (76L) | Chat/Name validation modes, per-character rejection, username (3-20 alphanumeric+underscore) and password (8-128) validation, delegates to ProfanityFilter |
| Consumable Definition | `consumable_definition.h` | ~105 | ConsumableDefinition (127L) | 16 effect types (HP/MP restore, 8 buff types, teleport, skill book, stat reset), cooldown groups, safe-zone/combat restrictions, effects description builder |
| Bag Definition | `bag_definition.h` | ~30 | BagDefinition (23L) | Inventory expansion bags (1-20 slots per bag), rarity, validation |

### Key Formulas Preserved (Exact Match to C# Prototype)
```
// XP to next level
xpRequired = max(100, round(0.35 * level^5.1))

// HP with vitality multiplier
baseHP = round(baseMaxHP + hpPerLevel * (level - 1))
maxHP = round(baseHP * (1.0 + bonusVitality * 0.01)) + equipBonusHP

// Armor mitigation
reduction% = min(75, armor * 0.5)
finalDamage = max(1, round(rawDamage * (1 - reduction/100)))

// Damage multiplier (class-specific primary stat)
Warrior: 1.0 + bonusSTR * 0.02
Mage:    1.0 + bonusINT * 0.02
Archer:  1.0 + bonusDEX * 0.02

// Hit rate (coverage system)
coverage = hitRate / 2.0  (levels covered)
Within coverage: 90%, 85%, 80%, 75%, 70%... (-5% per level)
Beyond coverage: 50%, 30%, 15%, 5%, 0% (steep dropoff)

// Crit rate
critRate = 0.05 + (isArcher ? bonusDEX * 0.005 : 0) + equipCritRate

// Fury generation
normalHit: +0.5 fury, critHit: +1.0 fury
maxFury = 3 + floor(level / 10)

// Enchant weapon damage
multiplier = 1 + (enchantLevel * 0.125)
+11 secret: *1.05, +12 secret: *1.10 (stacks)
+12 max damage bonus: +30%

// Enchant success rates
+1 to +8: 100%, +9: 40%, +10: 15%, +11: 10%, +12: 5%

// Socket value probabilities (weighted roll 1-10)
+1: 25%, +2: 20%, +3: 17%, +4: 13%, +5: 10%
+6: 7%, +7: 4%, +8: 2.5%, +9: 1%, +10: 0.5%

// PvP damage multiplier
pvpDamage = baseDamage * 0.05
```

---

## Changelog

### March 19, 2026 - Death/Respawn System, NPC UI Wiring, Faction Selection

**Death & Respawn (client-side):**
- Death/respawn protocol messages (`SvDeathNotifyMsg`, `CmdRespawnMsg`, `SvRespawnMsg`) with packet types 0xA0, 0xA1, 0x1B
- `SpawnPointComponent` for player respawn locations (distinct from BossSpawnPointComponent)
- `DeathOverlayUI` — centered panel with "You have died" text, XP/Honor loss display, 5-second countdown, 3 respawn options (Town, Map Spawn, Phoenix Down with inventory check)
- Death visual: sprite rotation (-PI/2 lay-down), gray tint (0.3 alpha), animation stop
- Movement blocked when dead, skill bar grayed out, combat already blocked
- `NetClient` wiring: `sendRespawn()`, `onDeathNotify`/`onRespawn` callbacks
- `respawn()` now restores both HP and MP to full
- GameplaySystem no longer auto-respawns — player-initiated via DeathOverlayUI
- Server-side death triggering is a separate in-progress task

**NPC Interaction UI Wiring:**
- NPCDialogueUI now has callbacks: `onOpenShop`, `onOpenSkillTrainer`, `onOpenBank`, `onOpenGuildCreation`, `onTeleport`
- All 5 TODO stubs in dialogue replaced with callback invocations
- ShopUI, SkillTrainerUI, BankStorageUI, TeleporterUI added as GameApp members with render calls
- Clicking NPC dialogue buttons opens the correct sub-UI and closes the dialogue
- Guild creation checks level requirement and gold cost
- Teleporter validates gold client-side and sends zone transition request to server

**Faction Selection:**
- Color-coded faction radio buttons on registration screen (Xyros red, Fenor blue, Zethos green, Solis gold)
- `pendingFaction_` member on GameApp, stored on register submit
- All 3 hardcoded `Faction::Xyros` in `createPlayer` calls replaced with selected faction

**Quest Manager:**
- Added `markCompleted(questId)` and `setProgress(questId, currentCount, targetCount)` for client-side state sync from server messages

**Server-Side Death/Respawn:**
- `CmdRespawn` handler validates timer, Phoenix Down, determines position from DB scene cache
- Town respawn (type 0) sends `SvZoneTransitionMsg` to load Town scene (not just same-scene teleport)
- Map spawn (type 1) teleports to `default_spawn_x`/`default_spawn_y` from `scenes` table
- Phoenix Down (type 2) respawns at death position, consumes item server-side
- Reconnect while dead: server sends `SvDeathNotifyMsg` with timer=0 on reconnect if `is_dead` in DB
- DB migration 004: `default_spawn_x`/`default_spawn_y` columns on `scenes` table (auto-migrated on server start)

**Server Bug Fixes:**
- Duplicate login crash: `activeAccountSessions_.erase(existing)` used invalidated iterator after `onClientDisconnected()` — undefined behavior causing server exit. Fixed by erasing before disconnect callback.
- Rubber-banding after respawn/zone transition: `lastValidPositions_` now updated on any teleport
- Zone transitions now send DB-configured spawn positions instead of hardcoded (0,0)
- Triple Disconnect send: client sends 3x unreliable Disconnect before socket close

**Quick Fixes:**
- Floating damage text converted to ImGui ForegroundDrawList (was SDF/SpriteBatch, invisible behind editor)
- Loot pickup wired to InventoryUI (onLootPickup → addItem/addGold + chat notification)
- Quest updates wired to QuestLogUI (onQuestUpdate → QuestComponent state sync)
- `npc_default.png` placeholder sprite created (32x48, silences startup errors)

### March 18, 2026 - Loot Drop, Ground Items, Boss Spawning, Starter Equipment

**Server-authoritative loot pipeline ported from C# prototype + starter gear on registration:**

- **Item Definition Cache** (`server/cache/item_definition_cache.h/.cpp`): Loads all 748 item definitions from `item_definitions` table at server startup. CachedItemDefinition struct with type helpers (isWeapon, isArmor, isAccessory), attribute accessors (getIntAttribute/getFloatAttribute/getStringAttribute from JSONB `attributes` column), possible stat parsing from JSONB `possible_stats` column. Handles both DB formats: `{"stat":"hp","weighted":true}` and legacy `{"name":"int","weight":1.0}`.
- **Loot Table Cache** (`server/cache/loot_table_cache.h/.cpp`): Loads 72 loot tables (835 drop entries) from `loot_drops` table, groups by `loot_table_id`. `rollLoot()` rolls each entry against `drop_chance`, generates `ItemInstance` with rolled stats (via ItemStatRoller using item's `possible_stats`), weighted enchant levels (+0=40%...+7=0.5%), and socket rolls for accessories. Enchantable subtypes: Sword, Wand, Bow, Shield, Head, Armor, Gloves, Boots, Feet.
- **Dropped Item Component** (`game/components/dropped_item_component.h`): Ground loot entity data — itemId, quantity, enchantLevel, rolledStatsJson, rarity, isGold/goldAmount, ownerEntityId (top damager), 2-minute despawn timer.
- **Entity Type 3 Protocol**: Extended `SvEntityEnterMsg` with conditional item fields when `entityType == 3`. New `SvLootPickupMsg` (packet 0x98) notifies client on pickup. Backward compatible — existing entity types unchanged.
- **Replication**: Dropped items replicated via AOI as entity type 3. `buildEnterMessage()` populates item fields from `DroppedItemComponent`.
- **Server Loot Pipeline** (`server_app.cpp`): On mob kill — `takeDamageFrom()` tracks per-attacker damage, top damager gets loot ownership. `LootTableCache::rollLoot()` generates drops, spawned as ground entities with grid offset + jitter. Gold rolls separately. All registered with `ReplicationManager`.
- **Pickup System**: `CmdAction(actionType=3)` targets a dropped item's PersistentId. Server validates proximity (48px), loot ownership, adds item/gold to inventory, sends `SvLootPickupMsg`, destroys entity.
- **Despawn**: Server tick iterates all `DroppedItemComponent` entities, destroys any past `despawnAfter` (120s default).
- **Boss Spawn Points** (`game/components/boss_spawn_point_component.h`): Fixed-position boss spawning from designer-specified coordinate lists. Respawns at different coordinate after death. 0.25s tick interval.
- **Boss Death Persistence** (`server/db/zone_mob_state_repository.h/.cpp`): Saves boss death state to `zone_mob_deaths` table. Restores respawn timers on server restart. Auto-cleanup of expired records.
- **Starter Equipment**: New characters receive class-specific weapon (Rusty Dagger / Gnarled Stick / Makeshift Bow) plus shared armor (Quilted Vest, Worn Sandals, Tattered Gloves) — inserted as equipped items in `character_inventory` during registration.
- **Client Handling**: Ghost dropped item entities created via `EntityFactory::createGhostDroppedItem()`. `onLootPickup` callback logs pickup notifications.

### March 18, 2026 - GameViewport: Universal UI Positioning Fix

**Single source of truth for viewport-relative UI positioning across all aspect ratios:**

- **GameViewport** (`game/ui/game_viewport.h`): New static class providing the active game render rect. Set once per frame by GameApp from `Editor::viewportPos()/viewportSize()`. All UI systems query it instead of `ImGui::GetIO().DisplaySize`. Handles letterboxing/pillarboxing automatically — UI always stays within the visible game area regardless of device aspect ratio.
- **Fixed 10 UI systems**: Inventory, Skill Bar, Bank Storage, NPC Dialogue, Shop, Quest Log, Skill Trainer, Teleporter, Chat UI, and Nameplates all now use `GameViewport::centerX()/centerY()` for centering and `GameViewport::right()/bottom()` for edge anchoring. Previously used `io.DisplaySize` which broke when editor forced a device resolution.
- **Nameplate/Combat Text Fix** (`game/systems/combat_action_system.h`): `worldToScreen()` now projects to `GameViewport::width()/height()` (screen display size) instead of FBO dimensions. Offset uses `GameViewport::x()/y()` instead of the old `vpOffset_`. Nameplates, quest markers, and mob names stay correctly positioned above entities in all aspect ratios.
- **Chat UI Polish**: Removed System tab (system messages still show on all tabs), added party/guild send blocking, private chat requires `/username message`, "Chat" button with unread indicator, panel locked to bottom 25% of viewport, input hint text.
- **Transport Dedup Fix** (`engine/net/reliability.h/.cpp`): `ReliabilityLayer::onReceive()` now returns `bool` — `true` for new packets, `false` for duplicates. Both `NetClient` and `NetServer` skip processing duplicate reliable packets. Fixes 4x message duplication caused by retransmit without dedup. All reliable packet types affected (chat, combat events, player state, entity updates).
- **Auth Protocol Fix** (`engine/net/auth_protocol.h`): Fixed `writeF32`/`readF32` → `writeFloat`/`readFloat` (introduced by zone transition subagent).

### March 18, 2026 - Client Side: Chat UI, Message Handlers, DB Mob Spawning, Scene Transitions

**Four client-side systems bringing all server features to the player:**

- **Chat UI** (`game/ui/chat_ui.h/.cpp`): In-game chat panel positioned bottom-left (TWOM style). 7-tab channel filter (All/Map/Global/Trade/Party/Guild/System), 200-message scrolling buffer with per-channel color coding, text input with Enter-to-send, Enter-on-empty to hide. Integrates with Input::setChatMode() to suppress gameplay actions while typing. Wired to `NetClient::onChatMessage` — all server system messages (trade invites, market results, bounty updates, gauntlet announcements, guild/social/quest notifications) now display in the chat panel.
- **Client Message Handlers** (`engine/net/net_client.h/.cpp`, `game/game_app.cpp`): 7 new `std::function` callbacks added to NetClient (onTradeUpdate, onMarketResult, onBountyUpdate, onGauntletUpdate, onGuildUpdate, onSocialUpdate, onQuestUpdate) + 7 new case blocks in handlePacket for deserialization. All registered in GameApp::onInit() routing to ChatUI as system messages with appropriate sender tags.
- **DB-Driven Mob Spawning** (`game/entity_factory.h`, `game/systems/spawn_system.h`, `game/shared/cached_mob_def.h`): Extracted `CachedMobDef` struct to standalone header (no pqxx dependency) so both server and game code can use it. New `EntityFactory::createMobFromDef()` uses ALL fields from the DB definition (scaled HP/damage/armor, crit, speed, AI ranges, loot table, gold drops, honor, monster type). SpawnSystem accepts a `mobDefLookup_` function — server wires it to `MobDefCache::get()`, client leaves null (uses fallback). 73 mob definitions now drive spawning with real stats.
- **Scene Loading from DB** (`engine/net/game_messages.h`, `server/server_app.cpp`, `game/game_app.cpp`): New `CmdZoneTransition` (0x1A) client-to-server message + `SvZoneTransitionMsg` response. Portal collisions now send transition request to server instead of loading locally. Server validates level requirement via `SceneCache::getByName()`, sends system chat rejection or `SvZoneTransition` with spawn coords. Client receives transition, loads scene via SceneManager, repositions player. Server async-saves player state on transition. `SceneCache::getByName()` added for name-based lookups.

### March 18, 2026 - Server Hardening: Combat Fix, Crash Safety, Async Saves, Gauntlet Data

**Five targeted fixes for correctness, safety, and performance:**

- **Combat Damage Off-By-One Fix** (`game/shared/combat_system.cpp`): Changed `<` to `<=` and removed `+1` in `calculateDamageMultiplier`. Engine was applying damage reduction one level earlier than Unity prototype. At levelDiff=2 with `damageReductionStartsAt=2`: was 0.88 (12% penalty), now 1.0 (no penalty). Boundary test added.
- **Inventory Save After Mutations** (`server/server_app.cpp`): New `saveInventoryForClient()` helper called after Market ListItem, Market BuyItem, and Trade Confirm. Prevents item duplication if server crashes between a trade/market operation and the next auto-save. Trade saves both players' inventories.
- **Trade Target Notification**: When a player initiates a trade, the target now receives a `[Trade]` system chat message and a `SvTradeUpdate` invite packet with the sender's name and session ID.
- **Async Auto-Save** (`server/server_app.cpp`): New `savePlayerToDBAsync()` snapshots all component data by value on the game thread, then dispatches DB writes to a fiber worker via `DbDispatcher`. `tickAutoSave` now uses this async path. Disconnect saves remain synchronous (must complete before entity destruction). Game loop no longer blocks on periodic DB round-trips.
- **Gauntlet Seed Data** (`Docs/migrations/003_gauntlet_seed_data.sql`): 3 divisions (Novice 1-20, Veteran 21-40, Champion 41-70) with real mob IDs from the database. 15 waves using forest/coastal/desert/swamp/sky/void creatures. 7 boss encounters (Tidal Serpent, Pharaoh's Shadow, Fate Maker). Winner/loser/performance rewards with gold, honor, and gauntlet tokens.

### March 18, 2026 - Market & Trade Full Implementation

**Server-authoritative market listing/buying and peer-to-peer trading:**

- **Market ListItem**: Full validation (price range, max 7 listings, item exists by instanceId, not soulbound, not trade-locked). Looks up item definition for metadata. Serializes rolled stats + socket. Creates listing in DB, removes from inventory, sends response.
- **Market BuyItem**: Loads listing, validates active + not own + buyer has gold + inventory space. Atomic transaction via pool: deactivate listing, calculate 2% tax, deduct buyer gold, add item to buyer, credit seller gold (works offline), add tax to jackpot. Logs in market_transactions.
- **Market CancelListing**: Deactivates listing, returns item (already implemented).
- **Trade Full Session**: Initiate (create session, validate not already trading), AddItem (validate instanceId + not soulbound, unlock both sides), RemoveItem, SetGold (validate balance), Lock/Unlock, Confirm (both-locked gate, then atomic execution: transfer all items via UPDATE character_id, transfer gold both ways, complete session, log history — all in single pqxx::work transaction via pool), Cancel (delete offers, mark cancelled).
- **Inventory.findByInstanceId()**: New method for O(n) lookup by UUID — needed by market and trade to locate specific item instances.

### March 18, 2026 - Gauntlet Server Integration

**GauntletManager fully wired into ServerApp:**

- **Init**: `initGauntlet()` loads all division configs, wave configs, rewards, and performance rewards from `gauntlet_config`, `gauntlet_waves`, `gauntlet_rewards`, `gauntlet_performance_rewards` tables. Parses boss vs basic waves, builds `GauntletDivisionSettings` with level-mob mappings.
- **Tick**: `gauntletManager_.tick(gameTime_)` called every server tick — drives 2-hour event cycle, 10-minute signup windows, announcement callbacks at 5m/2m/1m/30s/10s thresholds.
- **Callbacks**: `onAnnouncement` broadcasts to all clients via system chat. `onDivisionStarted` and `onDivisionComplete` log match results and broadcast winner announcements. `onConsolationAwarded` logs honor/token grants for overflow players.
- **CmdGauntlet Handler**: Register (auto-detects division from player level, saves return position/scene), Unregister, GetStatus (shows signup state or time until next event). All send `SvGauntletUpdateMsg` responses.
- **Mob Kill Hook**: When a player kills a mob, checks if they're in an active gauntlet instance and routes the kill to `gauntletManager_.notifyMobKill()` with boss detection via `monsterType`.
- **Graceful fallback**: If gauntlet tables are empty, logs a warning and continues — the event cycle still runs but won't create instances until config is populated.

### March 18, 2026 - Quest/Bank/Pet Repos, Message Handlers, Protocol Expansion

**3 new repos, full message protocol, gameplay command handlers:**

- **Quest Repository** (`server/db/quest_repository.h/.cpp`): Load/save active quest progress + completed quest IDs. Upsert by character_id + quest_id. Batch save in transaction. Restores into QuestManager via `setSerializedState()`.
- **Bank Repository** (`server/db/bank_repository.h/.cpp`): Load/save bank items (character_bank) and gold (character_bank_gold). Deposit/withdraw items and gold. Delete+re-insert pattern for full save.
- **Pet Repository** (`server/db/pet_repository.h/.cpp`): Load all pets, load equipped pet, save pet state (upsert by id), equip/unequip (unequip-all-then-equip pattern), add XP.
- **DB Migration 002** (`Docs/migrations/002_bank_and_pets.sql`): 3 new tables (character_bank, character_bank_gold, character_pets) + 2 indexes. Applied to fate_engine_dev.
- **Protocol Expansion** (`engine/net/packet.h`): 5 new client commands (CmdMarket 0x15, CmdBounty 0x16, CmdGauntlet 0x17, CmdGuild 0x18, CmdSocial 0x19) + 7 server responses (SvTradeUpdate 0x99 through SvQuestUpdate 0x9F).
- **Game Messages** (`engine/net/game_messages.h`): Sub-action enums for all command types (MarketAction::ListItem/BuyItem/CancelListing, BountyAction::PlaceBounty/CancelBounty, GuildAction::Create/Invite/Leave/Kick/Promote/Disband, SocialAction::SendFriendRequest/AcceptFriend/BlockPlayer, QuestAction::Accept/Abandon/TurnIn, etc.) + server response message structs with write/read serialization.
- **Message Handlers** (`server_app.cpp`): Full `onPacketReceived` handlers for Market (list validation + cancel), Bounty (place with guild check + cancel with refund), Guild (create with gold deduction + leave), Social (friend request/accept/decline/remove, block/unblock with blocked-check), Quest (accept/abandon/turnIn with DB persistence). All handlers send typed response messages to the client.
- **Connect/Disconnect**: Quest progress (active + completed), bank gold, and equipped pet now loaded on connect and saved on disconnect.

### March 18, 2026 - Full DB Wiring: Repositories, Async Dispatch, Server Integration

**6 new repositories, fiber-based async dispatcher, full ServerApp integration:**

- **Skill Repository** (`server/db/skill_repository.h/.cpp`): Load/save learned skills (character_skills), skill bar (character_skill_bar, 20 slots), skill points (character_skill_points). Upsert pattern (ON CONFLICT DO UPDATE). Batch save in single transaction.
- **Guild Repository** (`server/db/guild_repository.h/.cpp`): Full guild lifecycle — create (name check, 100K gold), disband (soft delete, guild_left_at), add/remove members (capacity check, count update), set rank, transfer ownership (demote+promote+update in transaction), guild XP contribution. 20+ SQL operations across guilds, guild_members, guild_invites, characters tables.
- **Social Repository** (`server/db/social_repository.h/.cpp`): Bidirectional friend system (send/accept/decline/remove), block system (removes friendship first), friend notes, online status tracking. Operates on friends, blocked_players, characters tables.
- **Market Repository** (`server/db/market_repository.h/.cpp`): Create/cancel/deactivate listings, transaction logging, player listing queries, jackpot pool management (add/reset/get state), expired listing deactivation. 2-day listing expiry. Operates on market_listings, market_transactions, jackpot_pool tables.
- **Trade Repository** (`server/db/trade_repository.h/.cpp`): Full P2P trade — session create/load/cancel, per-player lock/confirm/gold, item offer add/remove/clear, atomic item transfer (UPDATE character_id), gold transfer, trade history logging, stale session cleanup. 22 methods across trade_sessions, trade_offers, trade_invites, trade_history, character_inventory, characters tables.
- **Bounty Repository** (`server/db/bounty_repository.h/.cpp`): Place bounty (upsert existing or create new, record contribution), cancel contribution (refund with 2% tax, deactivate if below min), claim bounty (deactivate + calculate payout split), process expired bounties (refund all contributors with tax), guild leave cooldown check. Full bounty_history audit trail. FOR UPDATE row locking on mutations.
- **Async DB Dispatcher** (`server/db/db_dispatcher.h`): Bridges fiber job system to connection pool. `dispatch<Result>(workFn, completionFn)` runs DB queries on worker fibers, queues results for game thread. `drainCompletions()` called once per tick. Fire-and-forget variant via `dispatchVoid()`. RAII task cleanup. 4 concurrent DB operations via fiber workers.
- **ServerApp Init**: Creates all 9 repositories + initializes pool (5-50 connections) + dispatcher. Loads 5 definition caches at startup (748 items, 835 loot drops, 73 mobs, 60 skills/174 ranks, 3 scenes). Logs cache summary.
- **Player Connect**: Now loads skills (learned skills, 20-slot skill bar, skill points), guild membership + rank, initializes friends manager, updates last_online. Staggered auto-save timer set per client (offset by clientId mod 60).
- **Player Disconnect**: Now saves all skills + skill bar + skill points, updates last_online alongside existing character + inventory save.
- **Auto-Save**: Every 5 minutes per player (staggered to spread DB load). Full character save including skills.
- **Periodic Maintenance**: Market listing expiry check (60s interval), bounty expiry + refund processing (60s), stale trade session cleanup (30s, >30min sessions cancelled).
- **Shutdown**: Saves all connected players, shuts down connection pool, disconnects.

### March 18, 2026 - Database Layer: Connection Pool, Definition Caches, Data Migration

**Connection pool, startup caches, and game data migration from Unity DB:**

- **Connection Pool** (`server/db/db_pool.h/.cpp`): Thread-safe pqxx connection pool. Min 5, max 50 connections. RAII `Guard` class for automatic release. Eager creation of min connections at startup, overflow safety valve, dead connection detection on release, `testConnection()` health check.
- **Mob Definition Cache** (`server/db/definition_caches.h/.cpp`): Loads 73 mobs from `mob_definitions` at startup. Full stat loading (HP, damage, armor, crit, speed, scaling per level, aggro/attack/leash ranges, spawn weights, loot table IDs, gold drops, honor rewards).
- **Skill Definition Cache** (`server/db/definition_caches.h/.cpp`): Loads 60 skills + 174 ranks from `skill_definitions` and `skill_ranks`. Lookup by ID, by class, rank data with all passive/active/transform/resurrect fields.
- **Scene Cache** (`server/db/definition_caches.h/.cpp`): Loads 3 scenes from `scenes` table. PvP status queries.
- **Data Migration**: Migrated game data from `fate_mmo` (Unity prod DB) to `fate_engine_dev` via CSV export/import. FK dependency order: loot_tables before loot_drops, item_definitions before loot_drops, skill_definitions before skill_ranks. See `Docs/DATABASE_REFERENCE.md` for full schema reference.
- **Database Reference Doc** (`Docs/DATABASE_REFERENCE.md`): Complete reference for all 65 tables with column names/types, FK relationships, migration history, C++ query patterns, and connection details.

**Data loaded into fate_engine_dev:** 748 items, 73 mobs, 60 skills, 174 skill ranks, 72 loot tables, 835 loot drops, 3 scenes.

### March 18, 2026 - Bounty, Ranking, Profanity, Consumables, Bags, Input Validation

**Ported 6 remaining missing systems from Unity prototype:**

- **Bounty System** (`bounty_system.h`): Full PvE bounty board ported from `NetworkBountyManager.cs` + `BountyService.cs` + `BountyRepository.cs`. Constants: 50K min, 500M max, 10 board slots, 48hr expiry, 2% tax, 12hr guild-leave cooldown. Validation (self-bounty, guild-mate, board capacity), payout calculation with party split, cancel/refund with tax, expiration tick, human-readable result messages. DB callbacks for persistence layer.
- **Ranking System** (`ranking_system.h`): Leaderboards ported from `NetworkRankingManager.cs` + `RankingRepository.cs`. 6 categories (Global/Warrior/Mage/Archer/Guild/Honor), paginated at 50/page, 60s cache. Data structs: `PlayerRankingEntry`, `GuildRankingEntry`, `HonorRankingEntry` (with K/D ratio), `PlayerRankInfo` (global+class+guild rank).
- **Profanity Filter** (`profanity_filter.h`): Full port from `ProfanityFilter.cs`. 3 modes (Validate/Censor/Remove), leetspeak normalization (8 char mappings), 50+ word list (English + Spanish), 4 blocked phrases, 11 blocked characters. Character name validation (1-16 chars, starts with letter), guild name validation (1-20 chars), chat message filtering (max 200 chars), per-character input validation. Word-boundary logic for short words (<=3 chars).
- **Input Validator** (`input_validator.h`): Port from `InputValidator.cs`. Chat/Name validation modes, per-character rejection, username validation (3-20 chars, alphanumeric + underscore), password validation (8-128 chars). Delegates to ProfanityFilter for name/chat validation.
- **Consumable Definition** (`consumable_definition.h`): Port from `ConsumableDefinition.cs`. 16 effect types: RestoreHealth/Mana/Both, 8 buff types (STR/INT/DEX/VIT/ATK/DEF/Speed/EXP), Teleport, RevealMap, SkillBook, StatReset. Cooldown groups, safe-zone-only/out-of-combat-only/while-moving flags, effects description builder.
- **Bag Definition** (`bag_definition.h`): Port from `BagDefinition.cs`. Inventory expansion bags with 1-20 slot range, rarity, validation.

### March 18, 2026 - Socket System Port & Gauntlet Expansion

**Ported socket rolling/validation from Unity prototype, expanded Gauntlet to full event system:**

- **Socket System** (`socket_system.h`): New header-only system ported from Unity's `SocketSystem.cs`. Weighted probability socket rolling (1-10, cumulative thresholds out of 1000), equipment slot validation (Ring/Necklace/Cloak), stat type validation (STR/DEX/INT only), stat scroll ID mapping, `trySocket()` server operation with re-socket tracking, `clearSocket()`. Thread-safe RNG via `thread_local std::mt19937`.
- **Gauntlet Team** (`gauntlet.h/.cpp`): New `GauntletTeam` class tracking per-team members, scoring (mob kills = level pts, PvP kills = victim_level x 2), aggregate stats, MVP/top-killer/top-mob-killer queries.
- **Gauntlet Registry** (`gauntlet.h/.cpp`): New `GauntletRegistry` class managing signup queues per division. Open/close/cancel signup windows, player registration/unregistration with fast character-to-division lookup, queue size queries, time-remaining calculation.
- **Gauntlet Manager** (`gauntlet.h/.cpp`): New `GauntletManager` class — full event scheduler. 2-hour event cycle, 10-minute signup windows, announcement callbacks at 5m/2m/1m/30s/10s thresholds, division settings and reward config storage, instance creation per division, player overflow consolation (5 honor + 1 token), per-player netId-to-division tracking, debug GM commands for forcing signup open/close.
- **Gauntlet Config Structs**: Added `GauntletRewardType` enum, `GauntletRewardConfig`, `GauntletPerformanceRewardConfig`, `BasicWaveConfig` (per-wave spawn intervals/limits for waves 1-4), `BossSpawnConfig` (boss/miniboss entries for final wave), `LevelMobMapping`, `GauntletDivisionSettings` (extended config with mob level calculation, wave configs, tiebreaker settings, spawn points), `GauntletInstanceInfo` (client sync data).
- **Gauntlet Bug Fix**: `completeMatch()` now captures `wentToTiebreaker` before overwriting state, and calculates `durationSeconds` from match/wave timestamps.

### March 18, 2026 - Audit Fixes, Art Pipeline, Editor Polish

**Performance & correctness fixes from code audit, improved procedural art, editor tile painting overhaul:**

- **Replication Spatial Indexing**: Replaced O(n×m) brute-force visibility in `ReplicationManager::buildVisibility()` with `SpatialHashEngine` queries. Spatial index rebuilt once per tick (O(n)), then each client queries nearby cells only. Hysteresis (activation/deactivation radius) preserved.
- **HonorSystem Thread Safety**: Added `std::mutex` to protect static `s_killTracking` map. Public methods lock then delegate to unlocked internal helpers; `processKill()` holds one lock for the full operation.
- **Component Meta Serialization**: Filled in `autoToJson`/`autoFromJson` for `Enum` (size-aware memcpy), `EntityHandle` (uint32 value), `Direction` (uint8 cast). `Custom` fields now emit `LOG_WARN` instead of silent skip.
- **Click-to-Target Coordinate Fix**: `CombatActionSystem` and `NPCInteractionSystem` now use editor viewport position/size for screen-to-world conversion instead of full window DisplaySize. Fixes targeting in editor mode.
- **Mob HP Bars Always Visible**: Changed condition from `currentHP < maxHP` to `isAlive` so mob health bars render at full HP.
- **Mob Collision**: Mob colliders set to `isTrigger = true` — mobs no longer physically block player movement.
- **Sprite Enabled Check**: `SpriteRenderSystem` now skips sprites with `enabled = false` (dead mob visibility).
- **SpawnSystem Iterator Safety**: Refactored `SpawnSystem::update()` to collect zone handles first, then process outside `forEach` — fixes crash when entity creation during archetype iteration caused vector resize.
- **Tile Painting Undo**: `paintTileAt()` now records `CreateCommand` (new tiles) and `PropertyCommand` (tile updates) for full Ctrl+Z support.
- **Tile Painting Transparency**: Layer-aware painting — different tilesets create overlay entities at higher depth instead of replacing ground tiles, preserving transparency.
- **Tile Palette Recursive Scan**: Tileset dropdown now uses `recursive_directory_iterator` to find PNGs in subdirectories, with relative path display.
- **Network Panel Docking**: Registered "Network" window with `DockBuilderDockWindow` in editor dockspace (bottom panel).
- **Procedural Art Overhaul**: New pixel art sprites for player (warrior with sword), 5 mob types (Slime/Goblin/Wolf/Mushroom/Forest Golem), trees with bark/canopy, rocks, 3 ground tile variants. Procedural tileset PNG (8×5 grid: grass/dirt/water/stone/decorative).
- **Expanded Test Scene**: Ground grid 48×32 (was 32×20), 24 clustered trees (was 8), 8 rock decorations, player spawns at origin, spawn zone shrunk to 200×150.
- **Bundled Tilesets**: Downloaded Hyptosis 32×32 tiles (CC-BY 3.0, 6 sheets), LPC terrain/atlas (CC-BY-SA 3.0), Town tileset (CC-BY-SA 4.0).

### March 18, 2026 - Phase 7: Player Persistence + Auth

**TLS-authenticated login with PostgreSQL character persistence:**

- **Auth Server**: TLS-encrypted TCP auth server (port 7778) with OpenSSL. Runs on own thread with own DB connection. Handles RegisterRequest and LoginRequest. bcrypt password hashing (work factor 12). Thread-safe result queue consumed by game thread.
- **Auth Client**: Async TLS TCP client for login/register. Background thread for non-blocking UI. FATE_DEV_TLS flag skips cert verification in dev.
- **Auth Protocol**: 128-bit auth token bridges TLS TCP auth to UDP game connection. Token sent in Connect packet payload. 30-second expiry. Existing 32-bit UDP session token unchanged.
- **Database Layer**: PostgreSQL via libpqxx (FetchContent). Two separate DB connections (thread safety). Repository pattern: AccountRepository, CharacterRepository, InventoryRepository. Parameterized queries throughout.
- **Schema**: 1:1 with Unity project's PostgreSQL (DigitalOcean managed DB). Separate dev database `fate_engine_dev` with all 65 tables. Migration: `docs/migrations/001_full_schema.sql`.
- **Persistence**: Character stats, tile-coord position, gold, scene name saved on disconnect. Loaded from DB on connect. Retry-once on save failure with DATA LOSS logging.
- **Login UI**: ImGui login/register screens with input validation (username 3-20 chars, password 8-128, email, character name 2-16). Class selection (Warrior/Mage/Archer). Connection state machine: LoginScreen → Authenticating → UDPConnecting → InGame.
- **Duplicate Login**: New login kicks existing session (saves data, disconnects, destroys entity).
- **Dependencies**: OpenSSL + libpq via vcpkg, libpqxx via FetchContent, bcrypt vendored (header renamed `bcrypt_hashpw.h` to avoid Windows SDK collision).

### March 17, 2026 - Phase 6: Networking

**Custom reliable UDP networking with entity replication:**

- **Phase 6a — Transport**: Custom reliable UDP layer over Winsock2. ByteWriter/ByteReader for binary serialization. 16-byte packet header with sequence numbers. 3 channel types: unreliable, reliable-ordered, reliable-unordered. Ack bitfields for delivery confirmation. RTT estimation for adaptive retransmit.
- **Phase 6b — Protocol & Server**: Headless server application (FateServer) running at 20 tick/sec with no rendering. NetServer/NetClient facades. Session tokens, heartbeat/timeout for connection management. Full message protocol (CmdMove, SvEntityEnter, SvEntityLeave, SvEntityUpdate, etc.).
- **Phase 6c — Replication**: AOI-driven entity replication with enter/leave/update messages. Delta compression using per-component field bitmasks — only changed fields sent. Ghost entities on the client for remote players/mobs.
- **Phase 6d — Gameplay Integration**: Client-side position interpolation for ghost entities. Editor Network Panel for connect/disconnect, host/port config, client ID and ghost count display.

### March 17, 2026 - Phase 5: Job System + Graphics RHI

**Fiber-based parallelism and graphics abstraction layer:**

- **Fiber Job System**: Win32 fibers with 4 worker threads and a 32-fiber pool. Lock-free MPMC queue for job dispatch. Counter-based suspend/resume so fibers can wait on dependencies without blocking. Fiber-local scratch arenas for per-job temporary allocations.
- **Graphics RHI**: `gfx::Device` and `gfx::CommandList` interfaces abstracting the rendering backend. Pipeline State Objects (PSO) for state management. Typed 32-bit resource handles. Uniform cache to avoid redundant GL calls. OpenGL 3.3 backend implementation. Full render code migration from raw GL calls to the RHI layer.

### March 17, 2026 - Phase 4: Render Graph + Particles + Lighting + Post-Process

**10-pass render pipeline with visual effects and editor polish:**

- **Render Graph**: Declarative 10-pass pipeline: GroundTiles → Entities → Particles → SDFText → DebugOverlays → Lighting → BloomExtract → BloomBlur → PostProcess → Blit. Automatic FBO management and pass ordering.
- **Particle System**: CPU-driven particle emitters with spawn rate/burst modes, gravity, per-particle lifetime, rotation, and color lerp. Registered as an ECS system.
- **2D Lighting**: Ambient light + point lights rendered to a light map FBO via additive accumulation, then composited multiplicatively over the scene.
- **Post-Processing**: Bloom (threshold extract + two-pass Gaussian blur + additive composite), vignette, and color grading with live editor controls.
- **ImGuizmo**: Visual translate/scale/rotate handles on selected entities. R key for rotate tool mode with undo support.
- **Editor Polish**: Post-Process Panel for live tweaking, Network Panel stub, rotate tool in toolbar.

### March 17, 2026 - Phase 3: Asset Hot-Reload & Allocator Visualization

**Asset Hot-Reload — edit assets, see changes instantly without restarting:**

- **AssetHandle** (`engine/asset/asset_handle.h`): 32-bit generational index (20-bit slot + 12-bit generation). Components reference assets by handle instead of raw pointers. Handles survive reloads and detect stale references in O(1).
- **AssetRegistry** (`engine/asset/asset_registry.h/cpp`): Unified singleton registry with type-erased loaders. Path canonicalization via `std::filesystem::weakly_canonical` ensures consistent lookups regardless of relative/absolute paths. Slot 0 permanently reserved as null sentinel.
- **Debounced reload queue**: File changes are buffered for 300ms to handle editors that fire multiple events per save. Thread-safe queueing from the watcher thread, processing on the main thread (no GL calls under mutex).
- **FileWatcher** (`engine/asset/file_watcher.h/cpp`): Windows `ReadDirectoryChangesW` with overlapped I/O on a background `std::jthread`. Proper UTF-8/UTF-16 conversion for non-ASCII paths. Clean shutdown via manual-reset event + join.
- **Texture reload**: `Texture::reloadFromFile()` calls `glTexImage2D` on the existing GL texture name, avoiding delete/recreate and preserving texture IDs across reloads.
- **Shader reload**: `Shader::reloadFromFile()` compiles a new program via `loadFromSource()`, swaps on success, rolls back to the old program on failure. Uniform cache cleared after swap. Edit a `.frag`, save, see results instantly.
- **JSON reload**: Parse into temp, validate, swap with existing. Malformed JSON files are rejected with a warning.
- **Shader pair awareness**: Loading a `.vert` file registers the `.frag` partner as an alias in the path index, so changing either file triggers reload of the pair.
- **TextureCache migration**: `TextureCache::load()` now delegates to `AssetRegistry` internally, returning a non-owning `shared_ptr` (no-op deleter). This is Step 1-2 of the migration; full `TextureCache` removal deferred.
- **Shutdown ordering**: `FileWatcher::stop()` -> `TextureCache::clear()` -> `AssetRegistry::clear()` -> GL context teardown.

**Allocator Visualization — see memory usage in real-time while editing:**

- **AllocatorRegistry** (`engine/memory/allocator_registry.h`): Header-only singleton where arenas and pools register themselves with `std::function` callbacks for stats queries. Entire system compiles away with `ENGINE_MEMORY_DEBUG=OFF`.
- **Pool occupancy bitmap** (`engine/memory/pool.h`): Debug-only 1-bit-per-block bitmap tracking which blocks are allocated. Cost: `blockCount / 8` bytes, allocated from the same backing arena.
- **Memory debug panel** (`engine/editor/memory_panel.h/cpp`): 3-tab ImGui window accessible via View > Memory:
  - **Arenas tab**: Color-coded progress bars (green < 70%, yellow < 90%, red > 90%) showing used/reserved. Secondary dimmer bar for committed (physical) memory.
  - **Pools tab**: Per-block heat map grid (red=occupied, gray=free) using `ImDrawList::AddRectFilled`. Tooltip on hover shows block index. Summary shows active/total counts.
  - **Frame Timeline tab**: ImPlot scrolling line chart with 300-frame ring buffer of `frameArena.current().position()`. Red high-water mark reference line. Reset button.
- **ImPlot** added via FetchContent (v0.16). ImGui pinned to `v1.91.9b-docking` for API compatibility.
- **Registered allocators**: WorldArena and FrameArena auto-register/deregister with the registry in their owners' constructors/destructors.

**New files (14):** `engine/asset/asset_handle.h`, `engine/asset/asset_registry.h/cpp`, `engine/asset/file_watcher.h/cpp`, `engine/asset/loaders.h/cpp`, `engine/memory/allocator_registry.h`, `engine/editor/memory_panel.h/cpp`, `tests/test_asset_handle.cpp`, `tests/test_asset_registry.cpp`, `tests/test_allocator_registry.cpp`, `tests/test_pool_bitmap.cpp`

**Tests:** 120 -> 137 test cases, 1241 -> 1288 assertions

### March 16, 2026 - Spawn Zone System

**Region-based mob spawning with visual editor controls:**

Spawn zones are spatial entities you place and resize in the scene editor. Mobs spawn within the zone bounds, roam constrained to the zone, and return to the zone after leashing.

**New files:** `game/shared/spawn_zone.h`, `game/systems/spawn_system.h`

**Spawn zone features:**
- SpawnZoneComponent attached to an entity with Transform — drag to reposition, resize handles to adjust zone bounds
- Green rectangle outline shows zone boundary (toggleable "Show Bounds" checkbox)
- Multiple MobSpawnRule entries per zone: enemy ID, target count, level range, HP/damage, respawn time, aggressive/boss flags
- SpawnSystem ticks every 0.5s: detects deaths, processes respawn timers, tops up missing mobs
- Random spawn positions with minimum distance between mobs (adaptive spacing)

**Mob containment:**
- Mob roamRadius auto-scales to 40% of zone's smaller dimension
- constrainMobsToZone() clamps mob home positions back inside zone after leash returns
- Mobs return to zone when player exits leash radius, then resume roaming within zone bounds

**Inspector controls:**
- Zone Name, Zone Size (draggable), Min Spawn Distance, Tick Interval, Show Bounds toggle
- Per-rule tree nodes: enemy ID, target count, level range, base HP/damage, respawn seconds, aggressive/boss checkboxes
- "+ Add Rule" and "Remove Rule" buttons
- All rules editable at runtime — changes take effect on next tick

**Editor integration:**
- Spawn zone entities selectable and draggable in scene (W:Move)
- Resize handles on corners and edges (E:Size tool, or always for spawn zones)
- Undo/Redo support for move and resize
- Added to "+ Add Component" popup under Game Systems

**Mob behavior flags (per-mob in Mob AI inspector):**
- Can Roam: whether mob wanders when idle
- Can Chase: whether mob pursues targets
- Roam While Idle: alternates roam/idle phases vs standing still until aggro
- Non-aggressive rules automatically set canRoam=false, canChase=false, roamWhileIdle=false

**Replaced manual mob spawning:**
- Test scene now creates a "Whispering Woods" spawn zone entity with 5 rules (Slime x3, Goblin x2, Wolf x2, Mushroom x2, Forest Golem x1 boss)
- CombatActionSystem no longer handles respawns — SpawnSystem manages all mob lifecycle

### March 16, 2026 - Spatial Hash Grid

**New file:** `game/shared/spatial_hash.h` — grid-based spatial index for O(1) range queries

**How it works:**
- World divided into 128x128px cells (4x4 tiles)
- Entities inserted into cells by position, rebuilt each frame
- Range queries only check entities in nearby cells instead of iterating all entities
- Supports: `findNearest()` with filter, `queryRadius()`, `findAtPoint()` for click/touch targeting

**Integrated into:**
- **MobAISystem**: builds a player grid each frame, mobs find nearest player via spatial hash instead of brute-force forEach. Scales from O(N*P) to O(N+P)
- **CombatActionSystem**: builds a mob grid each frame, used by Space-to-target (`findNearestMob`), click/touch targeting (`findAtPoint`), and any future mob queries

**Performance:** With 5 mobs and 1 player, difference is negligible. With 100+ mobs and multiple players (multiplayer), this avoids the quadratic blowup of brute-force distance checks every frame.

### March 16, 2026 - Targeting, HUD Polish, Mob AI Cardinal Fix

**Click/touch-to-target system:**
- Left-click or tap on a mob to target it (replaces current target)
- Left-click or tap on empty space to deselect current target
- Touch and mouse both supported — ImGui panels correctly block targeting when UI is open
- Target auto-clears when mob leaves the camera's visible bounds (logged)
- Escape key also clears target (no longer quits app — use window X or Alt+F4)

**HUD bars redesigned to match Unity prototype layout:**
- HP bar (green): top-left — matches TWOM style
- MP bar (blue): top-right
- XP bar (gold): bottom-center
- Bars drawn directly to foreground draw list (no ImGui window), positions adjustable via F3 editor HUD Layout panel
- F1 HUD stripped to controls hint only — debug info (FPS, pos, entities, stats) moved to F3 Debug Info panel

**Mob AI roaming cardinal fix:**
- Roam movement now uses `calculateCardinalChaseTarget()` (L-shaped pathing), same as chase mode
- Previously, `setCardinalVelocity` on raw delta could flip axes frame-to-frame near 45-degree angles, creating diagonal drift
- Mobs now strictly move one axis at a time during roaming, as intended by the cardinal movement system

### March 16, 2026 - Class Selector & Inspector Polish

**Class switching in Character Stats inspector:**
- Dropdown selector: Warrior / Mage / Archer
- Switching class instantly reconfigures all ClassDefinition values (base stats, per-level gains, resource type, hit rate, attack range)
- Stats recalculated, HP/MP restored to new max, fury reset on class change
- Level drag auto-recalculates stats + XP on release
- "Full Heal" button: restores HP/MP and clears dead flag
- Inspector shows: class name, resource type, base stats, attack range, damage multiplier

**Nameplate inspector controls:**
- "Show Level" checkbox: toggle level display (Lv1) on/off for both player and mob nameplates
- "Font Size" slider (0.3 - 2.0): resize nameplate text live
- Nameplates now properly centered above sprites using Font::measureText()

**Combat Controller cleanup:**
- Removed unused autoAttackEnabled and targetEntityId fields (auto-attack state managed by CombatActionSystem)
- Only shows Base Cooldown (editable) and CD Remaining (read-only)

**HUD maxFury fix:**
- Fury display reads s.maxFury directly instead of recomputing from formula
- Inspector changes to maxFury properly reflected in HUD

### March 17, 2026 - Input System & SDF Text Rendering

**Action map input system:**
- ActionId enum with 23 logical actions (movement, combat, skills, UI toggles, chat)
- ActionMap translates SDL scancodes → actions with press/hold/release states
- Primary + secondary key bindings (WASD + arrow keys for movement)
- Gameplay/Chat context switching — Enter opens chat mode, suppresses gameplay actions
- InputBuffer: 6-frame circular buffer queues combat inputs during GCD/animations
- consumeBuffered() for responsive skill activation — no more missed inputs during cooldowns
- All game systems migrated from raw SDL to action API (movement, combat, UI toggles)

**SDF text rendering (replaces bitmap font):**
- MTSDF uber-shader integrated into SpriteBatch via `renderType` vertex attribute
- 5 render modes: sprite (0.0), normal text (1.0), outlined (2.0), glow (3.0), shadow (4.0)
- Runtime font atlas generation from system TTF via stb_truetype (swappable with msdf-atlas-gen)
- SDFText class: drawWorld() (Y-up), drawScreen() (Y-down), measure(), UTF-8 decode
- Damage numbers: Shadow style for hits, Glow for crits, Outlined for XP
- Nameplates: Outlined style with black border for readability over any background
- Text scales perfectly at any zoom level — no pixelation
- Old TextRenderer removed (380 lines deleted)

**Files: 15 changed, +2,384/-445 lines across 4 commits (SDF) + 4 commits (Input)**

### March 17, 2026 - Reflection, Serialization & Game Systems

**Reflection system:**
- FATE_REFLECT macro generates compile-time field metadata (name, offset, size, type) per component
- FieldType enum: Float, Int, Bool, Vec2, Vec3, Color, Rect, String, EntityHandle, Direction, Custom
- Reflection<T> template specialization with static fields() returning std::span<const FieldInfo>
- All 30+ game components reflected with FATE_REFLECT or FATE_REFLECT_EMPTY

**Serialization registry:**
- ComponentMetaRegistry maps string names → type-erased construct/destroy/toJson/fromJson
- Auto-generates JSON serializers from reflected fields (zero boilerplate for simple components)
- Custom serializer overrides for complex types (SpriteComponent, etc.)
- Alias support for backward compatibility ("Sprite" → "SpriteComponent")
- ComponentFlags (Serializable, Networked, Persistent, EditorOnly) per component via component_traits<T>

**Prefab system rewrite:**
- Removed 164 lines of hardcoded per-component serialization
- entityToJson/jsonToEntity now fully data-driven via ComponentMetaRegistry
- Unknown components preserved as raw JSON (no data loss on version mismatch)
- Adding new component requires zero changes to prefab.cpp

**Scene save/load rewrite:**
- Removed 160 lines of hardcoded serialization from editor.cpp
- Version header ("version": 1) added for future migration support
- Registry-based serialization for all entities and components

**Generic inspector:**
- drawReflectedComponent() auto-renders any reflected component via ImGui widgets
- Fallback for components without manual inspector code
- Manual inspectors take priority when present

**Type-erased addComponentById:**
- World::addComponentById(handle, CompId, size, alignment) enables runtime component creation by ID
- Entity::unknownComponents_ preserves unregistered component JSON across save/load

**Explicit component registration:**
- registerAllComponents() in game/register_components.h called from GameApp::onInit()
- All 30+ components registered with traits, reflection, and serializers

**NPC & quest systems (new):**
- NPCInteractionSystem: click-to-interact with NPCs, range check, dialogue triggers
- QuestSystem: event-driven progress tracking, 5 objective types, quest markers on NPCs
- NPC Dialogue UI: branching story dialogue with quest accept/decline/complete
- Shop UI, Quest Log UI, Skill Trainer UI, Bank Storage UI, Teleporter UI
- 6 starter quests, NPC templates with composable roles

**Faction system (4 factions: Xyros, Fenor, Zethos, Solis):**
- FactionRegistry with definitions (color, home village, merchant NPC)
- FactionComponent on all players, set permanently at character creation
- Same-faction PvP block in CombatActionSystem (faction check before any attack)
- Cross-faction chat garbling: public channels (Map/Global/Trade) garbled via deterministic FactionChatGarbler; Party/Guild/Whisper/System channels pass through
- ChatManager.localFaction set at player creation for client-side garble routing
- Faction-based spawn position (each faction starts at distinct map offset)
- Home village PK exception: `isInHomeVillage()` checks attacker zone against faction's homeVillageId — ready for future PvP kill handler
- `FactionRegistry::isHomeVillage()` utility for zone name matching

**Pet system (leveling, rarity-tiered stats, auto-loot):**
- PetDefinition (base stats + per-level growth, rarity determines stat quality)
- PetInstance (level, XP, auto-loot toggle, tradable by default)
- PetSystem static utility: effectiveHP/CritRate/ExpBonus, addXP with player-level cap (max 50)
- PetComponent on all players (empty by default, equip pet to activate)
- Pets gain 50% of player XP, cannot outlevel owner
- `PetSystem::applyToEquipBonuses()` writes pet HP/CritRate to CharacterStats equipBonus fields for recalculateStats()

**Stat enchant system (accessory-only enchanting):**
- StatEnchantSystem: 7 scroll types (STR/INT/DEX/VIT/WIS/HP/MP), Belt/Ring/Necklace/Cloak only
- 6-tier outcome table: Fail 25%, +1 30%, +2 25%, +3 12%, +4 6%, +5 2%
- Fail removes existing enchant (risk/reward), new enchant replaces previous, no item break
- HP/MP scrolls use x10 scaling (+10/+20/+30/+40/+50)
- statEnchantType/statEnchantValue fields on ItemInstance
- `StatEnchantSystem::applyToEquipBonuses()` writes accessory enchant values to CharacterStats equipBonus fields (STR/INT/DEX/VIT/WIS/HP/MP)

**Mage double-cast mechanic (hidden instant-cast window):**
- SkillDefinition gains castTime, enablesDoubleCast, doubleCastWindow fields
- Casting a double-cast-enabled spell opens a timed window (default 2s)
- Next spell within window is instant cast (zero cast time), mana/cooldown still apply
- No UI indicators — players discover through experimentation
- Window expires on use, timeout, or CC (not frozen by stun/root)

**Bug fixes:**
- Quest turn-in XP test: accounted for level-up when XP reward equals level threshold (test_quest_manager.cpp — 106/106 tests now passing)

**Files: 56 changed, +7,831 lines across 21 commits**

### March 17, 2026 - Engine Research Upgrade (Full Stack)

**Archetype ECS (replaced unordered_map-per-entity storage):**
- Contiguous typed columns per archetype — all entities with the same component set stored together
- Swap-and-pop entity removal, O(matching entities) forEach queries
- Cached archetype matching with version-counter invalidation
- Deferred command buffer for structural changes during iteration
- Entity* pointer stability preserved (heap-allocated, not stack facade)
- Compile-time CompId (uint32_t) with Hot/Warm/Cold tier classification

**Memory system (Fleury-inspired arena stack):**
- Zone Arena (256 MB reserve per scene) — O(1) bulk reset on zone unload
- Frame Arena upgraded to 64 MB double-buffered
- Thread-local scratch arenas (2 per thread, 256 MB) with conflict-avoidance
- ScratchScope RAII guard for automatic reset on scope exit
- Pool allocator composes on arena backing

**Spatial Grid (fixed power-of-two, replaces hash for bounded worlds):**
- Direct-indexed: `(y << gridBits) | x` — two shifts and an OR, zero hash computation
- gridBits computed from map dimensions at zone load
- Counting-sort rebuild via prefix sums (same O(n) pattern as Mueller hash)
- queryRadius returns std::span into scratch arena (zero-copy)
- findNearest returns Expected<EntityHandle, SpatialError>
- MobAISystem migrated to engine SpatialGrid (removed duplicate spatial hash)

**7-state chunk lifecycle (upgraded from 3 states):**
- States: Queued → Loading → Setup → Active → Sleeping → Unloading → Evicted
- Ticket system: player proximity, system holds (boss fights), future multiplayer
- Rate-limited transitions (max 4 per frame) prevent frame hitches
- Double-buffered staging tiles for future async loading
- Concentric rings: active buffer → sleep buffer → prefetch buffer

**Zone snapshots & persistent IDs:**
- PersistentId: 64-bit (zoneId:16 | creationTime:32 | sequence:16) with overflow handling
- ZoneSnapshot: serialization skeleton for mob positions, health, respawn timers
- Absolute timestamps for respawn timers (time passes correctly while zone unloaded)

**Scene/SceneManager zone arena lifecycle:**
- Scene owns dedicated zone arena, reset on exit
- Loading state tracking (isLoading, loadProgress) for future loading screens
- Snapshot hooks for persistent zone state

**Profiling & diagnostics:**
- Tracy Profiler integration (on-demand, named/colored zones, frame marks)
- FATE_ZONE/FATE_ALLOC/FATE_FRAME_MARK macro wrappers
- SpriteBatch dirty flag — FNV-1a hash-based sort skip when draw order unchanged
- MSVC AddressSanitizer preset in CMake

**Multiplayer groundwork (data structures only, no networking):**
- AOI: VisibilitySet with enter/leave/stay diffs, hysteresis
- Ghost entity scaffold: GhostFlag component + GhostArena
- Delta compression: dirty bit infrastructure on components

**Component migration:**
- All 27 game components migrated from virtual Component base to new FATE_COMPONENT macros
- Zero RTTI — compile-time type IDs and tier classification
- Components are plain structs, no vtable overhead

**Testing:**
- doctest framework integrated via FetchContent
- 32 test cases, 740 assertions covering arena, archetype, spatial grid, chunk lifecycle, world

**Files: 47 changed, +3,823 lines across 15 commits**

### March 16, 2026 - Skill Bar UI & HUD Bars

**Skill Bar UI (right side of screen):**
- 5 visible slots per page, 4 pages (20 total), matching SkillManager's 4x5 layout
- Page navigation: up/down arrows in panel, [ and ] keyboard shortcuts
- K key toggles skill bar visibility
- Drag skills from Inventory Skills tab to assign to slots
- Right-click any slot to clear it
- Cooldown overlay: dark sweep fills slot + countdown timer when skill is on CD
- Tooltips show skill ID, rank, cooldown remaining
- Semi-transparent dark panel, color-coded borders (blue=assigned, red=on CD, gray=empty)

**New files:** `game/ui/skill_bar_ui.h` (52L), `game/ui/skill_bar_ui.cpp` (208L)

**Graphical HUD Bars (TWOM layout):**
- HP bar (green, top-left), MP bar (blue, top-right), XP bar (gold, bottom-center)
- Drawn to foreground draw list — no ImGui window, no input stealing
- Positions/sizes adjustable via F3 editor HUD Layout panel with Reset to Defaults button
- Text shadow for readability, syncs with F1 toggle

**New files:** `game/ui/hud_bars_ui.h`, `game/ui/hud_bars_ui.cpp`

**Inventory Skills tab upgraded:**
- Shows available/earned/spent skill points
- Lists all learned skills with rank display (activated/unlocked)
- "+" button to spend skill points and activate next rank
- Cooldown indicator on skills currently on CD
- Each skill is a drag source — drag to Skill Bar slots to assign
- Hint text: "Drag skills to the Skill Bar ->"

**Controls updated:**
- HUD hint line: "WASD:Move Space:Attack I:Inv K:Skills []:Pages F1:HUD F3:Editor"

### March 16, 2026 - Nameplates, Gold Drops, Death System

**World-space nameplates above all entities:**
- Player nameplates: "Name Lv1" in PK status color, guild name in green below if in guild
- Mob nameplates: colored by TWOM level-difference system (gray/green/white/blue/purple/orange/red)
- Boss mobs show "[Boss] Name Lv5" prefix
- Mob HP bars appear below nameplate when damaged (green >50%, red <50%, dark red background)
- Dead mob nameplates and HP bars hidden automatically

**Gold drops on mob kill:**
- Mobs drop gold on death: minGold = level*2, maxGold = level*5, 100% drop chance
- Gold added directly to player's Inventory component
- "+X Gold" floating text in gold color
- HUD shows "Gold: X" below XP/Fury line

**Death system visual feedback:**
- Setting isDead (via inspector or HP reaching 0) grays out player sprite (0.3 tint, 60% alpha)
- 5-second respawn countdown starts automatically
- Sprite tint restored to white on respawn, HP fully restored
- Movement blocked while dead (existing check in movement system)

**HUD maxFury fix:**
- Fury display now reads s.maxFury directly instead of recomputing from classDef formula
- Inspector changes to maxFury properly reflected in HUD

### March 16, 2026 - Editor Inspector Overhaul & Combat Polish

**All 18 game components now fully editable in the inspector:**
- Character Stats: editable name, level, HP/MP, fury, honor, PK status dropdown, dead checkbox, "Recalc Stats" button
- Enemy Stats: editable name, type, level, HP, damage, armor, MR, hit rate, crit, attack/move speed, XP/honor rewards, aggressive/magic/alive checkboxes, "Clear Threat" button
- Mob AI: editable acquire/contact/attack radii, chase/return/roam speeds, attack cooldown, think interval, passive checkbox, live mode/facing display
- Combat Controller: editable base cooldown, auto-attack toggle
- Nameplate/Mob Nameplate: editable name, level, boss/elite/visible
- Targeting: editable max range, "Clear Target" button
- Status Effects: live display with "Clear All Effects" button
- Crowd Control: live CC state with "Clear CC" button
- All components removable via right-click context menu

**Add Component popup expanded with 3 sections:**
- Engine: Transform, Sprite, Box/Polygon Collider, Player Controller, Animator, Zone, Portal
- Game Systems: Character Stats, Enemy Stats, Mob AI, Combat Controller, Damageable, Inventory, Skill Manager, Status Effects, Crowd Control, Targeting, Nameplate, Mob Nameplate
- Social: Chat, Party, Guild, Friends, Trade, Market

**Mob AI rewrite — now matches C# ServerZoneMobAI exactly:**
- L-shaped pathfinding via `calculateCardinalChaseTarget()` returns intermediate target position (not direction)
- `driveMovement()` replaces old process methods, matching C# `DriveMovement()` switch structure
- Attack mode: mobs continue chasing while attacking if outside stopDistance (80% of range)
- Wiggle system: random initial perpendicular direction, then alternates (matches C# StartWiggle)
- All distance thresholds scaled to pixels (5px aligned, 6px arrival)
- Mob speeds reduced: chase 1.5 tiles/s (48px/s), roam 0.8 tiles/s (25.6px/s)

**Text rendering fixes:**
- Added `drawWorldYUp()` for Y-up camera space (floating damage text)
- Original `drawWorld()` preserved for Y-down screen space (HUD)
- Floating text now renders right-side-up and floats upward correctly

**Target selection marker:**
- Pulsing red border around targeted mob (4 rectangles, sin-wave alpha)

### March 16, 2026 - Player Attack System (Core Gameplay Loop)

**TWOM-style Option B combat fully implemented:**

The core gameplay loop is now complete: walk → aggro → fight → kill → XP → level up → repeat.

**New file:** `game/systems/combat_action_system.h` (561L)

**TWOM Option B targeting (matching Unity prototype):**
- Space with no target → auto-selects nearest mob within 10 tiles (NO attack)
- Space with target (Warrior/Archer) → enables auto-attack (continuous on cooldown)
- Space with target (Mage) → fires one spell per press (no auto-attack)
- Escape → clears target, stops auto-attack
- Auto-attack stops when: target dies, target out of range, player presses Escape

**Attack resolution using ported combat formulas:**
- Warriors/Archers: hit rate roll (CombatSystem::rollToHit) → Miss or Hit → damage via calculateDamage
- Mages: spell resist roll (CombatSystem::rollSpellResist) → Resist or Land → damage
- Fury generation: +0.5 per normal hit, +1.0 per crit (Warriors/Archers only)
- Attack cooldown: weaponAttackSpeed * (1 - equipBonusAttackSpeed), clamped 0.2-2.0s
- Range check: melee 1 tile (Warriors), ranged 7 tiles (Archers/Mages)

**Floating damage text:**
- Numbers float upward at 30px/s and fade out over 1.2s
- White = normal hit, Orange = crit (1.3x scale), Gray = "Miss", Purple = "Resist"
- Yellow "+X XP" on mob kill, Gold "LEVEL UP!" on level up (1.3x scale)

**Mob death and respawn:**
- XP awarded via XPCalculator with level-difference scaling (gray 0% through red 130%)
- Honor awarded for bosses/elites
- Mob sprite hidden on death, respawns at home position after 10s (configurable)
- Threat table cleared on respawn, HP fully restored

**HUD updates:**
- Target info displayed: name, level, HP/maxHP (red tint)
- Controls hint: "WASD:Move Space:Attack F1:HUD F2:Colliders F3:Editor"

### March 16, 2026 - Inventory UI

**Complete TWOM-style inventory panel with tabs:**
- Toggle with I key during gameplay
- Inventory tab: 15-slot grid (5x3) with drag-and-drop, 10 equipment slots (Hat, Armor, Gloves, Shoes, Belt, Cloak, Weapon, Shield, Ring, Necklace)
- Drag items between inventory and equipment slots, double-click to equip/unequip
- Item tooltips: name, rarity, rolled stats, enchant level, socket, soulbound/protected flags
- Rarity-colored borders (Common white, Uncommon green, Rare blue, Epic purple, Legendary orange)
- Gold display with K/M/B formatting, trade-locked slot indicators
- Stats tab: HP/MP/XP progress bars, primary stats (STR/VIT/INT/DEX/WIS), derived stats (armor, crit, speed), fury/honor/PvP
- Skills/Community/Settings tabs stubbed for future implementation
- Reads directly from InventoryComponent and CharacterStatsComponent on the player entity

### March 16, 2026 - Zone/Portal System

**Zone regions and portal transitions for multi-zone scenes:**
- ZoneComponent: named regions with size, level range, PvP flag, zone type
- PortalComponent: trigger area with target scene/zone, spawn position, fade transition
- ZoneSystem: detects player entering zones, triggers portal teleport + fade-to-black overlay
- Same-scene portals for dungeons (Lighthouse F1→F2→F3 within one scene)
- Cross-scene portals for map transitions (Whispering Woods → Town)
- Debug rendering: blue zone outlines, yellow portal markers with direction arrows (F2)
- Full editor integration: Add Component → Zone/Portal, inspector editing, scene save/load
- Fade overlay renders as full-screen black with configurable alpha/duration

### March 16, 2026 - Editor QoL: Undo/Redo, Tool Modes, Console, Hierarchy Grouping

**Undo/Redo system:**
- Command pattern with 200 action history
- Tracks move, resize, delete, duplicate operations
- Ctrl+Z undo, Ctrl+Y redo, toolbar buttons

**Tool modes (W/E/B/X):**
- W = Move tool (drag entities)
- E = Resize tool (drag corner/edge handles)
- B = Paint tool (tile painting from palette)
- X = Erase tool (click/drag to delete ground tiles)
- Active tool highlighted blue in toolbar

**Keyboard shortcuts:**
- Ctrl+Z/Y undo/redo, Ctrl+S save, Ctrl+D duplicate, Ctrl+A select all, Delete key

**Hierarchy grouping:**
- Entities with same name+tag grouped into collapsible tree nodes (e.g., "Tile (ground) x640")
- Unique entities shown as flat items
- Color-coded: blue=player, green=ground, orange=obstacle, red=mob, purple=boss
- Error badges [!] in red for missing textures

**Log Viewer panel:**
- Shows all engine log messages in editor
- Filter by level (DBG/INF/WRN/ERR) and text search
- Color-coded by severity, auto-scroll, clear button
- Starts collapsed, doesn't steal focus

**Command Console panel:**
- Commands: help, list (grouped output), count, find, delete, spawn, tp
- `list` groups duplicate entities (e.g., "Tile (ground) x640")
- Tile entity creation no longer floods the log

**Eraser tool:**
- X key activates, click/drag to delete nearest ground tile
- Undo support for deleted tiles

**Panel focus fix:**
- Log and Command panels start collapsed with NoFocusOnAppearing
- Inspector and Hierarchy drawn last for priority focus
- ImGui ini persistence saves window layout between sessions

### March 16, 2026 - Game Systems ECS Integration

**All ported game systems now connected to the engine's Entity-Component-System:**

Game logic classes from `game/shared/` are now wrapped in ECS components and ticked by ECS systems every frame. Player and mob entities are created via `EntityFactory` with the full component set matching the Unity PlayerScene2 prefab (24 MonoBehaviours → 18 ECS components).

**New files created:**
- `game/components/game_components.h` (147L) — 18 ECS component wrappers (CharacterStatsComponent, CombatControllerComponent, InventoryComponent, SkillManagerComponent, StatusEffectComponent, CrowdControlComponent, TargetingComponent, ChatComponent, GuildComponent, PartyComponent, FriendsComponent, MarketComponent, TradeComponent, NameplateComponent, EnemyStatsComponent, MobAIComponent, MobNameplateComponent, DamageableComponent)
- `game/systems/gameplay_system.h` (200L) — Ticks all game logic: StatusEffects, CrowdControl, HP/MP regen (10s/5s intervals), PK status decay (Purple 60s, Red 1800s, Black 600s), respawn countdowns, nameplate sync
- `game/systems/mob_ai_system.h` (134L) — Ticks MobAI for all mob entities: brute-force player scanning, cardinal movement application, attack callback wiring with real damage formulas (armor reduction via CombatSystem)
- `game/entity_factory.h` (223L) — `EntityFactory::createPlayer()` builds a player with all 17+ components and class-specific stat configuration (Warrior/Mage/Archer from CLAUDE.md tables); `EntityFactory::createMob()` builds mobs with EnemyStats, MobAI, placeholder sprites

**Updated files:**
- `game/game_app.h` — Added GameplaySystem and MobAISystem pointers, spawnTestMobs() method
- `game/game_app.cpp` — Uses EntityFactory for player creation, registers GameplaySystem + MobAISystem, spawns 5 test mobs (Slime Lv1, Goblin Lv2, Wolf Lv3, Mushroom Lv1 passive, Forest Golem Lv5 boss), HUD shows live player stats (HP/MP/Level/Class/XP/Fury)

**What runs at startup:**
- Level 1 Warrior player with real stats (70 HP, 30 MP, 14 STR, 12 VIT)
- 5 mobs that roam, aggro, chase (cardinal-only), and attack using ported combat formulas
- HP/MP regeneration ticking every 10s/5s
- Status effects and crowd control processing every frame
- HUD displaying live character stats

**Bug fixes during code review:**
- Fixed spell resist formula: mob MR now subtracts from effective INT (not flat % bonus)
- Fixed block counter stat: uses max(STR, DEX) instead of sum (matches C# exactly)
- Fixed hit chance cap: clamped to 0.97 (not 1.0) matching C#'s intentional 97% max

### March 16, 2026 - Editor Polish, Collision Upgrade, Pixel Art Scaling

**Virtual resolution changed from 960x540 to 480x270:**
- Everything renders 2x larger, matching pixel art proportions
- Player sprites (trimmed 20x33) look correct relative to 32px tiles
- Camera follow removed pixel-snapping to eliminate movement jitter at new resolution

**Collision system upgraded:**
- MovementSystem now checks all 4 collision combos: Box vs Box, Box vs Polygon, Polygon vs Box, Polygon vs Polygon
- Polygon colliders use SAT (Separating Axis Theorem) for accurate convex collision
- Polygon debug rendering uses dotted lines that handle any angle correctly

**Editor improvements:**
- Remove Components: right-click any component header to delete it
- Resize handles: 8 drag handles (4 corners + 4 edges) on selected entity for visual resizing
- Sticky selection: clicking inside a selected entity drags it instead of switching to a neighbor
- Box Collider "Fit to Sprite" button auto-sizes to match sprite dimensions
- Polygon Collider Box/Circle presets auto-size to sprite dimensions
- Source Rect UV editor in Sprite inspector for manual tileset region editing
- Tile Palette starts collapsed, scrollable grid, auto-sizes tiles to panel width, crash-proofed
- Scene save/load with file picker (File > Save Scene with name, File > Load Scene lists all scenes)
- New Scene command to clear canvas
- Real sprite asset loading (player.png from Aseprite, trimmed to 20x33)

**First real sprite asset:**
- Player sprite (Sprite-00065.png) trimmed in Aseprite to 20x33 pixels
- Renders at native size in 480x270 viewport, correct proportions
- Tileset (customWhisperingTileset.png 128x128, 4x4 grid) working with tile palette

### March 16, 2026 - Game Systems Port (C# to C++)

**All 20 game systems from the Unity/C# prototype converted to C++:**

Ported 38 files (6,464 lines) to `game/shared/`, all compiling with zero errors. Every formula, constant, and game rule from the Unity prototype is preserved exactly in the C++ versions.

**Core gameplay systems (pure logic, no dependencies):**
- CharacterStats: Full stat calculation with class-specific scaling, equipment bonuses, vitality HP multiplier, damage formulas, death/respawn, XP/leveling
- EnemyStats: Mob stats with threat table (damage attribution), level scaling, death events
- CombatSystem: Complete hit rate with coverage/beyond-coverage, spell resist with INT coverage, block system, armor reduction, PvP class advantage, magic damage reduction
- MobAI: TWOM-style cardinal-only movement with L-shaped pathfinding, axis locking, roam/idle alternation, wiggle unstuck, aggro memory, leash radius
- StatusEffects: 16 effect types (DoTs, buffs, shields, invuln, transform, bewitch), stacking (ArmorShred x3), tick-based processing
- CrowdControl: Stun/freeze/root/taunt with priority hierarchy, immunity checks, freeze-break-on-damage
- XPCalculator: Level-difference scaling (gray 0% through red 130%)
- HonorSystem: PvP honor with PK-status-based gain tables, 5-kill/hour tracking
- EnchantSystem: +1 to +12 with tiered success rates, protection scrolls, secret bonuses, stone tier validation
- ItemInstance + ItemStatRoller: Item data structures with weighted stat rolling

**Social/economy systems (logic complete, networking/DB integration pending):**
- Inventory: 15-slot system with equipment map, gold management, trade slot locking
- SkillManager: Skill learning via skillbooks + skill points, cooldown tracking, 4x5 skill bar
- PartyManager: 3-player parties with +10%/member XP bonus, loot distribution modes
- GuildManager: TWOM-style guilds with ranks, 16x16 pixel symbols, XP contribution
- FriendsManager: Friends list (50 max), block list (100 max), profile inspection
- ChatManager: 7-channel chat system (Map/Global/Trade/Party/Guild/Private/System)
- TradeManager: Two-step security trading (Lock -> Confirm -> Execute), 8 item slots + gold
- MarketManager: Marketplace with 2% tax jackpot system, merchant pass, listing filters
- Gauntlet: Wave survival PvPvE instance with team scoring, tiebreaker elimination

Networking transport (custom reliable UDP) is now implemented. Database persistence via libpqxx remains pending for these systems.

### March 15, 2026 - Initial Engine Build

**Core engine created from scratch:**
- SDL2 + OpenGL 3.3 window and rendering pipeline
- Custom OpenGL function loader (no GLAD, uses SDL_GL_GetProcAddress)
- Batched sprite renderer with depth sorting and texture batching
- 2D orthographic camera with 960x540 virtual resolution
- Custom Entity-Component-System with typed component queries
- Input system (keyboard, mouse, touch) with cardinal direction helper
- Scene management with factory pattern and JSON loading
- Structured logging system (console + file, timestamped, categorized)

### March 15, 2026 - Editor & Scene Building

**Dear ImGui editor built with:**
- Entity hierarchy with search filter and tag color-coding
- Inspector panel for live editing of all component properties with sprite thumbnails
- Project browser with tabs (Sprites, Scripts, Scenes, Shaders, Prefabs)
- Right-click context menus (Open in VS Code, Explorer, Copy Path, Delete)
- Scene interaction: click-to-select (depth priority, closest center), drag-to-move
- Grid overlay aligned to tile edges with smart snapping (ground=grid, objects=free)
- Camera pan (right-click drag) and zoom (scroll wheel, 0.05x-8x)
- Play/Pause with auto-pause on editor open
- Entity create/delete/duplicate with Delete key support while paused
- Scene save/load with file picker (lists all scenes in assets/scenes/)
- New Scene command to clear canvas

### March 15, 2026 - Tile Palette & Painting

**Tileset-based tile painting system:**
- Tile Palette panel with tileset dropdown (scans assets/tiles/)
- Visual tile grid with 2x scaled clickable tiles from tileset
- Paint mode: click or drag to stamp tiles, auto-snaps to grid centers
- Smart overwrite: painting on existing ground tiles updates sourceRect instead of creating duplicates
- sourceRect serialization in scene JSON so painted tiles survive save/load

### March 15, 2026 - Prefab System

**Entity template system:**
- Save any entity as a reusable JSON prefab (Entity > Save as Prefab)
- Spawn copies from Prefabs tab in Project browser
- Prefabs persist to both source and build directories
- Entity duplication via deep JSON copy
- isLocalPlayer flag on PlayerController prevents prefab copies from responding to input

### March 15, 2026 - Collision System

- BoxCollider with AABB detection and offset
- PolygonCollider with Separating Axis Theorem (SAT) for convex polygons
- Debug visualization (F2): colored overlays for all collider types
- Inspector vertex editing for polygon colliders with Make Box/Circle presets

### March 15, 2026 - Text Rendering

- stb_truetype integration for TTF fonts (Consolas from Windows)
- Glyph atlas baking at multiple sizes (16px, 24px)
- Screen-space HUD text drawing
- Coordinate display HUD (tile coordinates, always visible)

---

## Architecture Notes

### Scene/Zone Design
- Each game area (Whispering Woods, Town, Lighthouse) = one scene file
- Zones within a scene are logical regions (e.g., Lighthouse F1, F2, F3 are zones in one scene)
- Zone transitions within a scene = camera teleport + fade effect (no loading)
- Scene transitions between areas = actual load/unload with loading screen
- All entities in a scene are always loaded (players can see across zones)

### Coordinate System
- World coordinates in pixels, tile coordinates derived via `Coords::toTile()`
- Tile size: 32x32 pixels
- Grid lines at tile edges (0, 32, 64...), tile centers at half-grid (16, 48, 80...)
- Origin (0,0) is at a tile corner, not a tile center

### Entity Ownership
- `isLocalPlayer = true` on exactly ONE entity = responds to keyboard input + camera follows
- Prefab templates save `isLocalPlayer = false` by default
- Server/network will set this flag at runtime when players connect

### Game Systems Architecture
```
engine/
├── net/                         # Networking (Phase 6)
│   ├── aoi.h                   # AOI visibility sets
│   ├── ghost.h                 # Ghost entity component
│   ├── byte_stream.h           # Binary serialization
│   ├── packet.h                # Packet header, types, channels
│   ├── socket.h                # UDP socket wrapper
│   ├── socket_win32.cpp        # Winsock2 implementation
│   ├── reliability.h/.cpp      # Ack bitfields, retransmit, RTT
│   ├── connection.h/.cpp       # Client connections, session tokens
│   ├── protocol.h              # All message types (CmdMove, SvEntityEnter, etc.)
│   ├── replication.h/.cpp      # AOI-driven entity replication
│   ├── net_server.h/.cpp       # Server networking facade
│   ├── net_client.h/.cpp       # Client networking
│   └── interpolation.h         # Ghost entity position interpolation
├── job/                         # Job System (Phase 5)
│   ├── fiber.h                 # Platform fiber abstraction
│   ├── fiber_win32.cpp         # Win32 CreateFiber implementation
│   ├── job_system.h            # JobSystem, Job, Counter, MPMC queue
│   └── job_system.cpp          # Fiber pool, worker threads, scheduling
├── render/gfx/                  # Graphics RHI (Phase 5)
│   ├── types.h                 # Typed handles, enums, PipelineDesc
│   ├── device.h                # gfx::Device interface
│   ├── command_list.h          # gfx::CommandList interface
│   └── backend/gl/             # OpenGL 3.3 backend
│       ├── gl_device.cpp
│       ├── gl_command_list.cpp
│       ├── gl_loader.h/.cpp
server/                          # Headless Server (Phase 6+7)
├── server_app.h/.cpp           # ServerApp: 20 tick/sec, loot pipeline, boss tick, pickup, despawn
├── server_main.cpp             # Server entry point
├── auth/
│   └── auth_server.h/.cpp      # TLS auth (bcrypt, register+login, starter equipment)
├── cache/
│   ├── item_definition_cache.h/.cpp  # 748 items from item_definitions (possible_stats, attributes)
│   └── loot_table_cache.h/.cpp       # 72 loot tables from loot_drops (rollLoot, enchant rolling)
└── db/
    ├── db_connection.h/.cpp    # pqxx wrapper with reconnect
    ├── db_pool.h/.cpp          # Thread-safe connection pool (5-50)
    ├── account_repository.h/.cpp     # Account CRUD
    ├── character_repository.h/.cpp   # Character load/save
    ├── inventory_repository.h/.cpp   # Inventory load/save
    └── zone_mob_state_repository.h/.cpp  # Boss death persistence (zone_mob_deaths)

game/
├── shared/                      # Pure game logic (no engine deps)
│   ├── game_types.h             # All enums, constants, ClassDefinition
│   ├── character_stats.h/.cpp   # Player stats, damage, death
│   ├── enemy_stats.h/.cpp       # Mob stats, threat table
│   ├── combat_system.h/.cpp     # Hit rate, resist, block, armor
│   ├── mob_ai.h/.cpp            # TWOM cardinal AI
│   ├── faction.h                # Faction enum, FactionRegistry, FactionChatGarbler
│   ├── pet_system.h/.cpp        # PetDefinition, PetInstance, PetSystem (leveling, stats)
│   ├── stat_enchant_system.h    # StatEnchantSystem (accessory enchanting, 6-tier rolls)
│   ├── (16 more systems...)     # Inventory, skills, social, etc.
│   └── gauntlet.h/.cpp          # Wave PvPvE instance
│
├── components/                  # ECS component wrappers
│   ├── transform.h              # Position, scale, rotation
│   ├── sprite_component.h       # Texture, animation frames
│   ├── player_controller.h      # Movement input
│   ├── box_collider.h           # AABB collision
│   ├── polygon_collider.h       # SAT collision
│   ├── animator.h               # Animation state machine
│   ├── game_components.h        # 20 game logic wrappers
│   ├── faction_component.h      # FactionComponent (player faction)
│   ├── pet_component.h          # PetComponent (equipped pet, auto-loot)
│   ├── dropped_item_component.h # DroppedItemComponent (ground loot, despawn, ownership)
│   └── boss_spawn_point_component.h # BossSpawnPointComponent (fixed coords, respawn)
│
├── systems/                     # ECS systems (tick logic per frame)
│   ├── movement_system.h        # WASD input + collision
│   ├── render_system.h          # Sprite rendering
│   ├── gameplay_system.h        # StatusEffects, CC, regen, PK, respawn
│   ├── mob_ai_system.h          # Mob AI ticking + player scanning
│   └── combat_action_system.h   # TWOM Option B attacks, damage text, XP, respawn
│
├── entity_factory.h             # Creates player/mob entities with all components
├── game_app.h/.cpp              # Main game loop, scene setup, HUD
└── main.cpp                     # Entry point
```

**Integration pattern:**
- `game/shared/` classes are standalone logic (pure C++, no engine deps)
- `game/components/game_components.h` wraps each logic class in an ECS Component struct
- `game/systems/` contain ECS Systems that iterate entities and tick the wrapped logic classes
- `EntityFactory` creates entities with all components attached (mirrors Unity prefab structure)
- Game logic uses `std::function` callbacks; ECS systems wire these to engine actions
- RNG uses `thread_local std::mt19937` seeded from `std::random_device`
- Networking: custom reliable UDP transport with AOI-driven entity replication
- Database: PostgreSQL via libpqxx, server-side caches loaded at startup, character persistence on connect/disconnect
- Loot pipeline: server rolls loot on kill → spawns ground entities → replicates to clients → pickup via CmdAction → despawn after 120s

### Database Integration (Phase 7)

**Connection:** PostgreSQL on DigitalOcean (`fate_engine_dev`, 65 tables). Two separate pqxx connections — game thread and auth thread. Connection pool (5-50) available for future scaling.

**Server Startup Caches (loaded once, read-only):**
| Cache | Table | Records | Key Lookup |
|-------|-------|---------|------------|
| `ItemDefinitionCache` | `item_definitions` | 748 | `getDefinition(itemId)` → `CachedItemDefinition*` |
| `LootTableCache` | `loot_drops` | 835 entries across 72 tables | `rollLoot(lootTableId)` → `vector<LootDropResult>` |
| `MobDefinitionCache` | `mob_definitions` | 73 | by mob_def_id |
| `SkillDefinitionCache` | `skill_definitions` + `skill_ranks` | 60 skills, 174 ranks | by skill ID, by class |
| `SceneCache` | `scenes` | 3 | PvP status queries |

**Repositories (read/write per-request):**
| Repository | Tables | Operations |
|-----------|--------|-----------|
| `AccountRepository` | `accounts` | createAccount, findByUsername, updateLastLogin |
| `CharacterRepository` | `characters` | createDefaultCharacter, loadCharacter, saveCharacter |
| `InventoryRepository` | `character_inventory` | loadInventory, saveInventory |
| `ZoneMobStateRepository` | `zone_mob_deaths` | saveZoneDeaths, loadZoneDeaths, clearZoneDeaths, cleanupExpired |

**Key DB Tables for Loot Pipeline:**
- `item_definitions` — 748 items. `possible_stats` JSONB has two formats: `{"stat":"hp","weighted":true}` and legacy `{"name":"int","weight":1.0}` (both parsed). `attributes` JSONB for bonus stats (mp_bonus, lifesteal, move_speed_pct, etc.).
- `loot_drops` — 835 entries. FK to `loot_tables.loot_table_id` and `item_definitions.item_id`. Columns: `drop_chance` (0.0-1.0), `min_quantity`, `max_quantity`.
- `loot_tables` — 72 tables. Referenced by `mob_definitions.loot_table_id`.
- `character_inventory` — Per-character items. UUID `instance_id` (auto-generated), `rolled_stats` JSONB, `socket_stat`/`socket_value`, `enchant_level`, `is_equipped`/`equipped_slot`. Starter equipment inserted on registration.
- `zone_mob_deaths` — Boss death persistence. `scene_name`, `zone_name`, `enemy_id`, `died_at_unix`, `respawn_seconds`. Loaded on server start, cleared after respawn.

**Starter Equipment (inserted on registration):**
| Class | Weapon | Body | Boots | Gloves |
|-------|--------|------|-------|--------|
| Warrior | `item_rusty_dagger` | `item_quilted_vest` | `item_worn_sandals` | `item_tattered_gloves` |
| Mage | `item_gnarled_stick` | `item_quilted_vest` | `item_worn_sandals` | `item_tattered_gloves` |
| Archer | `item_makeshift_bow` | `item_quilted_vest` | `item_worn_sandals` | `item_tattered_gloves` |
- All formulas are exact 1:1 matches with the C# prototype (verified line-by-line)

---

## Future Implementation Plan

### Near-Term (Engine Foundation)
- [ ] Audio/sound system (SDL_mixer - music per zone, SFX for combat/UI)
- [ ] Sprite animation testing with real spritesheets
- [x] Scene transitions with loading screen and zone portals
- [x] Undo/Redo in editor
- [ ] Multi-select and bulk operations in editor
- [x] Eraser tool for tile painting
- [ ] Auto-load last scene on startup (once real sprites replace procedural)
- [x] Persistence & authentication (PostgreSQL character saves, login flow)
- [ ] Mobile build (iOS/Android via SDL2, touch input, D-Pad)

### Game Systems (Port from Unity Prototype)
- [x] CharacterStats (HP, MP, XP, level, stats, fury, death, respawn)
- [x] EnemyStats (mob HP, threat table, XP/loot distribution on death)
- [x] CombatSystem (hit rate, damage formulas, crit, armor mitigation)
- [x] MobAI (idle, roam, aggro, chase, attack, return home - cardinal movement)
- [x] SkillManager (skill execution, cooldowns, effects, skill bar)
- [x] StatusEffects (buffs, debuffs, DoTs)
- [x] CrowdControl (stun, freeze, root, taunt)
- [x] Inventory (slots, equipment, gold, item usage)
- [x] ItemStatRoller (drop tables, stat rolling)
- [x] XPCalculator (level scaling, party bonus, damage-based distribution)
- [x] HonorSystem (PvP kills, PK status colors)
- [x] EnchantSystem (+1 to +12 enhancement)
- [x] PartyManager (3-player, cross-scene XP bonus)
- [x] GuildManager (ranks, 16x16 pixel symbols, leveling)
- [x] FriendsManager (friends/block lists, profile inspection)
- [x] ChatManager (Map, Global, Trade, Party, Guild, Private channels)
- [x] TradeManager (two-step security trading)
- [x] MarketManager (listings, tax, merchant pass, jackpot)
- [x] Gauntlet (wave survival PvPvE instance)
- [x] SpawnSystem (spawn zones with visual editor, respawn timers, zone containment)
- [x] FactionSystem (4 factions, registry, same-faction PvP block, chat garbling, faction spawn)
- [x] PetSystem (leveling, rarity-tiered stats, XP sharing, auto-loot, tradable)
- [x] StatEnchantSystem (accessory enchanting, 6-tier rolls, no break risk)
- [x] Mage Double-Cast (hidden instant-cast window, castTime field on SkillDefinition)

### ECS Integration
- [x] Component wrappers for all 20 game systems (game_components.h)
- [x] GameplaySystem (tick StatusEffects, CC, regen, PK decay, respawn)
- [x] MobAISystem (tick MobAI, player scanning, attack wiring)
- [x] CombatActionSystem (TWOM Option B attacks, damage/XP/respawn)
- [x] EntityFactory (createPlayer with all 17+ components, createMob)
- [x] Game loop integration (systems registered, test mobs spawning)
- [x] HUD showing live player stats (HP/MP/Level/Class/XP/Fury)
- [x] HUD target info (name, level, HP when target selected)
- [x] Core gameplay loop (walk → aggro → fight → kill → XP → level up)
- [x] Spatial hash grid for efficient mob-player range queries (128px cells, used by MobAI + Combat)
- [x] Archetype ECS with contiguous SoA storage, swap-and-pop, cached queries
- [x] MobAI unified onto engine SpatialGrid (removed duplicate spatial hash)
- [x] Loot drop pipeline (loot table rolling, ground items, pickup, despawn)
- [x] Boss spawn points (fixed-position, death persistence, respawn timer)
- [x] Starter equipment on character creation (class-specific weapon + shared armor)
- [ ] Event bus for cross-system communication (party XP sharing)
- [ ] Timer/scheduler utility for periodic game events

### UI Systems
- [x] HUD text (HP/MP/XP/Level/Class/Fury stats display + target info)
- [x] Floating Damage Text (white=normal, orange=crit, gray=miss, purple=resist, yellow=XP, gold=level up)
- [x] HUD bars (HP/MP/XP graphical bars at top-center, text shadow, highlight, F1 toggle sync)
- [x] Inventory UI (grid, equipment panel, tooltips, drag-and-drop, stats tab)
- [x] Skill Bar UI (5 slots x 4 pages on right side, K toggle, [/] pages, drag-to-assign, cooldown overlay)
- [x] Skills Tab (learned skills list, drag-to-assign to bar, activate rank with skill points)
- [x] Chat System UI (channel tabs, scrolling text buffer, faction-scoped garbling)
- [x] Mob Nameplates (level-colored by difficulty, HP bars when damaged)
- [x] Player Nameplates (name + level + PK color + guild name)
- [x] Ground loot on mob kill (items + gold as entity type 3, pickup via CmdAction)

### Networking (Custom Proprietary)
- [x] Custom reliable UDP transport layer (Winsock2, 3 channels, ack bitfields, RTT estimation)
- [x] Custom replication system (AOI-driven enter/leave/update, delta compression with field bitmasks, ghost entities)
- [x] Client-server message protocol (binary ByteWriter/ByteReader, 16-byte packet header)
- [x] Server-authoritative game logic (headless 20 tick/sec FateServer)
- [x] Zone-based interest management (AOI visibility sets, hysteresis, enter/leave/stay diffs)
- [x] Client-side position interpolation for ghost entities
- [x] Session tokens, heartbeat/timeout connection management
- [ ] Client-side prediction and server reconciliation
- [x] Authentication and login flow (TLS auth, bcrypt, auth token bridging)
- [ ] Wire up remaining game/shared/ systems to networking messages

### Database Integration
- [x] PostgreSQL via libpqxx (same schema as Unity prototype)
- [x] Connection pooling (min 5, max 50, RAII Guard)
- [x] Definition caches (mob, item, skill, loot table)
- [x] Character persistence (save/load on connect/disconnect)
- [ ] Async queries for non-critical operations
- [ ] Wire up all game/shared/ systems to database layer

### Mobile & Platform
- [ ] iOS build (SDL2 + Xcode)
- [ ] Android build (SDL2 + NDK)
- [ ] Touch input (D-Pad, action button, tap-to-target)
- [ ] Safe area handling (notch, navigation bar)
- [ ] Battery/memory optimization
- [ ] 30 FPS cap on mobile, 60 on PC

### Advanced Engine
- [x] Particle system (CPU emitters, spawn rate/burst, gravity, per-particle lifetime/rotation/color lerp)
- [ ] Shader effects (water, fog, screen flash)
- [x] Spatial grid for efficient entity queries (power-of-two bitshift, zero hash)
- [ ] Object pooling for frequently spawned entities
- [x] Hot-reload for data files (textures, shaders, JSON — file watcher + debounced reload)
- [x] Built-in profiler (Tracy integration, on-demand, named zones, memory tracking)
- [x] Console command system for runtime debugging
- [x] Zone arena memory system (O(1) bulk deallocation on zone unload)
- [x] 7-state chunk lifecycle with ticket system and rate-limited streaming
- [x] Zone snapshots with persistent entity IDs (mob/boss state persists across zone visits)
- [x] Ghost entity scaffold for future seamless zone transitions
- [x] Compile-time component type system (CompId, Hot/Warm/Cold tiers, no RTTI)
- [ ] SIMD intrinsics for spatial queries (future optimization)
- [ ] C++20 coroutines for async chunk I/O (structured for future drop-in)
- [x] Unit test suite (doctest, 120 test cases, 1241 assertions)
- [x] Action map input system (23 actions, input buffering, chat mode switching)
- [x] SDF text rendering (uber-shader, outlined/glow/shadow effects, UTF-8, replaces bitmap font)
- [x] Reflection system (FATE_REFLECT macro, auto-generated JSON serializers)
- [x] Serialization registry (ComponentMetaRegistry, type-erased, forward-compat)
- [x] Component traits (Serializable/Networked/Persistent flags per component)
- [x] Registry-based prefab system (removed 164 lines of hardcoded type checks)
- [x] Registry-based scene save/load (version headers, unknown component preservation)
- [x] Generic reflection-driven inspector fallback
- [x] Asset hot-reload (file watching, generation-based invalidation, debounced reload)
- [x] Allocator visualization (ImGui panels for arena watermarks, pool heat maps, frame timeline)
- [x] Render graph (10-pass declarative pipeline, FBO management, post-processing)
- [x] Editor polish (ImGuizmo translate/scale/rotate gizmos, rotate tool, post-process panel)
- [x] Job system (Win32 fiber-based, 4 workers, 32-fiber pool, lock-free MPMC queue, counter-based suspend/resume)
- [x] Graphics RHI (gfx::Device + CommandList + PSO, GL backend, typed handles, uniform cache)
- [x] 2D Lighting (ambient + point lights, light map FBO, multiplicative composite)
- [x] Post-processing (bloom, vignette, color grading)
- [x] Networking transport (custom reliable UDP, Winsock2, 3 channels, delta compression, entity replication)
- [x] Headless server (20 tick/sec FateServer, session tokens, heartbeat/timeout)
