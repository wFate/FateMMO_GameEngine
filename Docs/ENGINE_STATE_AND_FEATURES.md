# FateEngine - State & Features

## Engine Overview

Custom 2D game engine built in C++ for FateMMO. Designed for mobile-first landscape gameplay with a built-in Unity-style editor for scene building, tile painting, and rapid iteration. All game systems from the Unity/C# prototype have been ported to C++ as server-authoritative logic.

**Tech Stack:** C++23, SDL2, OpenGL 3.3 Core, Dear ImGui (docking), nlohmann/json, stb_image, stb_truetype

**Build System:** CMake with FetchContent (auto-downloads all dependencies)

**Target:** Windows (development), iOS/Android (future), Linux server (future)

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
| Input System | Done | Keyboard, mouse, touch, cardinal direction helper |
| Structured Logging | Done | Timestamped, categorized, console + file output |
| Text Rendering | Done | stb_truetype, TTF font atlas, screen-space drawing |
| Tilemap System | Done | Tiled JSON loader, frustum-culled, collision layers |
| Coordinate System | Done | Tile-based coords (32px grid), pixel-to-tile conversion |
| Spatial Grid (Primary) | Done | Fixed power-of-two grid, bitshift cell lookup (zero hash computation), std::span query results, std::expected error handling |
| Spatial Hash (Fallback) | Done | Mueller-style 128px cells, counting-sort rebuild, for unbounded/sparse regions |
| Memory: Zone Arena | Done | 256 MB virtual reserve per scene, O(1) bulk reset on zone unload |
| Memory: Frame Arena | Done | Double-buffered 64 MB, per-frame temporaries, swap at frame start |
| Memory: Scratch Arenas | Done | Thread-local (2 per thread, 256 MB), Fleury conflict-avoidance, ScratchScope RAII |
| Memory: Pool Allocator | Done | Free-list on arena backing, O(1) alloc/dealloc |
| Zone Snapshots | Done | Persistent entity IDs (64-bit), serialization skeleton for mob/boss state across zone visits |
| Tracy Profiler | Done | On-demand profiling, named zones, frame marks, arena memory tracking |
| Component Registry | Done | Compile-time CompId, Hot/Warm/Cold tier classification, zero-RTTI macros |
| Reflection System | Done | FATE_REFLECT macro, field-level metadata (name, offset, type), auto-generated JSON serializers |
| Serialization Registry | Done | ComponentMetaRegistry maps string names → type-erased serialize/deserialize, aliases for backward compat |
| Component Traits | Done | ComponentFlags (Serializable/Networked/Persistent/EditorOnly), per-component policy via component_traits<T> |
| Generic Inspector | Done | Reflection-driven ImGui widget fallback for any reflected component, manual inspectors take priority |
| Chunk Lifecycle | Done | 7-state machine (Queued→Loading→Setup→Active→Sleeping→Unloading→Evicted), ticket system, rate-limited transitions, double-buffered staging |
| AOI Groundwork | Done | Visibility sets with enter/leave/stay diffs, hysteresis (20% larger deactivation radius) |
| Ghost Entity Scaffold | Done | GhostFlag component, dedicated GhostArena for cross-zone proxy entities |

### Editor (Dear ImGui)
| Feature | Status | Notes |
|---------|--------|-------|
| Toggle with F3 | Done | Auto-pauses game when opened |
| Entity Hierarchy | Done | Grouped by name+tag (collapsible), color-coded (player/ground/obstacle/mob/boss), error badges |
| Inspector Panel | Done | Edit all engine + game component properties live, sprite preview thumbnail, reflection-driven generic fallback |
| Project Browser | Done | Tabs: Sprites, Scripts, Scenes, Shaders, Prefabs |
| Right-Click Context Menus | Done | Open in VS Code, Show in Explorer, Copy Path, Delete |
| Asset Placement | Done | Click sprite thumbnail, click scene to stamp entities |
| Tile Palette | Done | Collapsible panel, load tilesets, scrollable tile grid, paint/drag to place |
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
| Undo/Redo | Done | Ctrl+Z/Ctrl+Y, 200 action history, tracks move/resize/delete/duplicate |
| Tool Modes | Done | W=Move, E=Resize, B=Paint, X=Erase. Active tool highlighted in toolbar |
| Keyboard Shortcuts | Done | Ctrl+Z undo, Ctrl+Y redo, Ctrl+S save, Ctrl+D duplicate, Ctrl+A select all, Delete |
| Eraser Tool | Done | X key, click/drag to delete ground tiles with undo support |
| Layer Visibility | Done | Gnd/Obj toggles in toolbar to show/hide entity layers |
| Log Viewer | Done | In-editor log panel with level filters (DBG/INF/WRN/ERR), text search, color-coded |
| Command Console | Done | Type commands: help, list, count, find, delete, spawn, tp. Results in log viewer |
| Error Badges | Done | Red [!] in hierarchy for entities with missing textures |
| Panel Persistence | Done | ImGui saves window layout to imgui.ini, panels don't steal focus |

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
| NPC Dialogue UI | Done | Click NPC to open, greeting + role buttons, quest accept/decline/complete, branching story dialogue |
| Shop UI | Done | Merchant buy/sell grid, gold display, buy checks player gold |
| Quest Log UI | Done | L key toggle, active quests with objective progress, abandon button, completed section |
| Skill Trainer UI | Done | Lists learnable skills, level/gold/SP requirements, greyed out if not met |
| Bank Storage UI | Done | Deposit/withdraw items and gold, fee display |
| Teleporter UI | Done | Destination list with costs and level requirements |
| D-Pad | Planned | Mobile touch control, bottom-left |
| Action Buttons | Planned | Attack + skill circular buttons, bottom-right |
| Chat UI | Planned | Text input + channel tabs + scrolling messages |

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
| SpriteComponent | Done | Texture, sourceRect (tileset support), spritesheet frames, tint, flip |
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

### Systems
| System | Status | Notes |
|--------|--------|-------|
| MovementSystem | Done | WASD input (local player only), Box+Polygon collision (all combos) |
| AnimationSystem | Done | Timer-based frame updates |
| CameraFollowSystem | Done | Locked to local player, smooth (no pixel-snap jitter) |
| SpriteRenderSystem | Done | Frustum culled, depth sorted |
| GameplaySystem | Done | Ticks StatusEffects, CrowdControl, HP/MP regen, PK decay, respawn, nameplates |
| MobAISystem | Done | Ticks MobAI for all mobs, scans for players, applies movement, fires attacks |
| CombatActionSystem | Done | TWOM Option B targeting, click/touch-to-target, auto-clear off-screen, player attacks, damage text, mob death/XP, same-faction PvP block |
| SpawnSystem | Done | Region-based mob spawning, death detection, respawn timers, zone containment |
| NPCInteractionSystem | Done | Click-to-interact with NPCs, range check, dialogue open/close, click consumption (prevents combat targeting) |
| QuestSystem | Done | Routes mob kills/item pickups/NPC talks to quest progress, event-driven quest marker updates on all NPCs |

### Entity Factory
| Feature | Status | Notes |
|---------|--------|-------|
| createPlayer() | Done | Assembles player with all 21+ components (includes FactionComponent, PetComponent), faction-based spawn position |
| createMob() | Done | Assembles mob with EnemyStats, MobAI, StatusEffects, nameplate, placeholder sprite |
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
| Tileset Loading | Done | Dropdown lists PNGs from assets/tiles/, auto-detects grid |
| Tile Selection | Done | Click tile in palette grid, highlights selected |
| Paint Mode | Done | Click or drag in scene to stamp tiles, auto-snaps to grid |
| Overwrite Detection | Done | Painting over existing ground tile updates it instead of stacking |
| Tileset Persistence | Done | sourceRect saved/loaded in scene JSON, tiles round-trip correctly |

---

## Game Systems (Ported from Unity Prototype)

All 20 game systems from the C#/Unity prototype have been converted to C++ and live in `game/shared/`. Total: **38 files, 6,464 lines**, all compile with zero errors. Systems marked with **(needs net/DB)** have full game logic implemented but contain detailed TODO block comments at the top of each `.cpp` specifying the exact ENet networking and libpqxx database integration points needed.

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

### NPC & Quest System (New — TWOM-Inspired)
| System | Files | Notes |
|--------|-------|-------|
| Quest Manager | `quest_manager.h/.cpp` | Quest progress tracking, accept/abandon/turn-in, 5 objective types (Kill/Collect/Deliver/TalkTo/PvP), max 10 active quests, prerequisite chains |
| Quest Data | `quest_data.h` | Hardcoded quest registry with 6 starter quests, 4 TWOM tiers (Starter/Novice/Apprentice/Adept) |
| NPC Types | `npc_types.h` | NPCTemplate, ShopItem, TrainableSkill, TeleportDestination structs |
| Dialogue Tree | `dialogue_tree.h` | Branching dialogue with enum-based actions (GiveItem/GiveXP/GiveGold/SetFlag/Heal) and conditions (HasFlag/MinLevel/HasItem/HasClass) |
| Bank Storage | `bank_storage.h` | Persistent bank storage with item slots, gold deposit/withdraw, configurable fee |
| Serialization | `register_components.h` | Custom toJson/fromJson for all complex NPC/Quest components (shops, skills, dialogue trees, quest progress, bank storage). NPC entities fully persist across scene save/load and prefab round-trips |

See `Docs/QUEST_AND_NPC_GUIDE.md` for full guide on creating quests and NPCs.

### Game Systems (Ported — Needs Networking & Database Wiring)
| System | Files | Lines | C# Source | Notes |
|--------|-------|-------|-----------|-------|
| Inventory | `inventory.h/.cpp` | 410 | NetworkInventory | 15 slots, equipment map, gold, trade slot locking, stack/swap **(needs net/DB)** |
| Skill Manager | `skill_manager.h/.cpp` | 340 | PlayerSkillManager (2,079L) | Skill learning (skillbook + points), cooldowns, 4x5 skill bar **(needs net/DB)** |
| Party Manager | `party_manager.h/.cpp` | 410 | NetworkPartyManager | 3-player parties, +10%/member XP bonus, loot mode, invites **(needs net/DB)** |
| Guild Manager | `guild_manager.h/.cpp` | 280 | NetworkGuildManager | TWOM guilds, ranks, 16x16 pixel symbols, XP contribution **(needs net/DB)** |
| Friends Manager | `friends_manager.h/.cpp` | 377 | NetworkFriendsManager | 50 friends, 100 blocks, profile inspection, online status **(needs net/DB)** |
| Chat Manager | `chat_manager.h/.cpp` | 120 | NetworkChatManager | 7 channels (Map/Global/Trade/Party/Guild/Private/System), cross-faction garbling on public channels **(needs net/DB)** |
| Trade Manager | `trade_manager.h/.cpp` | 354 | NetworkTradeManager | Two-step security (Lock->Confirm->Execute), 8 item slots + gold **(needs net/DB)** |
| Market Manager | `market_manager.h/.cpp` | 233 | NetworkMarketManager + MarketStructs | Marketplace with jackpot, merchant pass, tax system **(needs net/DB)** |
| Gauntlet | `gauntlet.h/.cpp` | 425 | GauntletConfig + GauntletInstance | Wave survival PvPvE, team scoring, tiebreaker elimination **(needs net/DB)** |
| Faction System | `faction.h` | ~130 | FactionRegistry + FactionChatGarbler | 4 factions (Xyros/Fenor/Zethos/Solis), registry, deterministic chat garbling, same-faction checks |
| Pet System | `pet_system.h/.cpp` | ~120 | PetDefinition + PetInstance + PetSystem | Leveling, rarity-tiered stats (HP/Crit/XP bonus), XP sharing (50%), player-level cap |
| Stat Enchant System | `stat_enchant_system.h` | ~70 | StatEnchantSystem | Accessory enchanting (Belt/Ring/Necklace/Cloak), 6-tier roll table, HP/MP x10 scaling |

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

// PvP damage multiplier
pvpDamage = baseDamage * 0.05
```

---

## Changelog

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

**Pet system (leveling, rarity-tiered stats, auto-loot):**
- PetDefinition (base stats + per-level growth, rarity determines stat quality)
- PetInstance (level, XP, auto-loot toggle, tradable by default)
- PetSystem static utility: effectiveHP/CritRate/ExpBonus, addXP with player-level cap (max 50)
- PetComponent on all players (empty by default, equip pet to activate)
- Pets gain 50% of player XP, cannot outlevel owner

**Stat enchant system (accessory-only enchanting):**
- StatEnchantSystem: 7 scroll types (STR/INT/DEX/VIT/WIS/HP/MP), Belt/Ring/Necklace/Cloak only
- 6-tier outcome table: Fail 25%, +1 30%, +2 25%, +3 12%, +4 6%, +5 2%
- Fail removes existing enchant (risk/reward), new enchant replaces previous, no item break
- HP/MP scrolls use x10 scaling (+10/+20/+30/+40/+50)
- statEnchantType/statEnchantValue fields on ItemInstance

**Mage double-cast mechanic (hidden instant-cast window):**
- SkillDefinition gains castTime, enablesDoubleCast, doubleCastWindow fields
- Casting a double-cast-enabled spell opens a timed window (default 2s)
- Next spell within window is instant cast (zero cast time), mana/cooldown still apply
- No UI indicators — players discover through experimentation
- Window expires on use, timeout, or CC (not frozen by stun/root)

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

Each networking-dependent `.cpp` has a detailed `NOTE: Networking & Database Integration Pending` block comment specifying exact ENet and libpqxx integration points.

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
│   ├── game_components.h        # 18 game logic wrappers
│   ├── faction_component.h      # FactionComponent (player faction)
│   └── pet_component.h          # PetComponent (equipped pet, auto-loot)
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
- Networking-dependent `.cpp` files have `NOTE: Networking & Database Integration Pending` blocks
- All formulas are exact 1:1 matches with the C# prototype (verified line-by-line)

---

## Future Implementation Plan

### Near-Term (Engine Foundation)
- [ ] Audio system (SDL_mixer - music per zone, SFX for combat/UI)
- [ ] Sprite animation testing with real spritesheets
- [x] Scene transitions with loading screen and zone portals
- [ ] Undo/Redo in editor
- [ ] Multi-select and bulk operations in editor
- [ ] Eraser tool for tile painting
- [ ] Auto-load last scene on startup (once real sprites replace procedural)

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
- [ ] Event bus for cross-system communication (loot drops, party XP sharing)
- [ ] Timer/scheduler utility for periodic game events

### UI Systems
- [x] HUD text (HP/MP/XP/Level/Class/Fury stats display + target info)
- [x] Floating Damage Text (white=normal, orange=crit, gray=miss, purple=resist, yellow=XP, gold=level up)
- [x] HUD bars (HP/MP/XP graphical bars at top-center, text shadow, highlight, F1 toggle sync)
- [x] Inventory UI (grid, equipment panel, tooltips, drag-and-drop, stats tab)
- [x] Skill Bar UI (5 slots x 4 pages on right side, K toggle, [/] pages, drag-to-assign, cooldown overlay)
- [x] Skills Tab (learned skills list, drag-to-assign to bar, activate rank with skill points)
- [ ] Chat System UI (channel tabs, scrolling text buffer)
- [x] Mob Nameplates (level-colored by difficulty, HP bars when damaged)
- [x] Player Nameplates (name + level + PK color + guild name)
- [x] Gold drops on mob kill (floating text + inventory integration)

### Networking (Custom Proprietary)
- [ ] ENet UDP transport layer
- [ ] Custom replication system (dirty-flag sync, delta serialization)
- [ ] Client-server message protocol (binary, not JSON)
- [ ] Server-authoritative game logic
- [x] Zone-based interest management (AOI data structures, visibility set diffs, hysteresis)
- [ ] Client-side prediction and server reconciliation
- [ ] Authentication and session management
- [ ] Wire up all game/shared/ systems to networking layer

### Database Integration
- [ ] PostgreSQL via libpqxx (same schema as Unity prototype)
- [ ] Connection pooling
- [ ] Definition caches (mob, item, skill, loot table)
- [ ] Character persistence (save/load)
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
- [ ] Particle system (spell effects, death, level up)
- [ ] Shader effects (water, fog, screen flash)
- [x] Spatial grid for efficient entity queries (power-of-two bitshift, zero hash)
- [ ] Object pooling for frequently spawned entities
- [ ] Hot-reload for data files (JSON, prefabs)
- [x] Built-in profiler (Tracy integration, on-demand, named zones, memory tracking)
- [ ] Console command system for runtime debugging
- [x] Zone arena memory system (O(1) bulk deallocation on zone unload)
- [x] 7-state chunk lifecycle with ticket system and rate-limited streaming
- [x] Zone snapshots with persistent entity IDs (mob/boss state persists across zone visits)
- [x] Ghost entity scaffold for future seamless zone transitions
- [x] Compile-time component type system (CompId, Hot/Warm/Cold tiers, no RTTI)
- [ ] SIMD intrinsics for spatial queries (future optimization)
- [ ] C++20 coroutines for async chunk I/O (structured for future drop-in)
- [x] Unit test suite (doctest, 32 test cases, 740 assertions)
