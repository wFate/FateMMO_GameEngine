# FateEngine — Development Changelog

### March 30, 2026 — Spawn System Overhaul

**Per-Scene Spawn Lifecycle (SceneSpawnCoordinator):**
- New `SceneSpawnCoordinator` (`server/scene_spawn_coordinator.h/.cpp`) manages per-scene `ServerSpawnManager` instances — created when first player enters a scene, torn down when last player leaves
- Replaces hardcoded single-scene `"WhisperingWoods"` initialization — all scenes in `spawn_zones` table now activate dynamically
- Player login calls `onPlayerEnterScene()`, disconnect calls `onPlayerLeaveScene()`, zone transitions call both
- Periodic cleanup of expired death records (every 5 minutes via `cleanupExpiredDeaths()`)

**Consolidated Mob Creation (createMobEntity):**
- New `ServerSpawnManager::createMobEntity()` static method is the single source of truth for server-side mob entity creation
- `MobCreateParams` struct: def, position, level, sceneId, zoneRadius
- Eliminated 3x code duplication: `ServerSpawnManager::createMob()` (now thin wrapper), `dungeon_handler.cpp` `spawnDungeonMobs()`, and `EntityFactory::createMobFromDef()` all consolidated
- `attackCooldown` bug fixed: was `1.5f / attackSpeed` (inverted) in EntityFactory, now direct seconds everywhere

**Death Persistence Wiring (ZoneMobStateRepository):**
- `ZoneMobStateRepository` (existed but was never called) now wired into `ServerSpawnManager` lifecycle
- On mob death: `DeadMobRecord` saved to `zone_mob_deaths` table with wall-clock timestamp
- On scene activate: loads persisted deaths, skips spawning mobs still on cooldown (creates dead trackers with remaining respawn time)
- On scene teardown (`shutdown()`): batch-saves all currently-dead mobs so timers survive server restarts
- Prevents exploit: kill boss → leave scene → re-enter = instant respawn

**Spawn Position Validation:**
- `randomPositionInZone()` now validates candidates against static colliders (`CollisionGrid::isBlockedRect`) and existing mob overlap (48px minimum distance)
- Up to 30 retry attempts per spawn, falls back to last candidate (matches client-side `SpawnSystem` behavior)
- Prevents mobs spawning inside trees, rocks, buildings, or on top of each other

**Zone Shape Support (circle/square per zone):**
- New `zone_shape` column on `spawn_zones` table (VARCHAR(10), default `'circle'`)
- `SpawnZoneRow` gains `zoneShape` field, `SpawnZoneCache` reads it from DB
- Circle zones use rejection sampling (discard candidates outside radius); square zones use axis-aligned bounds
- Migration: `024_spawn_zone_shape.sql`

**EntityFactory Fixes:**
- `createMobFromDef()` now accepts `sceneId` parameter (default `""`) — fixes scene-based AI filtering in `MobAISystem`
- `attackCooldown` standardized to direct seconds (was inverted multiplier formula)

**Dead Code Removal:**
- Deleted `BossSpawnPointComponent` (defined but never referenced) and ~107 lines of dead boss spawn tick logic in `server_app.cpp`
- Removed all references from `register_components.h` and `game_components.h`

**Editor: Hierarchy Right-Click Context Menu:**
- Right-clicking entities in the Hierarchy panel now shows Delete and Duplicate options
- Works for both single entities and grouped entities
- Previously only accessible via Edit menu or Delete key

**Tests:** 11 new spawn system tests (zone shape geometry, collision grid rejection, mob overlap distance, DeadMobRecord timer math)

**Files changed:** `server/scene_spawn_coordinator.h/.cpp` (new), `server/server_spawn_manager.h/.cpp`, `server/server_app.h/.cpp`, `server/handlers/zone_transition_handler.cpp`, `server/handlers/dungeon_handler.cpp`, `server/db/spawn_zone_cache.h/.cpp`, `game/entity_factory.h/.cpp`, `game/register_components.h`, `game/components/game_components.h`, `engine/editor/editor.cpp`, `tests/test_spawn_system.cpp` (new), `Docs/migrations/024_spawn_zone_shape.sql` (new)

### March 30, 2026 — Paper Doll System Revamp, Auth Fix, Engine Hardening

**Paper Doll Catalog System (12 commits):**
- New `PaperDollCatalog` singleton (`game/data/paper_doll_catalog.h/.cpp`) with JSON load/save, texture caching (`rebuildTextureCache`), and per-layer direction-aware sprite resolution
- JSON catalog (`assets/paper_doll.json`) defines body sprites, hairstyles, equipment per gender with front/back/side texture paths, animation metadata (startFrame, frameCount, frameRate, hitFrame, per-layer Y offsets)
- `AppearanceComponent` updated: visual indices replaced with style name strings (`armorStyle`, `weaponStyle`, `hatStyle`, `hairstyleName`) and directional `SpriteSet` textures
- Network protocol changed: server sends `visual_style` strings instead of packed uint16 indices. `SvEntityEnter` and `SvEquipmentUpdate` carry style names. Client resolves via catalog
- Direction-aware render system: catalog lookups for body/hair/equipment per facing direction, per-layer depth offsets from animation metadata
- Character select and creation screens use catalog for sprite preview (replaced hardcoded `equip_visual_table.h` lookups)
- Editor inspector uses catalog dropdowns for hairstyle selection instead of raw index input
- **Paper Doll Manager editor panel**: new `PaperDollPanel` with live composite preview (body+hair+equipment layers), Browse buttons for texture assignment, Save writes to catalog JSON
- `EquipVisualsComponent`, `equip_visual_table.h`, and associated pack/unpack functions removed (dead code after catalog migration)
- **Breaking protocol change:** Client and server must both be updated (style name strings replace visual indices)

**Paper Doll Manager Save Fix:**
- Save button now uses stored absolute `loadedPath_` from catalog load (was writing to a relative path that failed under VS's unpredictable CWD)
- `OFN_NOCHANGEDIR` added to Windows file Browse dialog to prevent CWD hijacking
- Composite preview UV flip fix (GL bottom-up → ImGui top-down)

**CWD Fix at Startup (root cause fix):**
- `game/main.cpp`: `std::filesystem::current_path(FATE_SOURCE_DIR)` at startup ensures all relative paths resolve from the project root
- This was the root cause of multiple path resolution failures — VS sets CWD to an unpredictable location
- `FATE_SOURCE_DIR` compile definition on `fate_engine` library removed (redundant after CWD fix); retained on `FateEngine` exe and `fate_tests`

**Asset Registry Failed-Load Hardening:**
- Failed loads now tracked in a dedicated `failedPaths_` set — subsequent loads of the same bad path return immediately without re-attempting fopen
- Slots are properly freed back to `freeList_` on failure (previous implementation cached failures in `pathToIndex_` which leaked slots permanently)
- `failedPaths_` cleared in `AssetRegistry::clear()` for clean scene transitions

**Auth Reconnect Bug Fix (3 bugs):**
- **`busy_` race condition in `loginAsync()` and `registerAsync()`**: `busy_.store(true)` was set before `cleanup()`, but the old worker thread's teardown called `busy_.store(false)`, stomping the flag. The main thread then saw `!isBusy()` and displayed "Login failed" despite the server confirming success. Fix: moved `busy_.store(true)` to after `cleanup()` returns.
- **Missing `disconnectAuth()` on rejection/disconnect**: When the game server rejected a connection or the client disconnected, the stale auth TLS connection was never torn down. The old auth worker thread remained blocked in `cmdCv_.wait()`. Added `authClient_.disconnectAuth()` to both `onConnectRejected` and `onDisconnected` handlers.
- Symptoms: heartbeat timeout → auto-reconnect with expired token → rejected → user clicks Login → auth server succeeds (3x) → client shows "Login failed — try again" → persistent auth connection closes repeatedly

**Production Build Title:**
- `config.title` now uses `#ifdef FATE_SHIPPING` to select "FateMMO" (shipping) vs "FateMMO Engine" (dev/editor)

**Miscellaneous:**
- `imgui.ini` removed from git tracking, added to `.gitignore` (per-developer layout file)
- `mutable` removed from paper doll texture cache members (no longer needed after cache refactor)
- Redundant `FATE_SOURCE_DIR` path-resolution fallbacks removed from `async_scene_loader.cpp` and `paper_doll_catalog.cpp`
- `tree.png` restored to `assets/sprites/` (was missing, caused 8 invisible tree entities in WhisperingWoods)
- Debug logging added: slot selection shows armor/weapon/hat style strings, character select logs armor catalog lookup results

**Files changed:** `game/main.cpp`, `game/data/paper_doll_catalog.h/.cpp`, `game/game_app.cpp`, `engine/net/auth_client.cpp`, `engine/asset/asset_registry.h/.cpp`, `engine/scene/async_scene_loader.cpp`, `engine/editor/paper_doll_panel.cpp`, `engine/ecs/components/appearance_component.h`, `engine/net/game_messages.h`, `server/server_app.cpp`, `CMakeLists.txt`, `.gitignore`, `assets/paper_doll.json`

### March 28, 2026 — Character Select Screen Overhaul

**CharacterSelectScreen — Full Editor Serialization (70+ properties):**
- Extracted all hardcoded values from `CharacterSelectScreen` render/input code into public serializable properties
- **Layout** (22 props): `slotCircleSize`, `entryButtonWidth`, `slotSpacing`, `slotBottomMargin`, `selectedRingWidth`, `normalRingWidth`, `displayWidthRatio`, `displayHeightRatio`, `displayTopRatio`, `displayBorderWidth`, `nameBgHeight`, `nameBgWidthRatio`, `nameTextY`, `classTextY`, `levelTextY`, `entryBtnBorderWidth`, `swapDeleteScale`, `swapDeleteMargin`, `swapBtnRingWidth`, `deleteBtnRingWidth`, `previewScale`, `previewCenterYRatio`
- **Dialog layout** (9 props): `dialogWidth`, `dialogHeight`, `dialogBorderWidth`, `dialogInputHeight`, `dialogInputPadding`, `dialogInputBorderWidth`, `dialogBtnWidth`, `dialogBtnHeight`, `dialogBtnMargin`
- **Font sizes** (14 props): `nameFontSize`, `classFontSize`, `levelFontSize`, `emptyPromptFontSize`, `plusFontSize`, `slotLevelFontSize`, `entryFontSize`, `swapFontSize`, `deleteFontSize`, `dialogTitleFontSize`, `dialogPromptFontSize`, `dialogRefNameFontSize`, `dialogInputFontSize`, `dialogBtnFontSize`
- **Colors** (32 props): background, display area (bg/border), name (bg/text), class, level, empty prompt, slot colors (empty/filled/selected ring/empty ring/plus/level), entry button (bg/border), swap button (bg/ring), delete button (bg/ring), dialog colors (overlay/bg/border/title/prompt/refName/inputBg/inputBorder/confirmActive/confirmDisabled/confirmDisabledText/cancel)
- Full JSON serialization/deserialization in `ui_serializer.cpp` / `ui_manager.cpp`
- Editor inspector with 6 collapsible TreeNode sections: Layout, Dialog Layout, Fonts, Colors, Dialog Colors — all with undo/redo support

**Absolute Text Positioning:**
- Replaced stacked relative padding (`nameBgTopPad`, `classTopPad`, `levelTopPad`) with absolute Y offsets from display area top (`nameTextY=8`, `classTextY=42`, `levelTextY=60`)
- Each text element (name, class, level) can now be positioned independently in the editor

**Paper Doll Sprite Preview in Character Select:**
- Character select display area now renders the selected character's body + equipment sprites
- 4-layer composition: body (gender/hairstyle combined sprite) → armor → hat → weapon
- `resolvePreviewTextures()` loads textures from `TextureCache` using `equip_visual_table.h` paths, cached per selected slot
- Editor-adjustable `previewScale` (default 3.0) and `previewCenterYRatio` (default 0.55)
- `CharacterSlot` struct extended with `weaponVisualIdx`, `armorVisualIdx`, `hatVisualIdx`

**Equipment Visual Indices in Character Preview Protocol:**
- `CharacterPreview` (auth_protocol.h) extended with 3 `uint16_t` fields: `weaponVisualIdx`, `armorVisualIdx`, `hatVisualIdx` — wire format adds 6 bytes per character
- `AuthServer::buildCharacterList()` now queries `character_inventory` for equipped weapon/armor/hat items and resolves visual indices via `ItemDefinitionCache`
- `ItemDefinitionCache` added to `AuthServer` (initialized on start alongside repos)
- `GameApp::populateCharacterSlots()` passes visual indices from `CharacterPreview` to `CharacterSlot`

**Breaking protocol change:** Client and server must both be updated (CharacterPreview wire format changed).

**Files changed:** `engine/ui/widgets/character_select_screen.h` (70+ properties, equip indices on CharacterSlot, preview texture cache), `engine/ui/widgets/character_select_screen.cpp` (all hardcoded values → properties, resolvePreviewTextures(), paper doll layer rendering), `engine/ui/ui_serializer.cpp` (full property serialization), `engine/ui/ui_manager.cpp` (full property deserialization), `engine/editor/ui_editor_panel.cpp` (6 inspector sections), `engine/net/auth_protocol.h` (CharacterPreview equip indices + wire format), `server/auth/auth_server.h` (ItemDefinitionCache member), `server/auth/auth_server.cpp` (cache init, equip visual query in buildCharacterList), `game/game_app.cpp` (pass equip indices to CharacterSlot)

### March 28, 2026 — Missing Items, Currencies & Consumables

**Stat Allocation Disabled:**
- Stat allocation system disabled (stats are fixed per class, only increased through equipment and collections)
- `freeStatPoints`, `allocatedSTR/INT/DEX/CON/WIS` fields retained at 0 for DB/network compat
- `+5 stat points per level` in `addXP()` commented out
- StatusPanel "+" buttons and allocation UI removed
- Server handler returns "disabled" message
- Sync/persistence paths send 0 values

**New Item Definitions (9 items, migration 020):**
- `fate_coin` — Fate Coin (Consumable): 3 coins = level × 50 XP. Validates quantity >= 3, WAL-logs XP gain
- `soul_anchor` — Soul Anchor (Consumable): auto-consumed on death to prevent XP loss. `shouldPreventXPLoss` callback on `CharacterStats::die()` checks inventory before `applyPvEDeathXPLoss()`
- `battle_token` — Battle Token (Currency): battlefield reward token, stackable to 9999
- `dungeon_token` — Dungeon Token (Currency): dungeon reward token, stackable to 9999
- `guild_token` — Guild Token (Currency): guild activity token, stackable to 9999
- `minor_exp_scroll` — Minor EXP Scroll (Consumable): 10% EXP boost for 1 hour via `EffectType::ExpGainUp`
- `major_exp_scroll` — Major EXP Scroll (Consumable): 20% EXP boost for 1 hour. Same-tier rejects, different tiers stack additively
- `beacon_of_calling` — Beacon of Calling (Consumable): teleports a party member to user's location. Cross-scene support with full zone-transition bookkeeping (AOI clear, threat purge, Aurora buff removal)
- `elixir_of_forgetting` — Elixir of Forgetting (Consumable): skill point reset (subtype `stat_reset`, handler already existed — just needed DB item definition, 11,000 gold)

**Recall Scene Field (data model):**
- Added `recallScene` field to `CharacterStats` (default "Town")
- `recall_scene` VARCHAR(64) column on `characters` table
- Threaded through `CharacterRecord`, `rowToRecord()`, all 3 SELECT queries, `saveCharacter()` UPDATE, persistence handler (both save paths), server login load
- Recall Scroll handler now reads `charStats->stats.recallScene` instead of hardcoded "Town", with fallback validation

**Life Tap Skill Wiring:**
- `mage_life_tap` special case in `combat_handler.cpp`: sacrifices 20% current HP (floor at 1), restores 20%/28%/36% max MP by rank
- CC-blocked, cooldown-tracked, broadcasts `SvSkillResult`

**Network Changes:**
- `CmdUseConsumableMsg` extended with `uint32_t targetEntityId` (default 0) — used by Beacon of Calling
- `sendUseConsumableWithTarget()` added to `NetClient`

**Files changed:** `game/shared/character_stats.h` (recallScene field, shouldPreventXPLoss callback, stat allocation disabled), `game/shared/character_stats.cpp` (die() Soul Anchor check, stat allocation disabled), `engine/net/game_messages.h` (CmdUseConsumableMsg targetEntityId), `engine/net/net_client.h/.cpp` (sendUseConsumableWithTarget), `server/handlers/consumable_handler.cpp` (4 new subtype branches: fate_coin, exp_boost, beacon_of_calling + recallScene update), `server/handlers/combat_handler.cpp` (Life Tap handler), `server/handlers/stat_allocation_handler.cpp` (disabled), `server/handlers/sync_handler.cpp` (send 0s for allocation), `server/handlers/persistence_handler.cpp` (recallScene + allocation zeroed), `server/db/character_repository.h/.cpp` (recall_scene column), `server/server_app.cpp` (recallScene load, Soul Anchor callback hook, stat allocation disabled), `engine/ui/widgets/status_panel.h/.cpp` (allocation UI removed), `game/game_app.cpp` (allocation wiring disabled), `Docs/migrations/020_items_and_recall.sql`

### March 28, 2026 — Costume/Closet System

**Costume System (new feature):**
- Full costume/closet system: players can own, equip, and display cosmetic costumes per equipment slot
- DB schema: `costume_definitions` (def_id, name, display_name, slot_type, visual_index, rarity, source), `player_costumes` (ownership), `player_equipped_costumes` (slot equip state), `show_costumes` toggle on `characters` table
- `CostumeCache` loads all definitions at server startup; `CostumeRepository` (pool-based) handles player persistence (grant, equip, unequip, toggle, load owned/equipped)
- `CostumeComponent` on player entities: owned set, equipped-by-slot map, show toggle
- Entity replication extended to 32-bit field mask (was 16-bit) with `costumeVisuals` on bit 16 — packed/unpacked via `packCostumeVisuals()`/`unpackCostumeVisuals()` helpers
- 3 client→server commands: `CmdEquipCostume`, `CmdUnequipCostume`, `CmdToggleCostumes`
- `SvCostumeSync` (0xBE): full costume state on login (owned IDs + equipped slots + toggle)
- `SvCostumeUpdate` (0xBF): incremental updates (obtained/equipped/unequipped/toggleChanged)
- `costume_handler.cpp`: processEquipCostume, processUnequipCostume, processToggleCostumes, sendCostumeSync, loadPlayerCostumes — all with ownership/slot validation
- `CostumePanel` UI widget: 4-column grid, slot filter tabs, equip/unequip/toggle buttons, rarity-colored borders. 9 editable layout properties. Full inspector + serialization
- MenuTabBar expanded to 11 tabs (added COS tab)

**Costume Definition Cache — Client enrichment (follow-up):**
- `CostumeDefEntry` struct + `SvCostumeDefsMsg` in `game_messages.h` (u16 count + entry loop, same pattern as SvSkillDefsMsg)
- `SvCostumeDefs` (0xC0) packet type
- Server `sendCostumeDefs()` sends all costume definitions from `costumeCache_.all()`, called from `loadPlayerCostumes()` before `sendCostumeSync()`
- Client `onCostumeDefs` callback populates `costumeDefCache_` map in GameApp, re-enriches existing panel entries (handles defs arriving after sync)
- `enrichCostumeEntry()` helper fills displayName, slotType, visualIndex, rarity from cache — called in onCostumeSync, onCostumeUpdate (obtained), and onCostumeDefs
- CostumePanel entries now show correct display names, rarity borders, and slot filtering

**Mob Costume Drops (follow-up):**
- `mob_costume_drops` DB table (migration 019): mob_def_id + costume_def_id + drop_chance, PK on both
- `CostumeCache` extended with `MobCostumeDrop` struct, `mobDrops_` map, `loadMobDrops()`, `getMobDrops()` — loaded at server startup alongside definitions
- `mob_death_handler.cpp`: after loot table roll, iterates mob's costume drops, rolls against drop_chance per entry, skips if player already owns, grants via `costumeRepo_->grantCostume()` + `SvCostumeUpdate(obtained)` + system chat notification

**Collection Costume Rewards (follow-up):**
- `collection_handler.cpp`: after `markCompleted`, checks if `rewardType == "Costume"` — grants costume via `costumeRepo_->grantCostume(characterId, conditionTarget)` + sends `SvCostumeUpdate(obtained)`. Chat notification adapted for costume vs stat rewards. Uses `conditionTarget` field as the costume_def_id

**Shop Costume Purchases (follow-up):**
- `shop_handler.cpp`: before normal item flow, checks if `def->itemType == "Costume"` — grants costume via `costumeRepo_->grantCostume()` + `SvCostumeUpdate(obtained)` instead of adding to inventory. Skips inventory dirty/persist. Uses `itemId` as the costume_def_id

**Files changed:** `engine/net/game_messages.h` (CostumeDefEntry, SvCostumeDefsMsg, SvCostumeSyncMsg, SvCostumeUpdateMsg, CmdEquipCostumeMsg, CmdUnequipCostumeMsg, CmdToggleCostumesMsg), `engine/net/packet.h` (SvCostumeSync 0xBE, SvCostumeUpdate 0xBF, SvCostumeDefs 0xC0), `engine/net/net_client.h/.cpp` (onCostumeSync, onCostumeUpdate, onCostumeDefs dispatch, sendEquipCostume, sendUnequipCostume, sendToggleCostumes), `engine/net/replication.h/.cpp` (32-bit fieldMask, costumeVisuals bit 16), `engine/net/protocol.h` (fieldMask widened), `engine/ui/widgets/costume_panel.h/.cpp` (CostumePanel widget), `engine/ui/widgets/menu_tab_bar.h` (COS tab), `engine/editor/ui_editor_panel.cpp` (CostumePanel inspector), `game/components/game_components.h` (CostumeComponent), `game/register_components.h` (CostumeComponent registration), `game/entity_factory.cpp` (CostumeComponent on player entities), `game/shared/game_types.h` (packCostumeVisuals/unpackCostumeVisuals), `game/game_app.h` (costumeDefCache_, enrichCostumeEntry), `game/game_app.cpp` (onCostumeDefs, enriched onCostumeSync/onCostumeUpdate, CostumePanel wiring), `server/cache/costume_cache.h` (CostumeCache + MobCostumeDrop + loadMobDrops/getMobDrops), `server/db/costume_repository.h/.cpp` (CostumeRepository), `server/handlers/costume_handler.cpp` (equip/unequip/toggle/sync/load/sendCostumeDefs), `server/handlers/mob_death_handler.cpp` (costume drop wiring), `server/handlers/collection_handler.cpp` (Costume reward type), `server/handlers/shop_handler.cpp` (costume purchase path), `server/server_app.h/.cpp` (costume handler declarations, cache loading, costume drop loading), `Docs/migrations/018_costume_system.sql`, `Docs/migrations/019_mob_costume_drops.sql`

### March 27, 2026 — Collection System, Stat Allocation, Consumables & Full UI Wiring

**Collection System (new feature):**
- DB-driven collection/achievement system with 3 categories (Items/Combat/Progression) and 30 seeded definitions
- `collection_definitions` + `player_collections` DB tables
- `CollectionCache` loads definitions at server startup; `CollectionRepository` handles player persistence
- `CollectionComponent` on player entities stores completed IDs + cached `CollectionBonuses` (11 stat types: STR/INT/DEX/CON/WIS/MaxHP/MaxMP/Damage/Armor/CritRate/MoveSpeed)
- Bonuses stack additively with no cap, integrated into `recalculateStats()` alongside equipment and allocated stat bonuses
- Server checks completion at 9 event trigger points: TotalMobKills, KillBoss, ReachEnchant, OwnItemRarity, ReachLevel, WinArena, WinBattlefield, CompleteDungeon, JoinGuild, LearnSkills
- `SvCollectionSync` (0xBC) + `SvCollectionDefs` (0xBD) packets
- `CollectionPanel` UI widget — menu tab "COL", 3 category tabs, scrollable entries with completion icons + reward badges. 9 editable properties (5 floats + 4 colors with full ColorEdit4). Full inspector + serializer + parseNode support

**Stat Point Distribution (new feature):**
- 5 free stat points granted per level-up, manual allocation to STR/INT/DEX/CON/WIS
- `CmdAllocateStat` (0x37) packet + `stat_allocation_handler.cpp` server handler
- StatusPanel shows "Free Points: N" with "+" buttons next to each stat
- 6 new DB columns (free_stat_points, allocated_str/int/dex/con/wis) with graceful pre-migration fallback
- `sendAllocateStat()` NetClient method

**Oblivion Potion (new consumable):**
- `StatReset` subtype in consumable handler — resets all activated skill ranks to 0, refunds all spent skill points
- Preserves unlocked ranks from skillbooks (only resets activated/spent, not learned)
- `SkillManager::resetAllSkillRanks()` method + passive bonus recalculation

**Recall Scroll (new consumable):**
- `TownRecall` subtype in consumable handler — teleports player to Town spawn point
- Blocked during combat, arena, battlefield, dungeon; cancels active trades; purges mob threat tables
- Inline zone transition (bypasses portal proximity checks)

**Town NPCs expanded to 9:**
- Added Marketplace NPC "Veylan" (npcId 1007, MarketplaceNPCComponent)
- Added Quest NPC "Elder" (npcId 1008, QuestGiverComponent)
- Added Leaderboard NPC "Keeper" (npcId 1009, LeaderboardNPCComponent)
- Fixed Arena Master Kael — added missing ArenaNPCComponent
- Fixed Battlefield Herald Thane — added missing BattlefieldNPCComponent

### March 27, 2026 — Editor/Game Input Fixes, Dangling Pointer Fix, Skill System Hardening & Skill Panel Drag-Drop

**Bug fix: Editor viewport blocking game keyboard input (`app.cpp`)**
- **Root cause:** Changing the keyboard guard from `WantTextInput` to `WantCaptureKeyboard` fixed the inspector (drag/slider widgets could capture keyboard), but `WantCaptureKeyboard` is true whenever ANY ImGui window is focused — including the game viewport panel. Clicking on the viewport to play made ImGui claim the keyboard, blocking login fields, chat, and all game UI text input.
- **Fix:** Added `!Editor::instance().isViewportHovered()` to both the keyboard guard and the mouse/focus guard. When the viewport is hovered, keyboard and mouse input flow through to the game UI. When an actual editor panel (inspector, hierarchy) is active, ImGui properly consumes input.
- **Fix 2:** KEY_UP events now always forward to `Input::processEvent()` even when ImGui captures keyboard outside the viewport — prevents stuck movement keys when cursor leaves the game viewport during play.

**Bug fix: Dangling pointer in SkillManager stats (`entity_factory.cpp`, `server_app.cpp`)**
- **Root cause:** The archetype-based ECS moves all component data on every `addComponent()` call. `EntityFactory::createPlayer` captured a pointer to `CharacterStatsComponent` early, stored it in `SkillManager::initialize()`, then added ~14 more components — each migration invalidated the pointer. `SkillManager::stats` pointed to freed memory, causing `stats->className` to read as empty string. This silently broke all class-gated skill checks (learnSkill rejected Mage skills because `"Mage" != ""`).
- **Fix 1 — `entity_factory.cpp`:** Moved `initialize()` call to after the last `addComponent()`, using fresh `getComponent()` pointers.
- **Fix 2 — `server_app.cpp`:** Added explicit `skillComp->skills.initialize(&charStatsComp->stats)` re-link during player login as a safety net.

**Bug fix: learnSkill empty className check (`skill_manager.cpp`)**
- `learnSkill`'s class check only treated `"Any"` as no-restriction. Skills with empty `class_required` (loaded as `""` from NULL DB values) passed the handler's check but failed inside `learnSkill`. Added `!def->className.empty()` guard for consistency.

**Bug fix: Hit roll miss callback (`skill_manager.cpp:573,583`)**
- Resist/miss paths in `executeSkill` called `onSkillUsed` instead of `onSkillFailed`. The combat handler hooks `onSkillFailed` to set the `wasMiss` flag for `HitFlags::MISS`. Fixed both paths to call `onSkillFailed` with descriptive reasons ("Spell resisted", "Attack missed").

**Bug fix: Destroy button blocked by inventory panel (`game_app.cpp`)**
- **Root cause:** The destroy confirm dialog lives in `fate_hud` screen, but the inventory panel in `fate_menu_panels` (loaded later = checked first) always returns true from `onPress()`, consuming clicks before they reach the dialog.
- **Fix:** Inventory panel is now disabled (`setEnabled(false)`) when the destroy dialog appears, and re-enabled on confirm/cancel.

**Skill bar auto-assign moved from learn to activate (`skill_manager.cpp`)**
- `autoAssignToSkillBar()` previously ran inside `learnSkill()`, placing skills on the bar at rank 0 (learned but no points spent). Now runs inside `activateSkillRank()` on first activation (rank 1). Skills must have at least 1 skill point spent to appear on the skill bar.

**SkillPanel drag-and-drop skill assignment:**
- **Drag:** Press and hold on an activated skill (currentLevel > 0) in the skill list → starts drag. Orange circle with skill name follows cursor. Non-activated skills retain click-to-select / double-click-to-level-up behavior.
- **Drop:** Release over a wheel slot → assigns skill to that global slot via `onAssignSkill(skillId, globalSlot)` → sends `CmdAssignSkillSlot` (action=0) to server.
- **Unequip:** Click an occupied wheel slot → sends `CmdAssignSkillSlot` (action=1, clear) to server.
- **Slot display:** Wheel slots now show assigned skill names (truncated to fit) instead of just numbers. Slots highlight gold when a drag hovers over them.
- **Data flow:** `skillBarSlots` and `skillBarNames` vectors (size 20) populated per frame from `SkillManager::getSkillInSlot()` + `ClientSkillDefinitionCache::getSkill()`. Assignments sync to SkillArc on the HUD.
- New members: `draggingSkillIndex_`, `dragPos_`, `lastSlotHits_`, `skillBarSlots`, `skillBarNames`, `onAssignSkill` callback.
- Overrides: `onDragUpdate()`, `onRelease()` (UINode virtual methods).

**Diagnostic logging added to learnSkill (`skill_manager.cpp`)**
- All rejection paths now LOG_WARN with specific reason: rank out of range, class mismatch (shows both class names), level too low (shows required vs actual), already at rank, rank skip, new skill must start at rank 1.

**Consume result error messages improved (`consumable_handler.cpp`)**
- Catch-all "Must learn previous rank first" replaced with specific messages: "Already learned this rank", "Must learn previous rank first" (only when skill exists at lower rank), "Must learn rank I first" (bookRank > 1 with no existing skill), "Cannot learn this skill" (generic fallback).

**Files changed:** `engine/app.cpp` (viewport-aware input routing, key-UP forwarding), `game/entity_factory.cpp` (initialize at end), `server/server_app.cpp` (stats re-link on login), `game/shared/skill_manager.cpp` (empty className, miss callbacks, auto-assign move, diagnostic logging), `server/handlers/consumable_handler.cpp` (error messages), `engine/ui/widgets/skill_panel.h` (drag state, slot data, onAssignSkill), `engine/ui/widgets/skill_panel.cpp` (drag-drop, unequip, slot names), `game/game_app.cpp` (onAssignSkill wiring, destroy dialog enable/disable, onConsumeResult, skill arc logging)

### March 27, 2026 — UI Wiring: NPC Panels, Inventory Actions, Town NPCs & Trade Initiation

**New NPC components (3):**
- `ArenaNPCComponent` — Arena NPC marker (FATE_COMPONENT_COLD, registered, editor menu, EntityFactory)
- `BattlefieldNPCComponent` — Battlefield NPC marker (same pattern)
- `DungeonNPCComponent` — Fixed missing FATE_REFLECT, added to editor menu and EntityFactory

**New NetClient send methods (8):**
- `sendEnchant(inventorySlot, useProtectionStone)`, `sendRepair(inventorySlot)`, `sendExtractCore(itemSlot, scrollSlot)`, `sendCraft(recipeId)`, `sendSocketItem(equipSlot, scrollItemId)`, `sendArena(action, mode)`, `sendBattlefield(action)`, `sendPetCommand(action, petDbId)`

**New UI widgets (5 + 1 bonus):**
- `ArenaPanel` — NPC-triggered arena registration (Solo 1v1 / Duo 2v2 / Team 3v3), register/unregister. Editable: titleFontSize, bodyFontSize, buttonHeight, buttonSpacing
- `BattlefieldPanel` — NPC-triggered battlefield registration, register/unregister. Editable: titleFontSize, bodyFontSize, buttonHeight
- `PetPanel` — Menu tab pet management (equip/unequip, pet list, stats). Editable: titleFontSize, nameFontSize, statFontSize, portraitSize, buttonHeight
- `CraftingPanel` — Menu tab crafting (recipe list, ingredient grid, craft button). Editable: titleFontSize, recipeFontSize, slotSize, resultSlotSize, ingredientColumns
- `PlayerContextMenu` — Floating popup on player tap in safe zones. Trade (faction-gated), Party Invite, Whisper. Editable: menuFontSize, itemHeight, menuWidth
- `LeaderboardPanel` (bonus) — NPC-triggered leaderboard display
- All 6 widgets have full inspector support, serialization (ui_serializer.cpp), deserialization (ui_manager.cpp parseNode), and JSON screen entries

**Inventory enhancements:**
- Context menu on item tap: Equip/Enchant/Repair/Extract Core/Destroy for equipment; Use/Destroy for consumables
- Drag-drop enchant scroll onto equipment triggers `sendEnchant`
- Drag-drop extraction scroll triggers `sendExtractCore`
- Drag-drop socket scroll onto equipped accessory triggers `sendSocketItem`
- New callbacks: `onEnchantRequest`, `onRepairRequest`, `onExtractCoreRequest`, `onSocketRequest`

**NpcDialoguePanel extensions:**
- Added `hasArena` + `hasBattlefield` flags
- Added `onOpenArena` + `onOpenBattlefield` callbacks
- Arena and Battlefield buttons render in dialogue when NPC has the corresponding component

**Result handler wiring (8 new):**
- `onEnchantResult`, `onRepairResult`, `onExtractResult`, `onCraftResult`, `onSocketResult` — chat messages
- `onPetUpdate` — PetPanel state + chat
- `onArenaUpdate` — ArenaPanel state + chat
- `onBattlefieldUpdate` — BattlefieldPanel state + chat

**Town scene NPCs placed (6):**
- Merchant Elara (npcId 1001) — ShopComponent with 7 items
- Banker Gideon (npcId 1002) — BankerComponent
- Teleporter Mira (npcId 1003) — TeleporterComponent
- Arena Master Kael (npcId 1004) — ArenaNPCComponent
- Battlefield Herald Thane (npcId 1005) — BattlefieldNPCComponent
- Dungeon Guide Voss (npcId 1006) — DungeonNPCComponent

**Trade initiation:**
- PlayerContextMenu appears on player entity tap
- Same-faction + safe-zone check for trade eligibility
- Sends `TradeAction::Initiate` with character name
- Ghost entity faction now set from `SvEntityEnter` (was `Faction::None` before)

**MenuTabBar expanded:** 7 tabs -> 9 tabs (added PET and CRAFT)

### March 27, 2026 — TestScene Removal, Async Loader Cleanup & Editor Input Fix

**Removed TestScene and redundant scene auto-load:**
- Removed the `TestScene` scene factory and its 4 dead creation functions (`createPlayer`, `createTestEntities`, `spawnTestMobs`, `spawnTestNPCs`) — ~500 lines of unreachable code. The factory's conditional (`!fs::exists("WhisperingWoods.json")`) was always false, so it never created entities.
- Replaced with a no-op `Default` scene factory — provides an empty World for systems to attach to at startup. Scene data is now loaded exclusively by `AsyncSceneLoader` after server connect.
- Removed the editor auto-load of `WhisperingWoods.json` at startup. Previously the scene was loaded twice: once by the auto-load (651 entities) and again by the async loader after connect (destroy 651 + recreate 651). Now entities are loaded once.
- Updated `WhisperingWoods.json` metadata: `sceneName` changed from `"TestScene"` to `"WhisperingWoods"`.
- Removed unused `stb_image_write.h` include from `game_app.cpp`.

**Editor input routing hardened (recurring bug fix):**
- **Root cause:** Input routing between ImGui (editor panels) and retained-mode game UI (chat, inventory, etc.) was split across 3 locations with inconsistent checks. Every new widget addition could shift the focus balance.
- **Fix 1 — `editor.cpp`:** `wantsKeyboard_`/`wantsMouse_` was captured from `ImGui::GetIO()` *after* `NewFrame()`, which always returned false since no widgets existed yet. Moved capture to *before* `NewFrame()` so it reflects the previous frame's widget focus state (how ImGui is designed to work).
- **Fix 2 — `app.cpp` keyboard routing:** Changed `WantTextInput` guard to `WantCaptureKeyboard`. `WantTextInput` only covers ImGui `InputText` fields; `WantCaptureKeyboard` covers all widget types (DragFloat, DragInt, Checkbox, Slider, etc.). This was why inspector DragFloat fields appeared "locked" — keyboard events were still going to the chat panel.
- **Fix 3 — `app.cpp` mouse routing:** When `Editor::wantsMouse()` is true, now calls `uiManager_.clearFocus()` instead of just skipping `handleInput()`. Previously, `focusedNode_` from a prior chat interaction would persist and steal keyboard events the next frame when `wantsMouse()` flickered false.
- **New:** `UIManager::clearFocus()` method — explicitly unfocuses the current game-UI node (fires `onFocusLost`, nulls `focusedNode_`).

**Files changed:** `game/game_app.cpp` (TestScene removal + auto-load removal), `game/game_app.h` (4 declarations removed), `assets/scenes/WhisperingWoods.json` (sceneName metadata), `engine/editor/editor.cpp` (wantsKeyboard_ timing), `engine/app.cpp` (WantCaptureKeyboard + clearFocus), `engine/ui/ui_manager.h` (clearFocus method)

### March 27, 2026 — Skill Book Bug Fix & UI Serialization Overhaul

**Bug fix: Skill book double-click broken (`inventory_panel.cpp`)**
- Root cause: tooltip dismiss logic (lines 607-619) cleared `tooltipSlotIndex` to -1 **before** the "second tap = use item" check at line 660, making the condition `tooltipSlotIndex == slot` always false. The `onUseItemRequest` callback was completely unreachable — no consumable items could be used from the inventory.
- Fix: moved second-tap detection into the tooltip-visible block, checking `hitTestGridSlot()` against the saved `tooltipSlotIndex` before the dismiss clears it.

**Bug fix: `onConsumeResult` not wired in `game_app.cpp`**
- The server sent `SvConsumeResult` (success/error messages like "Learned Flame Burst rank 1" or "Wrong class for this skill"), but the client had no handler. All consume feedback was silently dropped.
- Wired `netClient_.onConsumeResult` to display `[Item]` messages in ChatPanel (with pending queue for pre-connect messages).

**ChatPanel full serialization (31 new properties):**
- **7 channel sender colors** (All, Map, Global, Trade, Party, Guild, Private/System) — previously hardcoded in `tabColor()` and two duplicate lambdas in full-panel + idle overlay render
- **5 faction sender name colors** (None, Xyros, Fenor, Zethos, Solis) — previously hardcoded in `addMessage(uint8_t faction)` overload
- **2 message text colors** (messageTextColor, messageShadowColor)
- **3 close button colors** (background, border, icon)
- **6 input bar colors** (bar background, field background, border, border focused, channel button background, placeholder text)
- **3 font/layout floats** (channelLabelFontSize, messageLineSpacing, messageShadowOffset)
- All 31 properties: serialized in `ui_serializer.cpp`, deserialized in `ui_manager.cpp`, inspector controls in `ui_editor_panel.cpp` (7 TreeNode groups: Layout, Font Sizes, Channel Colors, Faction Colors, Message Colors, Close Button, Input Bar Colors)

**InventoryPanel tooltip + panel serialization (27 new properties):**
- **7 tooltip layout floats** (width, padding, slot offset, shadow offset, line spacing, border width, separator height)
- **3 tooltip font sizes** (name, stat, level requirement)
- **6 tooltip colors** (background, border, shadow, stat text, separator, level requirement)
- **5 rarity colors** (Common, Uncommon, Rare, Epic, Legendary) — previously hardcoded in file-local `rarityColor()` static, now instance method `getRarityColor()` reading from members
- **2 panel colors** (background, border) — previously hardcoded in main render
- All 27 properties: serialized, deserialized, inspector controls (5 TreeNode groups: Panel Colors, Tooltip Layout, Tooltip Fonts, Tooltip Colors, Rarity Colors)

**Files changed:** `engine/ui/widgets/chat_panel.h` (31 new members), `engine/ui/widgets/chat_panel.cpp` (all hardcoded colors replaced), `engine/ui/widgets/inventory_panel.h` (27 new members + `getRarityColor()`), `engine/ui/widgets/inventory_panel.cpp` (all tooltip/panel hardcoded values replaced), `engine/ui/ui_serializer.cpp` (58 new serialization lines), `engine/ui/ui_manager.cpp` (48 new deserialization lines), `engine/editor/ui_editor_panel.cpp` (ChatPanel inspector restructured into 7 tree nodes, InventoryPanel inspector gains 5 tree nodes), `game/game_app.cpp` (`onConsumeResult` wiring + `onUseItemRequest` fix)

### March 27, 2026 — Skill System End-to-End Wiring

**6 network gaps closed to make skills testable:**
- `SvSkillSyncMsg` now includes `availablePoints`, `earnedPoints`, `spentPoints` (int16) — client no longer preserves stale zeroes on connect
- New `SvSkillDefs` packet (0xBB) — server sends all class skill definitions on login, client populates `ClientSkillDefinitionCache` via `applySkillDefs()` with pending replay support
- New `CmdActivateSkillRank` packet (0x35) — client requests spending a skill point; server validates, calls `activateSkillRank()`, persists, re-syncs
- New `CmdAssignSkillSlot` packet (0x36) — client requests skill bar assign/clear/swap; server validates slot bounds, persists, re-syncs
- Skillbook consumption wired in `consumable_handler.cpp` — SkillBook subtype reads `skill_id`/`rank` from item attributes, validates class+level, calls `learnSkill()`, consumes item, re-syncs both skill and inventory
- Item use (consumables) wired in inventory panel — second tap on same inventory slot triggers `onUseItemRequest` → `sendUseConsumable()` (was completely missing, no way to use any consumable)

**New server handler file:** `server/handlers/skill_handler.cpp` — `processActivateSkillRank` and `processAssignSkillSlot`

**Level-up grants skill points:** `onLevelUp` callback now calls `grantSkillPoint()` + persists + re-syncs (was missing — level-ups never awarded skill points)

**New GM command:** `/addskillpoints <player> <count>` — grants skill points for testing (retroactive fix for existing characters)

**SkillPanel UI overhaul:**
- Shows ALL class skills from `ClientSkillDefinitionCache` (was showing only learned skills)
- Three-state rank dots: orange (activated), yellow (unlocked but not activated), grey (locked)
- Skill names readable (font 9, auto-trim to cell width; was 5-char truncation at font 7)
- Second tap on selected skill triggers level-up via `sendActivateSkillRank()`
- 18 configurable properties extracted from hardcoded values: layout (splitRatio, gridColumns, circleRadiusMul, dotSize, dotSpacing), font sizes (title, header, name, tab, points), 13 colors (backgrounds, rings, dots, names, badge)
- Full serialization/deserialization in `ui_serializer.cpp` / `ui_manager.cpp` — Ctrl+S saves all properties to JSON
- Inspector expanded with TreeNode groups: Layout, Font Sizes, Skill Colors, Dot Colors, Text Colors, Runtime State (with skill table)

**Tests:** 6 new tests in `test_skill_wiring.cpp` (105 assertions) — message round-trips (SvSkillSync, CmdActivateSkillRank, CmdAssignSkillSlot, SvSkillDefs), SkillManager integration, ClientSkillDefinitionCache

### March 27, 2026 — Admin Command System Overhaul

**AdminRole enum & command framework:**
- Added `AdminRole` enum (`Player=0`, `GM=1`, `Admin=2`) to replace raw ints across the codebase
- `GMCommand` struct extended with `category`, `usage`, `description` metadata fields
- `GMCommandRegistry` gains `getCommandsByRole(AdminRole)` — returns commands sorted by category then name
- All 10 existing commands migrated from brace-init to explicit metadata registration
- `/admin` help command (GM+) — lists available commands grouped by category, filtered by caller's role

**Ban system wired to database:**
- `/ban <player> <minutes> [reason]` — now writes `is_banned`, `ban_reason`, `ban_expires_at` to DB (was kick-only)
- `/permaban <player> [reason]` — permanent ban (NULL expiry) to DB
- `/unban <player>` — clears ban in DB (was a no-op)
- Auth login check now respects `ban_expires_at` — expired bans auto-cleared on login attempt, active bans show remaining time
- `AccountRepository` gains `setBan()`, `clearBan()`, `clearBanByUsername()`, `setAdminRole()` methods (JOIN through characters table by character name)

**Mute system (new):**
- `/mute <player> <minutes> [reason]` — blocks all chat channels for duration (GM+)
- `/unmute <player>` — removes mute early (GM+)
- In-memory `MuteInfo` (expiry + reason) per client, auto-expires, cleared on disconnect
- Mute check inserted after GM command parsing — GM commands always work regardless of mute

**New admin commands:**
- `/spawnmob <mobDefId> [count]` — spawns 1-10 mobs at caller's position from MobDefCache (Admin)
- `/goto <scene> <x> <y>` — teleport to arbitrary coordinates in any scene (GM+)
- `/whois <player>` — shows level, class, role, scene, position, HP/MP, gold, IP, clientId (GM+)
- `/setrole <player> <player|gm|admin>` — promote/demote with escalation guards (Admin)
- `/invisible` — toggle, hides entity from non-staff clients via replication filter (GM+)
- `/god` — toggle, blocks all damage (PvP auto-attack, skills, mob attacks) (GM+)

**Invisibility system:**
- `ReplicationManager` gains `visibilityFilter` callback — checked per-entity in `buildVisibility()`
- `invisibleEntities_` set on ServerApp — staff clients (GM+) can still see invisible entities
- State cleared on disconnect

**God mode system:**
- `godModeEntities_` set on ServerApp — damage blocked at 3 sites:
  - PvP auto-attack path in `combat_handler.cpp`
  - Skill damage path in `combat_handler.cpp`
  - Mob-to-player path via `shouldBlockDamage` callback on `MobAISystem`
- Blocked attacks reported as misses (0 damage) to clients
- State cleared on disconnect

**Command categories for `/admin` output:**

| Category | Commands |
|---|---|
| Economy | `/addgold`, `/additem`, `/setlevel` |
| GM Tools | `/announce`, `/dungeon`, `/god`, `/invisible` |
| Help | `/admin` |
| Player Management | `/ban`, `/kick`, `/mute`, `/permaban`, `/setrole`, `/unban`, `/unmute`, `/whois` |
| Spawning | `/spawnmob` |
| Teleportation | `/goto`, `/tp`, `/tphere` |

**Tests:** 24 GMCommands tests (AdminRole enum, permission checks, metadata, category sorting, role filtering)

### March 26, 2026 — Chat Panel TWOM Restyle, Edge Case Hardening, Serialization Audit

**Chat panel restyle (TWOM-style):**
- Full panel mode: messages float over game world with drop shadows (no dark background by default), game visible behind
- Tab bar replaced with channel selector button on the left of the input bar — tap to cycle channels (All→Map→Glb→Trd→Pty→Gld→PM), skips unavailable channels
- Panel expands/collapses on open/close: `setFullPanelMode` saves original anchor size, applies `fullPanelWidth`/`fullPanelHeight`, restores on close
- Anchor synced live with inspector values so Full Panel Width/Height changes apply immediately
- Message area clicks pass through to game world (only input bar and close button consume)
- Background color uses `resolvedStyle_.backgroundColor` — set alpha > 0 in Style section for semi-transparent bg, 0 for fully transparent
- Close button X font scales proportionally with `closeBtnSize`
- Idle overlay font uses `messageFontSize` (was hardcoded 11pt)

**Chat panel — 10 inspector-editable properties (all serialized):**
- `fullPanelWidth` (default 1000), `fullPanelHeight` (default 350)
- `inputBarHeight` (default 28), `inputBarWidth` (0 = match panel)
- `channelBtnWidth` (default 44), `channelBtnHeight` (0 = match bar)
- `closeBtnSize` (default 20), `messageFontSize` (default 11), `inputFontSize` (default 11)
- `chatIdleLines` (default 3)

**Destroy item race condition fix:**
- `CmdDestroyItemMsg` now includes `expectedItemId` — server verifies the item in the slot still matches before destroying
- On mismatch (inventory resynced between dialog show and confirm), server logs warning and resyncs client instead of destroying wrong item
- `onDestroyItemRequest` callback passes (slot, itemId, displayName) instead of just (slot, displayName)
- Dialog stacking: dragging another item out updates all fields (slot, itemId, message) correctly

**Stale drag state fix:**
- `InventoryPanel::render()` clears all drag state (`isDragging_`, source slot/equip, item ID) when panel is not visible
- Prevents stale drag state from persisting when panel is closed mid-drag (e.g., Escape key)

**SkillArc test fix:**
- Hit test used hardcoded `{0,0}` for attack button position — now reads `arc.attackOffset` so test survives layout changes

**Hot-reload save loop fix:**
- `UIManager::suppressHotReload(1.5s)` called after Ctrl+S and "Save Screen" button — prevents hot-reload from destroying the screen tree that was just saved
- Previously, save → hot-reload detect → reload screen → destroy tree → dangling pointers → chat panel broken
- Suppress timer drains pending file changes so they don't fire after the window expires

**Screen reload infinite loop fix:**
- `retainedUILoaded_` block now checks `if (!ui.getScreen(...))` before calling `loadScreen` — only loads screens that haven't been loaded yet
- Previously, hot-reload set `retainedUILoaded_ = false` → next frame reloaded ALL screens → triggered reload listener → set `retainedUILoaded_ = false` again → infinite loop every frame

**ImGui keyboard routing fix:**
- `app.cpp` now checks `ImGui::GetIO().WantTextInput` before routing keyboard events to the game
- Prevents game action map from consuming number keys while typing in ImGui inspector input fields
- Uses `WantTextInput` (not `WantCaptureKeyboard`) so login screen and game keyboard still work normally

**Inline style serialization:**
- `UISerializer` now saves inline style overrides: backgroundColor, borderColor, textColor, borderWidth, fontSize, opacity
- `UIManager` loads inline overrides from JSON and merges them on top of theme styles in `applyThemeStyles`
- Previously, editing Background Color in the inspector was lost on save — only `styleName` was persisted

**Serialization audit fixes:**
- `SkillPanel.activeSetPage` — was serialized but never loaded (reverted to 0 on reload). Now loaded.
- `CharacterSelectScreen.selectedSlot` — never serialized or loaded. Now saved and loaded.
- Full audit verified 18+ widget types with 100+ properties are fully synced across inspector/serializer/loader

**`engine/ui/` gitignored:**
- 83 files untracked from git — entire `engine/ui/` directory (core + widgets) kept local

### March 26, 2026 — Drag Cursor, Destroy Item, ResourceType::None Fix

**Drag cursor (TWOM-style):**
- Items now follow the cursor while dragging between inventory and equipment slots
- New `UINode::onDragUpdate(const Vec2& localPos)` virtual hook, called every frame while a widget is pressed — UIManager dispatches after existing panel/window drag handling
- `InventoryPanel::renderDragCursor()` draws a floating slot box (rarity border, item letter, quantity badge) at the cursor position at depth above tooltips
- `dragCursorPos_` initialized on press, updated each frame via `onDragUpdate`

**Destroy item (drag outside inventory):**
- Dragging an inventory item outside the panel bounds triggers a "Destroy [item]?" confirmation dialog
- New `CmdDestroyItem` packet (0x34) with `CmdDestroyItemMsg` (slot index)
- `NetClient::sendDestroyItem()` sends via ReliableOrdered
- `destroy_item_handler.cpp` — validates slot, blocks during active trade, calls `inventory.removeItem()`, marks dirty + resyncs
- `onDestroyItemRequest` callback on InventoryPanel fires with (slot, displayName)
- `destroy_item_dialog` ConfirmDialog added to fate_hud.json (centered, zOrder 110, "Destroy" / "Cancel")
- Confirm sends `CmdDestroyItem`, cancel clears pending state, both hide dialog

**ResourceType::None bug fix:**
- Added `None = 0` to `ResourceType` enum (Fury = 1, Mana = 2) — previously only Fury/Mana existed
- Database `resource_type = 'None'` (used by all auto-attacks and passives) was incorrectly converted to `ResourceType::Mana` via catch-all else clause
- Conversion now explicitly handles "None" / "Fury" / "Mana"
- `SkillDefinition::resourceType` default changed from `Mana` to `None`
- Skill execution skips all resource checks/deduction when `resourceType == None`
- Fury deduction uses explicit `else if (ResourceType::Fury)` instead of catch-all else
- Affected all 3 class auto-attacks (warrior_cleave, archer_quick_shot, mage_arcane_bolt) and all passive skills

### March 26, 2026 — Ghost Entity Lifecycle Fixes, Replication Leave Bug, Chat/Input/UI Fixes

**Critical: Ghost entity cleanup on disconnect/reconnect:**
- `onDisconnected` callback now destroys all ghost entities from the world (previously only cleared tracking maps, leaving zombie entities rendered forever)
- New `onReconnectStart` callback on `NetClient` — clears all ghost entities when auto-reconnect begins (heartbeat timeout), so stale sprites from a crashed server don't persist
- `processDestroyQueue()` now called after `netClient_.poll()` during gameplay — previously never called during normal play, so entities queued for destruction by `onEntityLeave` were never actually removed from the world

**Critical: Replication `SvEntityLeave` never sent for picked-up/despawned items:**
- `unregisterEntity()` erased the handle→PID mapping immediately, so when `sendDiffs()` later tried to look up the PID for the `left` set, `getPersistentId()` returned null and the leave message was silently skipped
- Fix: `recentlyUnregistered_` map preserves handle→PID for one tick, cleared after `sendDiffs` completes
- This was the root cause of "phantom loot sprites that never disappear"

**Critical: Interpolation writing (0,0) for entities with missing data:**
- `getInterpolatedPosition()` returns `Vec2{0,0}` when an entity has no interpolation state
- The interpolation apply loop wrote this (0,0) to the entity's Transform every frame, causing a visible flicker at world origin before destruction
- Fix: check the `valid` out-parameter — skip the position write entirely if interpolation data is missing, preserving the entity's last correct position

**Chat system:**
- Chat idle overlay now always shows last N messages (TWOM style) instead of fading after 14 seconds
- Chat button on status bar now properly sets input context (`setChatMode`) so keyboard shortcuts (I, K, etc.) are suppressed while chat is open
- Chat close (X button) and Escape key properly exit chat mode
- `onClose` callback wired to return to idle overlay mode

**Scene save hardening:**
- `Scene::saveToFile()` now filters `ghost` and `dropped_item` tagged entities (matching editor's `saveScene()` filter)

**Equipment slot validation:**
- Server `processEquip()` now validates item subtype matches target equipment slot (prevents equipping a sword in a helmet slot)

**Skill arc visibility:**
- Removed `"visible": false` from skill_arc in fate_hud.json — now visible on game start without requiring inventory toggle

**Menu panels visibility:**
- Added `"visible": false` to fate_menu_panels.json root — inventory no longer auto-opens on login

**Test scene optimization:**
- Test scene factory skips creating 1571 entities when WhisperingWoods.json exists (previously created and immediately destroyed on every startup)

**Server crash safety:**
- Both kill paths in combat_handler.cpp wrapped in try-catch to prevent server process death on exceptions during loot/XP processing

### March 25, 2026 — Inventory Stacking, Stats Sync, UI Inspector Polish, Hot-Reload Safety

**Inventory Drag-to-Stack/Swap (full pipeline):**
- New packet type `CmdMoveItem` (0x33) with `CmdMoveItemMsg` (sourceSlot + destSlot)
- `NetClient::sendMoveItem()` sends via ReliableOrdered
- `ServerApp::processMoveItem()` in new `inventory_move_handler.cpp` — validates slots, blocks during active trade, calls `inventory.moveItem()`, marks dirty + resyncs
- `InventoryPanel::onRelease()` now handles general move/stack/swap (not just stat scroll enchanting)
- `onMoveItemRequest` callback wired in game_app.cpp to NetClient

**Drag-to-Equip/Unequip:**
- `NetClient::sendEquip()` added — sends `CmdEquipMsg` (action, inventorySlot, equipSlot) via ReliableOrdered
- Drag grid item → equip slot = equip (action=0, server validates class/level/combat)
- Drag equip slot → grid = unequip (action=1, server calls `unequipItem()` + `recalcEquipmentBonuses()`)
- `InventoryPanel` drag state tracks source type (`dragFromEquip_` flag + `dragSourceEquip_` index)
- `equipSlotEnum[]` static array maps UI slot indices to `EquipmentSlot` enum values
- `onEquipRequest` / `onUnequipRequest` callbacks wired in game_app.cpp

**Stats Panel Fix — Server-Authoritative Derived Stats:**
- `CharacterStats::applyServerSnapshot()` — client now applies server-sent armor, magic resist, crit rate, hit rate, evasion, speed, damage multiplier from `SvPlayerStateMsg`
- Previously `onPlayerState` ignored all derived stats — armor always showed base VIT formula with `equipBonusArmor = 0`

**Equipment Slots Expanded (8 → 10):**
- Added Belt and Cloak slots to paper doll (matching server `EquipmentSlot` enum)
- `NUM_EQUIP_SLOTS = 10` constant replaces all hardcoded `8`s across render, hitTest, tooltip, serializer, deserializer, inspector, game_app equipMap
- Default layout positions for Belt (right-middle) and Cloak (right-bottom), adjustable in inspector

**UI Inspector — Font/Color Configurability:**
- InventoryPanel: 5 font sizes (item letter, quantity badge, currency value/label, equip label) + 7 colors (quantity, item text, equip label, gold/plat label/value) + platinum position offsets (platOffsetX/Y)
- StatusPanel: 6 font sizes (title, name, level, stat label, stat value, faction) + 5 colors (title, name, level, stat label, faction)
- Equipment slot tree nodes now show equipped item name + stats in inspector (e.g., "Weapon: Iron Sword")
- All new fields fully serialized/deserialized in UI JSON — existing scenes load with defaults, no migration needed

**Hot-Reload Crash Fix (use-after-free):**
- `UIManager::addScreenReloadListener()` — multi-listener callback fired after any `loadScreen()` replaces a screen tree
- Editor: `revalidateSelection()` called on reload (fixes dangling `selectedNode_`)
- Game: all cached widget pointers (`inventoryPanel_`, `skillArc_`, `chatPanel_`, `deathOverlay_`, NPC panels, `loginScreenWidget_`) nulled + `retainedUILoaded_` reset on their screen's reload — next frame re-resolves all pointers and re-wires callbacks
- `closeMenu` lambda no longer captures raw `menuScreen` pointer — looks up `"fate_menu_panels"` fresh via `uiManager().getScreen()`
- Source-dir save path fixed: was double-nesting `assets/scenes/assets/ui/screens/`; now strips to project root before appending relative UI path

**Removed:**
- All 4 `[predict]` combat LOG_DEBUG lines (spell/physical/PvP spell/PvP physical) — client prediction damage was inaccurate and spammy

**Other:**
- `PlayerInfoBlock::barSpacing` deserialization gap fixed (was serialized but not loaded back)

**Files:** `engine/net/packet.h`, `engine/net/game_messages.h`, `engine/net/net_client.h/cpp`, `engine/ui/ui_manager.h/cpp`, `engine/ui/ui_serializer.cpp`, `engine/ui/widgets/inventory_panel.h/cpp`, `engine/ui/widgets/status_panel.h/cpp`, `engine/editor/editor.h/cpp`, `engine/editor/ui_editor_panel.cpp`, `game/game_app.h/cpp`, `game/shared/character_stats.h/cpp`, `game/systems/combat_action_system.h`, `server/server_app.h/cpp`, `server/handlers/inventory_move_handler.cpp` (new)

---

### March 25, 2026 — V3 Audit Fixes: ARM Threading, Economy Exploits, Event Lock Safety

5 issues from Production Readiness Review V3 (2 CRITICAL, 3 HIGH) fixed in one pass. Build clean, 983 tests passing.

**C2 — AsyncSceneLoader ARM Data Race** (`engine/scene/async_scene_loader.h/cpp`):
- `workerFailed` changed from `bool` to `std::atomic<bool>` — was written by worker thread, read by main thread without synchronization
- `workerDone.store()` now uses `memory_order_release`; all main-thread loads use `memory_order_acquire` — establishes a proper happens-before relationship for non-atomic fields (errorMessage, prefabs, texturePaths, sceneMetadata, totalEntities, totalTextures)
- `hasFailed()` now gates through `workerDone` acquire before reading `workerFailed` — prevents racy reads on ARM (iOS/macOS Metal)
- `workerProgress` stores use `memory_order_relaxed` (progress is advisory, not a synchronization point)

**C3 — Shop Buy Quantity Exploit** (`server/handlers/shop_handler.cpp`):
- Added `MAX_STACK_SIZE` (9999) cap on `msg.quantity` before any processing — previously a client could send `quantity = 65535` to create a single stack of 65535 items in one slot
- Existing `freeSlots() <= 0` check was for a single slot; `addItem()` stacks onto existing items of same type, so the cap is sufficient

**H2 — Aurora Buff Friendly Fire** (`game/shared/status_effects.h/cpp`, `server/handlers/aurora_handler.cpp`):
- Added `removeEffectBySource(EffectType, uint32_t source)` to `StatusEffectManager` — only removes an effect if its `sourceEntityId` matches the given source, leaving other effects of the same type untouched
- Added `SOURCE_AURORA = 0xAE01` sentinel constant on `StatusEffectManager`
- `applyAuroraBuffs()` and `tickAuroraRotation()` now tag AttackUp/ExpGainUp with `SOURCE_AURORA`
- `removeAuroraBuffs()` now uses `removeEffectBySource()` — potion and party AttackUp buffs are preserved during Aurora rotation transitions

**H4 — Dungeon Event Lock Stale Key** (`server/handlers/dungeon_handler.cpp`, `server/handlers/gm_handler.cpp`):
- Event lock in `startDungeonInstance` moved to AFTER `transferPlayerToWorld` — `conn->playerEntityId` now holds the new (post-transfer) entity ID, so the lock key matches what other systems check against
- Event lock erase in `endDungeonInstance` moved to BEFORE the exit transfer — erases using the current dungeon entity ID before it changes
- GM `/dungeon` command updated with same pattern
- Dungeon validation check now also consults `dungeonManager_.getInstanceForClient()` as a secondary guard

**H5 — Teleporter Item Cost Partial Consumption** (`server/handlers/teleport_handler.cpp`):
- Replaced single-slot `findItemById` + `removeItemQuantity` with a loop across all inventory slots
- Now consumes from each matching stack until `remaining` reaches 0 — correctly handles items split across multiple stacks (e.g., need 5, first stack has 3, second has 2)

**Files:** `engine/scene/async_scene_loader.h/cpp`, `server/handlers/shop_handler.cpp`, `game/shared/status_effects.h/cpp`, `server/handlers/aurora_handler.cpp`, `server/handlers/dungeon_handler.cpp`, `server/handlers/gm_handler.cpp`, `server/handlers/teleport_handler.cpp`

---

### March 25, 2026 — Race Condition Audit, Inventory Tooltips & Networking Fixes

**Race Condition Fixes (6 verified from Gemini audit):**
- **Job system use-after-free**: Counter released after fiber resumes, not before (checkWaitList)
- **Asset registry sync/async race**: `load()` now waits for in-flight async decode instead of returning a Loading handle
- **PlayerLockMap serialization break**: `erase()` preserves mutex if worker fibers still hold references (prevents reconnect deserialization)
- **WAL truncation data loss**: Deferred until all async auto-saves commit (was truncating same frame as dispatch)
- **PvP disconnect exploit**: Victim character_id resolved from entity data instead of connection map (honor/kill credit preserved)
- **Client message dropping**: Buffered onSkillSync, onQuestSync, onQuestUpdate, onChatMessage + 5 notification handlers during login/loading

**Editor Play-in-Editor Bug:**
- Network client now disconnects when play mode exits (server messages were creating ghost entities in edit-mode world — "purple named rat")
- Combat click-targeting blocked when editor is not in play mode

**Inventory Tooltip System (TWOM-style):**
- Protocol expanded: `displayName`, `rarity`, `itemType`, `levelReq`, `damageMin/Max`, `armor` added to `InventorySyncSlot` and `InventorySyncEquip`
- Server populates from `itemDefCache_` (748 item definitions from PostgreSQL)
- Client parses rolled stats into stat lines (e.g., "INT +12", "Mana +85", "Crit +7")
- Tooltip shows: enchant prefix (+8), rarity-colored name, item type, attack/armor, rolled stats, level requirement
- Equipment and grid slots now carry full tooltip data (enchantLevel, statLines, levelReq, damageMin/Max, armor)

**Inventory Stacking:**
- `addItem()` now stacks consumables/materials into existing slots by matching `itemId` (items with no rolled stats, no socket, no enchant)
- Prevents inventory filling with 16 individual potion stacks

**Networking Fixes:**
- Server `sendPacket()` and client receive/decrypt buffers increased to 4KB (inventory sync with tooltip data exceeded 1200-byte MTU limit, causing silent packet drops)
- Server sends "[System] Inventory full!" chat message when pickup rejected
- WAL item-add entry moved after successful `addItem()` (was logging phantom entries for failed pickups)
- Removed client-side optimistic drop destruction on pickup click (drops now stay visible until server confirms via replication)

**Test Fixes:**
- `DungeonManager::tick` test calls updated for 2-arg signature (`dt`, `gameTime`)
- `fate_scenario_tests` CMake target: added `/EHsc` (doctest requires C++ exceptions)
- `SvInventorySyncMsg` round-trip test updated for new protocol fields

**Files:** `engine/job/job_system.cpp`, `engine/asset/asset_registry.cpp`, `engine/net/protocol.h`, `engine/net/net_server.cpp`, `engine/net/net_client.cpp`, `engine/ui/widgets/inventory_panel.h/cpp`, `game/game_app.h/cpp`, `game/shared/inventory.cpp`, `game/shared/item_instance.h`, `game/systems/combat_action_system.h`, `server/player_lock.h`, `server/server_app.h`, `server/handlers/persistence_handler.cpp`, `server/handlers/combat_handler.cpp`, `server/handlers/sync_handler.cpp`, `tests/test_dungeon_manager.cpp`, `tests/test_state_sync.cpp`, `CMakeLists.txt`

---

### March 25, 2026 — Inventory Inspector, Critical Bugfixes & Reconnect Grace Period

**Inventory Panel — Data-Driven Inspector:**
- All layout properties now editable in UI inspector with collapsible TreeNode sections (Layout, Paper Doll, Grid, Equipment Slots)
- 6 new layout properties: `dollWidthRatio`, `contentPadding`, `currencyHeight`, `gridPadding`, `dollCenterY`, `characterScale`
- Per-equipment-slot positioning: each of 8 slots has `offsetX`, `offsetY`, `sizeMul` (multiples of equipSlotSize from doll center)
- Unified `computeEquipPositions()` helper used by render, hitTest, and tooltip — fixes position mismatch bug where hitTest used different multipliers than render
- Grid slot hitTest now uses same auto-size formula as renderItemGrid (fixes second mismatch)
- Full serializer/deserializer round-trip for all new properties including `equipLayout` JSON array

**Critical Bugfixes:**
- **Inventory sync race condition**: `onInventorySync` arrived before `localPlayerCreated_` and was silently dropped. Now buffered in `pendingInventorySync_` and replayed after player creation.
- **Inventory slot wipe**: `setSerializedState()` replaced 16-slot vector with empty one when DB had 0 items (`slots_ = std::move(slots)`). Now always ensures 16 slots via `assign()`.
- **Damage text showing prediction**: Client prediction (50-70, no weapon bonus) was displayed; server's real damage (1000+) was skipped for local attacks. Fixed: prediction `spawnDamageText` removed from `CombatActionSystem`, server-authoritative `SvCombatEvent.damage` always shown.
- **Inventory items not persisted on disconnect**: `savePlayerToDB` saved gold but never called `saveInventoryForClient`. Now calls it when inventory is dirty.
- **Paper doll sprite upside down**: Added `flipY = true` to character sprite draw params.

**Battlefield/Dungeon Reconnect Grace Period:**
- Players who disconnect during an active battlefield or dungeon get a 3-minute grace period to rejoin
- `BattlefieldManager`: `markDisconnected()` preserves faction/kills/deaths/return position in grace map. `restorePlayer()` re-adds with full preserved state. `tickGracePeriod()` expires entries when event ends or 180s passes.
- `DungeonManager`: `markDisconnected()` saves instanceId + return point. Grace entries expire if instance completes/expires or timer runs out.
- Arena unchanged (disconnect = forfeit)
- Disconnect handler calls `markDisconnected` instead of `removePlayer`; session setup checks grace entries and restores player into event

**Files:** `engine/ui/widgets/inventory_panel.h/cpp`, `engine/editor/ui_editor_panel.cpp`, `engine/ui/ui_serializer.cpp`, `engine/ui/ui_manager.cpp`, `game/game_app.h/cpp`, `game/shared/inventory.h`, `game/shared/battlefield_manager.h`, `game/systems/combat_action_system.h`, `server/dungeon_manager.h`, `server/server_app.cpp`, `server/handlers/persistence_handler.cpp`, `server/handlers/dungeon_handler.cpp`

---

### March 25, 2026 — TWOM-Style Menu System & Inventory Restyle

**MenuTabBar Widget** (`engine/ui/widgets/menu_tab_bar.h/cpp`):
- 7 panel tabs (STS/INV/SKL/GLD/SOC/SET/SHP) with left/right arrow cycling
- Active tab: gold (#C8A832), inactive: dark tan (#4A3F2F)
- Direct tap to jump to panel, arrows cycle with wrapping
- Wired to show/hide panels by index (status, inventory, skills, guild, social, settings, shop)
- FateStatusBar menu items ("Inventory", "Skills", etc.) set active tab + open menu
- Menu opens to last active tab instead of always resetting to inventory

**Inventory Panel Restyle** (`engine/ui/widgets/inventory_panel.cpp`):
- TWOM-matching layout: paper doll left 45%, item grid right 55%
- 8 equipment slots positioned around character (Hat/Necklace/Weapon/Shield/Armor/Gloves/Boots/Ring)
- Rarity-colored slot borders (Common=gray, Uncommon=green, Rare=blue, Epic=purple, Legendary=orange)
- Formatted currency display with comma separators ("Gold 2,161,763", "Platinum 6")
- Warm parchment background, removed "Inventory" title (tab bar handles identification)

**Paper Doll Character Rendering:**
- Base character sprite rendered at 5x scale in the paper doll inset area
- Armor and hat overlay layers supported (wired as nullptr until equipment art is available)
- Depth layering: character behind equipment slots

**Item Tooltip on Tap:**
- Tap any equipped or inventory item → cream popup with drop shadow and brown border
- Item name in rarity color with enchant prefix ("+8 Mystic Wand")
- Type label, stat lines, level requirement
- Auto-sizes vertically, clamped to panel bounds
- Tap tooltip or tap elsewhere to dismiss

**Responsive Layout & HUD Integration:**
- All menu panels use `StretchAll` anchor with margins (top=108, left/right=80, bottom=20) — adapts to any screen resolution/aspect ratio
- Tab bar uses `StretchX` with matching left/right margins — tabs spread evenly across panel width
- Grid slots and equipment slots auto-size to fill available area (no fixed pixel sizes)
- DPad and SkillArc hidden when menu is open, restored on close
- Menu panels sit directly below the tab bar with no gap

**Button Wiring Fixes:**
- Attack button: `Input::injectAction()` writes to both ActionMap and InputBuffer (combat reads consumeBuffered)
- PickUp button: scans nearest dropped item ghost within 48px, sends CmdAction, client-side prediction removes ghost immediately
- Menu button: status bar height 60→100, buff_bar repositioned to avoid hit-test overlap
- ChatPanel idle mode: no longer consumes clicks (passes through to DPad/SkillArc underneath)

**Files:** `engine/ui/widgets/menu_tab_bar.h/cpp` (new), `engine/ui/widgets/inventory_panel.h/cpp`, `engine/input/input.h`, `game/game_app.h/cpp`, `assets/ui/screens/fate_menu_panels.json`, `assets/ui/screens/fate_hud.json`

---

### March 25, 2026 — Editor Polish & Full Widget Inspector Coverage

**UI Editor — Complete Widget Inspector Coverage:**
All 42 widget types now have full inspector sections, serializer round-trip support, and deserializer field reads. No more edit-JSON-rebuild cycles for any widget property.

- Added inspector sections for 19 previously missing widget types: ImageBox, BuffBar, BossHPBar, ConfirmDialog, NotificationToast, Checkbox, LoginScreen, PartyFrame, ChatPanel, FateStatusBar, DeathOverlay, CharacterSelectScreen, CharacterCreationScreen, GuildPanel, NpcDialoguePanel, ShopPanel, BankPanel, TeleporterPanel, TradeWindow
- Added serializer blocks for 12 previously unserialized widgets (ScrollView, CharacterSelectScreen, PartyFrame, ChatPanel, TeleporterPanel, + 7 runtime-only type stubs)
- Fixed deserializer gaps: ScrollView missing `contentHeight`, CharacterSelectScreen missing `entryButtonWidth`, TeleporterPanel missing `title`, ChatPanel missing `chatIdleLines`, PartyFrame missing `cardSpacing`
- Fixed LeftSidebar serializer/deserializer key mismatch (`panelLabels` written but `labels` read)

**FateStatusBar — All Layout Properties Exposed:**
Promoted all hardcoded `static constexpr` rendering constants to public editable fields with full serialize/deserialize/inspector support:
- Layout: `topBarHeight`, `portraitRadius`, `barHeight` (HP/MP bars)
- Menu button: `menuBtnSize`, `menuBtnGap`, `showMenuButton` toggle
- Chat button: `chatBtnSize`, `chatBtnOffsetX`, `showChatButton` toggle
- Coordinates: `coordFontSize`, `coordOffsetY`, `coordColor`, `showCoordinates` toggle
- Font sizes: `levelFontSize`, `labelFontSize`, `numberFontSize`, `buttonFontSize`
- Colors: `hpBarColor`, `mpBarColor`, `coordColor` — all with RGBA color pickers
- Inspector organized into collapsible sections: Layout, Menu Button, Chat Button, Coordinates, Font Sizes

**UI Hierarchy — Unity-Style Polish:**
- Nodes show just the ID (type conveyed by colored badge instead of `type: id` format)
- Colored 3-letter type badges for all 42 widget types (PNL=blue, BTN=green, LBL=yellow, ARC=orange, TGT=red, etc.)
- Alternating row shading for readability
- Blue highlight on selected node
- Gold-colored screen headers to distinguish from child nodes
- Per-node visibility toggle (`o`/`-` button) — hidden nodes rendered dimmed
- Escape key deselects current node
- Context menu separator before Delete to prevent misclicks

**Selection Outline Fix:**
- Fixed widget selection outline drawing at wrong position when using device resolution presets (e.g., iPhone 16 Pro 2556x1117). The `computedRect()` is in FBO resolution but the outline was drawing without scaling to the displayed viewport size. Now correctly scales from FBO coords to screen coords via `viewportSize_ / fboSize`.

**Asset Browser — Visual Polish:**
- Folders rendered as proper folder icons (golden body with tab) instead of flat "DIR\nname" buttons; selected folders turn blue
- File type cards with dark rounded rectangles, colored accent strip at top, centered type icon
- Sprite thumbnails rendered directly on card with checkerboard background
- Click-to-select with blue border highlight, filename turns blue when selected
- Hover effect with subtle white border on all item types
- Centered filename labels below cards
- Selected file path shown in footer bar
- `.meta.json` files now classified as Animation type (teal ANM icon) instead of Scene

**Files modified:** `engine/editor/ui_editor_panel.h/cpp`, `engine/editor/asset_browser.h/cpp`, `engine/editor/editor.cpp`, `engine/ui/ui_serializer.cpp`, `engine/ui/ui_manager.cpp`, `engine/ui/widgets/fate_status_bar.h/cpp`

### March 25, 2026 — UI Button Hit-Test Fixes (Mobile-Critical)

Fixed three input bugs that made HUD buttons non-functional during gameplay — critical for mobile touch input.

**FateStatusBar — Menu/Chat button coordinate mismatch:**
- `menuBtnCenter_`, `chatBtnCenter_`, `menuOverlayRect_` were cached in **global** screen coords during render, but `onPress` receives **local** coords (relative to computedRect). All comparisons produced wrong distances. Fixed by storing in local coords.
- Menu button only worked at the very top edge because the computedRect (60px height) was too small to reach the button center (~75px below top). Added `hitTest` override that expands the hit area to include the menu button circle and menu overlay popup below the bar strip.

**SkillArc — Attack/PickUp/skill slots unreachable on high-res screens:**
- All rendered elements (arc slots at radius 180, attack/pickup buttons) are scaled by `layoutScale_` during render, but the anchor rect (250x380) is fixed. On high-res screens (e.g., iPhone 16 Pro, layoutScale_=1.24), scaled elements overflow the anchor rect. `hitTestNode` rejected clicks outside the rect, so `onPress` was never called.
- Added `hitTest` override with a circular hit area covering the full reach of all elements (max of arc radius + slot size, attack offset + button size, pickup offset + button size), all scaled by layoutScale_.

**UINode — virtual hitTest:**
- `UINode::hitTest()` made `virtual` so widgets with custom-rendered content (arcs, circles, elements below their anchor rect) can expand their clickable area beyond `computedRect`. Base implementation unchanged.

**Files modified:** `engine/ui/ui_node.h`, `engine/ui/widgets/fate_status_bar.h/cpp`, `engine/ui/widgets/skill_arc.h/cpp`

---

### March 24, 2026 - TWOM-Style HUD Rework (Gold Metallic UI)

Full HUD restyle to match The World of Magic mobile aesthetic. Gold/tan metallic embossed buttons, TWOM-style top status bar, redesigned skill arc with page selector, idle chat overlay.

**Metallic Rendering Helper** (`engine/ui/widgets/metallic_draw.h/cpp`):
- Shared `drawMetallicCircle()` utility for all gold buttons. Procedural gradient via 6 concentric `drawCircle` rings (edge-to-center color interpolation), outer border ring, highlight crescent at 11 o'clock, lower shadow band. Designed for easy texture swap — replace body with single `batch.draw()` call when atlas sprites are ready.

**FateStatusBar** (`engine/ui/widgets/fate_status_bar.h/cpp`) — New widget replacing PlayerInfoBlock + MenuButtonRow:
- TWOM-style horizontal top bar: portrait → "LV XX" (26pt) → "HP" (22pt) → orange bar → "330/330" (28pt yellow) → "MP" → blue bar → "640/640"
- HP/MP bars dynamically stretch to fill available screen width (compact on narrow, wide on ultrawide)
- Coordinate display below bar strip (replaces editor's `drawViewportHUD` which was removed)
- EXP progress arc around portrait (circular fill indicator)
- Gold metallic Menu button — opens parchment popup overlay with 7 items, tap-outside-to-dismiss
- Gold metallic Chat button (top-right) — toggles ChatPanel between idle overlay and full panel mode
- Registered as `"fate_status_bar"` type in `UIManager::parseNode()`

**Skill Arc Restyle** (`engine/ui/widgets/skill_arc.h/cpp`):
- Red "ATK" button → gold metallic "Action" button with crossed-swords icon
- New gold metallic "Pick Up" button (60px), individually positionable via `pickUpOffset`
- 5 skill slots per page on C-shaped arc (290°→190°), radius 180px
- **SlotArc** — page selector (1/2/3/4) follows its own configurable C-arc with independent radius, angles, and offset
- `attackOffset` and `pickUpOffset` (Vec2) — individually positionable buttons, editable in UI Inspector
- `slotArcRadius`, `slotArcStartDeg`, `slotArcEndDeg`, `slotArcOffset` — all editable in UI Inspector
- Hit-test convention: -1=Attack, -2=PickUp, -10..-13=page select, 0..N=skill slot, -99=miss
- All properties saved/loaded via JSON and live-editable during play

**D-Pad Restyle** (`engine/ui/widgets/dpad.cpp`):
- Dark semi-transparent → gold metallic circle via `drawMetallicCircle` at 0.85 opacity
- Arms: tan with 1px embossed highlight ridges. Arrows: white-tan. Active direction: bright gold (was cyan). Dead zone: darker tan.

**Chat Panel Idle Overlay** (`engine/ui/widgets/chat_panel.h/cpp`):
- New idle overlay mode (default) — floating messages over game world with drop shadows, no background panel
- Configurable line count: 0/1/3/5 visible lines (`chatIdleLines` setting)
- 12-second message visibility + 2-second alpha fade-out
- Channel-colored sender names, white message text
- Full panel mode (existing tabs/input/close) toggled via Chat button
- Absorbs `chat_ticker` role (ticker removed from HUD JSON)

**HUD JSON Rewire** (`assets/ui/screens/fate_hud.json`):
- Removed: `player_info` (PlayerInfoBlock), `menu_buttons` (MenuButtonRow), `chat_ticker` (ChatTicker)
- Added: `status_bar` (FateStatusBar, full-width 60px), `buff_bar` (TopLeft below status bar)
- Repositioned: `target_frame` → TopRight below status bar, EXP bar → 14px (was 22px), D-pad → 150px (was 200px)
- `fate_menu_panels.json`: removed `left_sidebar` node (accessed via Menu button now)
- `fate_social.json`: ChatPanel now starts visible in idle overlay mode (was visible:false)
- Chat panel removed from HUD JSON (single instance in social screen handles both idle + full modes)

**Minor Tweaks:**
- EXP bar: background darkened, font 9pt (was 11pt)
- Target frame: darker background, added border
- D-pad: reduced from 200px to 150px

**Editor Changes:**
- Removed `drawViewportHUD()` from editor (coordinates now shown by FateStatusBar)
- SkillArc UI Inspector: added Attack Offset, PickUp Offset, SlotArc Radius/Angles/Offset — all live-editable during play, saved via "Save Screen"

**Data Wiring** (`game/game_app.cpp`):
- FateStatusBar bound to player HP/MP/XP/level/name/tile position each frame
- Menu item callbacks open Inventory/Status/Skills panels
- Chat button toggles `chatPanel_->setFullPanelMode()` (social screen's single chat panel)
- ChatPanel `updateTime(dt)` called per frame for idle fade
- Removed all PlayerInfoBlock/MenuButtonRow/ChatTicker/LeftSidebar references
- Chat message forwarding simplified (single `chatPanel_` handles both modes)

**Files:** `engine/ui/widgets/metallic_draw.h/cpp` (new), `engine/ui/widgets/fate_status_bar.h/cpp` (new), `engine/ui/widgets/skill_arc.h/cpp`, `engine/ui/widgets/dpad.cpp`, `engine/ui/widgets/chat_panel.h/cpp`, `engine/ui/widgets/exp_bar.cpp`, `engine/ui/widgets/target_frame.cpp`, `engine/ui/ui_manager.cpp`, `engine/editor/editor.cpp/h`, `engine/editor/ui_editor_panel.cpp`, `game/game_app.cpp`, `assets/ui/screens/fate_hud.json`, `assets/ui/screens/fate_menu_panels.json`, `assets/ui/screens/fate_social.json`, `tests/test_ui_metallic_draw.cpp` (new), `tests/test_ui_fate_status_bar.cpp` (new), `tests/test_ui_skill_arc.cpp`

---

### March 24, 2026 - Zone Transition Crash Fix, Loading Screen Polish & UI Resolution Scaling

**Zone Transition Crash Fix** — Critical use-after-free crash when entering portals or using teleporters. Three root causes identified and fixed:
1. `UIManager::loadScreenFromString()` replaced screens via map assignment, destroying the old widget tree without clearing `focusedNode_`/`hoveredNode_`/`pressedNode_` — dangling pointers caused access violation at `0xFFFFFFFFFFFFFFFF` when `uiManager_.render()` ran next frame. Fixed by clearing stale pointers before replacement (mirrors `unloadScreen()` logic).
2. `NPCInteractionSystem` cached raw `Entity*` pointers (`localPlayer`, `interactingNPC`) that became dangling after async entity destruction. Converted to `EntityHandle`-based resolution each frame.
3. `App::update()` ran `world.update()` (all ECS systems) during `LoadingScene` when entities were mid-destruction. Added `isLoading_` early-return guard.

**Loading Screen Polish:**
- 2-second minimum display time (`loadingMinTimer_`) — scenes load too fast on modern hardware. Progress bar shows real progress while loading, holds at 100% while waiting for min timer.

**UI Resolution Scaling** — All 19+ UI widgets now scale with viewport resolution:
- `UIManager::computeLayout()` computes `scale = screenHeight / UI_REFERENCE_HEIGHT` (900px) and propagates to all nodes via `layoutScale_`. Font sizes use `scaledFont(base)` (clamps to MIN_FONT_SIZE). Pixel dimensions multiply by `layoutScale_`.
- Widgets updated: exp_bar, target_frame, buff_bar, boss_hp_bar, chat_panel, chat_ticker, inventory_panel, shop_panel, bank_panel, trade_window, guild_panel, skill_panel, status_panel, death_overlay, notification_toast, party_frame, npc_dialogue_panel, slot, combat nameplates/HP bars/floating text.
- Login screen: separate scaling system with `recomputeScale(viewportH)`, capped at 2.5x max.
- Previously scaled (unchanged): player_info_block, skill_arc, left_sidebar, dpad.

**Other:**
- Zone transition cleanup now clears `ghostUpdateSeqs_`, resets NPC interaction system, clears combat target.
- Render system entity pass skipped during loading.
- Editor ground tile lock toggle (toolbar Locked/Unlocked button, `groundLocked_` member).

**Files:** `engine/app.cpp/h`, `engine/ui/ui_manager.cpp`, `engine/ui/widgets/*.cpp` (19 files), `engine/render/render_graph.cpp`, `game/game_app.cpp/h`, `game/systems/npc_interaction_system.h`, `game/systems/combat_action_system.h`

---

### March 24, 2026 - TWOM-Style Loading Screen System

Full async scene loading with TWOM-style loading screens. 3-phase fiber pipeline (JSON parse → prefab decode → batched entity creation), zone artwork backgrounds, real progress bars. Zero frame freezes.

**AsyncSceneLoader** (`engine/scene/async_scene_loader.h/cpp`):
- Fiber worker parses JSON + collects texture paths (never touches ECS or AssetRegistry). Main thread kicks texture `loadAsync()`, batch-creates entities (adaptive 2ms budget), tracks texture progress via `AssetHandle`.
- `PendingSceneLoad` with atomic progress fields, error handling, double-start guard.

**LoadingScreen** (`engine/render/loading_screen.h/cpp`):
- Zone artwork background (`assets/ui/loading/{sceneName}.png`, solid dark fallback). Bottom progress bar (gold fill), zone name with shadow, percentage text. SpriteBatch + SDFText (works in shipping builds).

**Game Wiring** (`game/game_app.h/cpp`, `engine/app.h/cpp`):
- `ConnectionState::LoadingScene` between UDPConnecting and InGame. Network polling continues during loading. Disconnect check returns to login. Entity message buffering + replay on completion.
- Render pipeline hooks all 4 paths (GL/Metal × Editor/Shipping) to skip render graph during loading.

**Files:** `engine/scene/async_scene_loader.h/cpp`, `engine/render/loading_screen.h/cpp`, `engine/app.h/cpp`, `game/game_app.h/cpp`, `tests/test_async_scene_loader.cpp`

---

### March 24, 2026 - Performance Fixes (Memory Leak, Packet Batching, Arena Syscall)

Three targeted performance fixes: a server memory leak, network packet batching, and an arena allocator syscall cache.

**Server processDestroyQueue Fix** (`server/server_app.cpp`):
- `processDestroyQueue()` was only called on client disconnect — destroyed entities (despawned ground items, dead mobs) accumulated in memory during sustained play. Now called once per tick after despawn processing, immediately freeing destroyed entities.

**Entity Update Packet Batching** (`engine/net/replication.cpp`, `engine/net/net_client.cpp`, `engine/net/packet.h`):
- New `SvEntityUpdateBatch` packet type (0xBA). Multiple entity delta updates are now written into a single UDP packet buffer (up to MAX_PAYLOAD_SIZE / 1182 bytes), with a leading count byte.
- Previously, 50 visible entities = 50 individual UDP packets per tick per client (each with 18-byte header overhead). Now typically 2-3 packets instead — ~90% reduction in per-packet overhead.
- Client unpacks by reading the count byte and looping `SvEntityUpdateMsg::read()`. The existing single-entity `SvEntityUpdate` (0x92) handler is preserved for backwards compatibility.

**Arena pageSize() Cache** (`engine/memory/arena.h`):
- `pageSize()` previously called `GetSystemInfo()` (Win32) or `sysconf(_SC_PAGESIZE)` (POSIX) on every invocation — a syscall per arena allocation. Now cached in a `static const` lambda, runs the syscall once and returns the cached value thereafter.

**AOI Spatial Hash Commented Out** (`engine/net/replication.cpp`):
- `rebuildSpatialIndex()` was called every tick but the spatial hash was never queried (dead CPU work). Commented out with explanation of when to re-enable (zone populations > 200 entities requiring distance-based AOI).

**Files changed:** `server/server_app.cpp`, `engine/net/replication.cpp`, `engine/net/net_client.cpp`, `engine/net/packet.h`, `engine/memory/arena.h`

---

### March 24, 2026 - Animation System Polish

TWOM-style animation authoring workflow — sprite sheet slicing in the editor, `.meta.json` convention-based output, auto-load at entity creation.

**Sprite Sheet Slicer** (`engine/editor/animation_editor.h`, `engine/editor/animation_editor.cpp`):
- New slicer mode in the Animation Editor: load a sprite sheet PNG, set cell size, see a grid overlay with frame index labels, click cells to assign frames to animation states. Zoom + pan for large sheets.
- Quick templates: File > New > Mob pre-creates idle/walk/attack/death states; New > Player adds cast.
- Direction support: Down/Up/Side tabs. Side emits `_right` + `_left` (with flipX) entries.

**`.meta.json` Save/Load** (`engine/editor/animation_editor.cpp`):
- Save writes `.meta.json` next to the sprite sheet (e.g., `mob_squirrel.png` → `mob_squirrel.meta.json`) in the format `AnimationLoader::parsePackedMeta()` already reads.
- Dual-save to build dir + source dir. Load reconstructs full editor state from existing metadata.

**Auto-Load Pipeline** (`game/game_app.cpp`, `game/animation_loader.h/cpp`):
- New `AnimationLoader::tryAutoLoad()` — derives `.meta.json` path from `SpriteComponent::texturePath`, applies animation data if found, auto-plays `idle_down`.
- Wired at 3 trigger points: `SvEntityEnter` (mob/NPC/player ghosts), `createPlayer()` (local player), `enterPlayMode()` (editor play-in-editor).
- File-existence pre-check avoids LOG_WARN spam for entities without metadata.

**Direction Fallback** (`game/animation_loader.cpp`):
- `applyToAnimator()` now auto-registers missing direction variants. A mob with only `idle_down` will also respond to `play("idle_up")`, `play("idle_left")`, etc.

**Inspector Integration** (`engine/editor/editor.cpp`):
- SpriteComponent: new "Animation" tree node showing read-only frame size, grid info, meta file status, and "Open Animation Editor" button that pre-loads the entity's sprite sheet.
- Animator: enhanced with a "States" row listing loaded animation base names, and context-aware "Open in Animation Editor" button.

**Tests:** 18 animation-related tests (90 assertions), all passing. New tests for direction fallback, tryAutoLoad missing file, non-square frames, frame index math, and `.meta.json` round-trip.

**Files changed:** `engine/editor/animation_editor.h`, `engine/editor/animation_editor.cpp`, `engine/editor/editor.h`, `engine/editor/editor.cpp`, `game/animation_loader.h`, `game/animation_loader.cpp`, `game/game_app.cpp`, `tests/test_animation_loader.cpp`

---

### March 24, 2026 - Collision Grid System

Collision tiles painted in the editor now block movement. Server-authoritative with client-side prediction — zero rubber-banding in normal play.

**CollisionGrid Data Structure** (`engine/spatial/collision_grid.h`):
- Per-scene packed bitgrid (1 bit per tile) with dynamic bounding box. Supports negative coordinates, variable map sizes.
- API: `beginBuild/markBlocked/endBuild` build phase, `isBlocked(float, float)` point check, `isBlockedRect(float, float, float, float)` AABB check, `isBlockedTile(int, int)` direct tile check.
- A 1000x1000 tile map uses ~125KB. Out-of-bounds queries return `false` (walkable).

**Server Integration** (`server/server_app.h`, `server/server_app.cpp`):
- `loadCollisionGridsFromScenes()` parses `assets/scenes/*.json` at startup (same pattern as portal loading). Extracts collision-layer tiles (`TileLayerComponent.layer == "collision"`), builds one `CollisionGrid` per scene.
- `CmdMove` handler checks `isBlockedRect()` after speed validation, before accepting position. Blocked moves trigger rubber-band correction (anti-cheat backstop). Uses player's `BoxCollider` for bounds (default 12x12 half-size).
- `MobAISystem::isBlockedByStatic()` checks collision grid via mob's `sceneId` after existing BoxCollider checks. Mobs can't walk through collision tiles.

**Client Integration** (`game/game_app.h`, `game/game_app.cpp`, `game/systems/movement_system.h`, `game/systems/mob_ai_system.h`):
- Builds `CollisionGrid` from collision-layer entities after scene load, rebuilds on zone transitions.
- `MovementSystem` checks grid after BoxCollider/PolygonCollider checks — player stops at walls locally (no rubber-banding).
- `MobAISystem` uses `localCollisionGrid_` fallback for client single-scene case.

**Tests:** 6 CollisionGrid unit tests (19 assertions): empty grid, set/get, pixel-to-tile conversion, AABB overlap detection, out-of-bounds, negative coordinates.

**Files changed:** `engine/spatial/collision_grid.h`, `server/server_app.h`, `server/server_app.cpp`, `game/game_app.h`, `game/game_app.cpp`, `game/systems/movement_system.h`, `game/systems/mob_ai_system.h`, `tests/test_collision_grid.cpp`

---

### March 24, 2026 - Tile Editor: Brush Sizes, 4-Layer System, Grid Shader Polish

Multi-tile brush painting, a 4-layer tile system, server trade bugfix, flood fill bounds fix, and a polished editor grid shader.

**Brush Sizes** (`engine/editor/editor.h`, `engine/editor/editor.cpp`):
- NxN brush stamps (1–5) for Paint and Erase tools. Numeric input in Tile Palette panel.
- Entire NxN stamp is one CompoundCommand — single Ctrl+Z undoes the full stamp. Drag-painting accumulates into one brush stroke compound.
- Faint green/red NxN preview overlay follows cursor in Paint/Erase mode.

**4-Layer Tile System** (`engine/editor/editor.h`, `engine/editor/editor.cpp`, `game/components/tile_layer_component.h`, `game/register_components.h`, `engine/ecs/prefab.cpp`):
- New `TileLayerComponent` (registered ECS component, `FATE_REFLECT` serialized) stores layer: `"ground"` / `"detail"` / `"fringe"` / `"collision"`.
- Auto-depth per layer: Ground=0, Detail=10, Fringe=100 (above player entities), Collision=-1.
- Layer dropdown and 4 visibility checkboxes in Tile Palette panel.
- All tile tools (Paint, Erase, Fill, RectFill, LineTool) are layer-aware — painting/erasing/filling only affects the selected layer. Tiles on different layers at the same position are independent.
- Collision layer: paints red semi-transparent overlay in editor (no palette tile needed), invisible at runtime (`SpriteComponent` stripped in non-`EDITOR_BUILD` via `removeComponent`).
- Fringe layer renders above player/NPC/enemy entities (depth 100+) for tree canopy / bridge overpass effects.
- `applyLayerVisibility()` toggles `sprite->enabled` per layer each frame in editor mode.
- Backwards compatible: legacy scenes without `TileLayerComponent` auto-migrate to `layer="ground"` in `jsonToEntity()`.

**Trade Inventory Space Validation** (`server/server_app.cpp`):
- Fixed trade execution that committed DB transfers without checking if receiving players had enough free inventory slots. Added pre-commit free-slot check accounting for items being sent out. Aborts trade if either player lacks space.
- Fixed in-memory sync order: offered items are now removed from both players into temp vectors first, then received items are added. Previously, items from A were added to B before B's items were removed, causing silent `addItem` failures on full inventories.

**Flood Fill Bounds Fix** (`engine/editor/tile_tools.h`, `engine/editor/tile_tools.cpp`, `engine/editor/editor.cpp`):
- `floodFill()` now takes `minCol/minRow/maxCol/maxRow` bounds instead of hardcoded `256x256`. Uses `int64_t` key packing so negative coordinates hash correctly.
- Editor computes fill bounds dynamically from actual tile extents (padded by 1).

**Grid Shader Polish** (`assets/shaders/grid.frag`):
- Major/minor grid lines: faint lines every `gridSize`, brighter thicker lines every 10 tiles.
- Red/green world-origin axes at (0,0) for orientation.
- Zoom-based minor grid fade prevents moiré when zoomed out. Removed circular spotlight fade.
- Sub-grid at 1/4 size preserved when zoomed past 2x.

**Files changed:** `engine/editor/editor.h`, `engine/editor/editor.cpp`, `engine/editor/tile_tools.h`, `engine/editor/tile_tools.cpp`, `engine/ecs/prefab.cpp`, `game/components/tile_layer_component.h`, `game/register_components.h`, `server/server_app.cpp`, `assets/shaders/grid.frag`, `tests/test_tile_tools.cpp`, `tests/test_tile_layers.cpp`

---

### March 24, 2026 - UI Scaling, Editor Input Fix, Reconnect Fix

Viewport-proportional UI scaling for mobile, editor tool-key leak fix, UI save persistence, and reconnect spam fix.

**Viewport-Proportional UI Scaling** (`engine/ui/ui_node.h`, `engine/ui/ui_node.cpp`, `engine/ui/ui_manager.h`, `engine/ui/ui_manager.cpp`):
- All UI pixel sizes, offsets, margins, and padding now scale by `screenHeight / 900.0f` (reference height matching the design viewport). Percentage-based dimensions are unaffected.
- `UINode::computeLayout()` accepts a `scale` parameter (default 1.0) propagated from `UIManager::computeLayout()` to all children. Each node stores `layoutScale_` for use by widget renderers.
- Updated 6 widget renderers to scale internal pixel sizes and font sizes: DPad, SkillArc, PlayerInfoBlock, MenuButtonRow, LeftSidebar, StatusPanel. Hit-test methods also scaled for correct touch targets.
- iPhone 16 Pro (852x393) gets scale ~0.44 — D-pad shrinks from 200px (51% of height) to 87px (22%), matching the same proportion as desktop. At default 1600x900 editor viewport, scale = 1.0 (no change).

**Editor Tool-Key Leak Fix** (`engine/editor/editor.cpp`):
- G (Fill), U (RectFill), L (Line) tool shortcuts were missing the `allowToolKeys` check that W/E/R/B/X already had. Pressing L during play mode activated the editor's Line tool. All three now gated by `allowToolKeys` (paused-only).

**UI Save Persistence** (`engine/editor/editor.cpp`, `engine/editor/ui_editor_panel.cpp`, `engine/editor/ui_editor_panel.h`):
- Ctrl+S and the "Save Screen" inspector button now save UI screens to **both** the build directory (relative path) and the source directory (`sourceDir_`). Previously only saved to the build dir, so changes were overwritten on next CMake build.
- Added `setSourceDir()` to `UIEditorPanel`, wired from `Editor::setSourceDir()`.

**Reconnect Spam Fix** (`engine/net/net_client.cpp`, `engine/net/net_client.h`):
- Fixed broken reconnect timer that used stale `lastHeartbeatSent_` timestamp for delta calculation. Replaced with separate `reconnectStartTime_` (total timeout baseline) and `reconnectLastTick_` (proper delta time).
- When a connect attempt times out during reconnect, the client now immediately transitions to `ReconnectPhase::Failed` (clears token, fires `onDisconnected`). Previously it just reset `waitingForAccept_` and closed the socket — the server's ConnectReject packet was lost, causing an infinite retry loop with the expired token (observed as 75+ rapid rejections flooding the server).

**Files changed:** `engine/ui/ui_node.h`, `engine/ui/ui_node.cpp`, `engine/ui/ui_manager.h`, `engine/ui/ui_manager.cpp`, `engine/ui/widgets/dpad.cpp`, `engine/ui/widgets/skill_arc.cpp`, `engine/ui/widgets/player_info_block.cpp`, `engine/ui/widgets/menu_button_row.cpp`, `engine/ui/widgets/left_sidebar.cpp`, `engine/ui/widgets/status_panel.cpp`, `engine/editor/editor.cpp`, `engine/editor/editor.h`, `engine/editor/ui_editor_panel.cpp`, `engine/editor/ui_editor_panel.h`, `engine/net/net_client.cpp`, `engine/net/net_client.h`, `tests/test_ui_input.cpp`

---

### March 24, 2026 - Editor Audit Bugfixes (Gemini Review)

Verified 9 findings from an external Gemini editor audit (5 correct, 2 wrong, 2 partially correct), then fixed all 6 confirmed issues.

**Dialogue Node Position Sync** (`engine/editor/node_editor.cpp`, `engine/editor/node_editor.h`):
- Loaded `posX`/`posY` were parsed but never applied to ImNodes — all nodes spawned at origin. Added `needsPositionApply_` flag; positions applied via `ImNodes::SetNodeEditorSpacePos()` on first frame after load.
- `saveToJson()` wrote stale struct values instead of actual dragged positions. Now reads back via `ImNodes::GetNodeEditorSpacePos()` before writing.

**UI Widget ID Collision Fix** (`engine/editor/ui_editor_panel.cpp`, `engine/editor/ui_editor_panel.h`):
- "Add Panel/Label/Button Child" generated IDs from `childCount()` — deleting then adding produced duplicate IDs that broke interactions and serialization. Replaced with monotonic `nextChildId_++` counter.

**Async Shell Commands** (`engine/editor/asset_browser.cpp`):
- "Open in VS Code" and "Show in Explorer" used synchronous `system()`. Prefixed with `start ""` so cmd.exe spawns the child process and returns immediately.

**Static Reference Elimination** (`engine/editor/animation_editor.cpp`, `engine/editor/animation_editor.h`):
- `currentFrameList()` returned a mutable reference to a `static` local vector — thread-unsafe and silently discarded modifications. Replaced with instance member `fallbackFrames_`.

**Dead Code Removal** (`engine/editor/editor.h`):
- Removed 5 unused multi-tile stamp variables (`stampStart_`, `stampEnd_`, `stampTiles_`, `stampWidth_`, `stampHeight_`).
- Removed unused legacy `isSelected(EntityId)` method (zero callers, O(n) linear scan over `std::set<EntityHandle>`).

**Audit findings rejected:**
- *Deletion flicker* (node_editor.cpp): `EndOutputAttribute()` is properly called before `break` — Begin/End pairs balanced.
- *Unconstrained text wrapping* (asset_browser.cpp): filenames are manually truncated before `TextWrapped` call.

**Files changed:** `engine/editor/node_editor.cpp`, `engine/editor/node_editor.h`, `engine/editor/ui_editor_panel.cpp`, `engine/editor/ui_editor_panel.h`, `engine/editor/asset_browser.cpp`, `engine/editor/animation_editor.cpp`, `engine/editor/animation_editor.h`, `engine/editor/editor.h`

---

### March 24, 2026 - Widget Theming Migration & MSDF Atlas Upgrade

Migrated 10 widgets from hardcoded structural colors to theme-based styling via `resolvedStyle_`, activated the MSDF shader path for resolution-independent text, and replaced the raster font atlas with a true MTSDF atlas.

**MSDF Shader Activation** (`engine/render/sdf_text.cpp`):
- `drawInternal()` was passing `renderType = 0.0f` (plain sprite path), ignoring the `TextStyle` parameter entirely. Changed to `static_cast<float>(style)` so text routes through the MSDF shader branch (renderType 1-4: Normal/Outlined/Glow/Shadow). The shader's `median(r,g,b)` + `screenPxRange` logic was already implemented but dead code — now active.

**MTSDF Atlas Generation** (`assets/fonts/default.png`, `assets/fonts/default.json`):
- Replaced the runtime-generated raster atlas (stb_truetype, Consolas, grayscale coverage) with a true MTSDF atlas generated offline via `msdf-atlas-gen v1.4`.
- Font: Inter-Regular.ttf, 512x512, 48px em size, 4px distance range, 177 glyphs.
- Character set: ASCII 32-126 + Latin-1 Supplement (U+00C0-U+00FF) + em-dash, en-dash, degree, multiplication, currency symbols, and other common punctuation.
- Added `yOrigin` handling in `loadMetrics()` — msdf-atlas-gen outputs `yOrigin:"bottom"` (atlas coords from bottom), so UV Y is flipped: `(atlasHeight - abTop) / atlasHeight`. The old raster generator used `yOrigin:"top"`.

**Theme System Expansion** (`assets/ui/themes/default.json`, `assets/ui/screens/fate_hud.json`):
- Added 8 new theme styles: `panel_hud`, `panel_hud_dark`, `bar_track`, `panel_dialog`, `tab_active`, `tab_inactive`, `scrollbar_track`, `scrollbar_thumb`.
- Wired 4 HUD widgets to styles in `fate_hud.json`: player_info→panel_hud, target_frame→panel_hud_dark, exp_bar→bar_track, dungeon_invite_dialog→panel_dialog.

**Widget Theming Migration** (10 widget files):
- Each widget reads `resolvedStyle_` for structural chrome (background, border, text color) with `alpha > 0` check and hardcoded fallback. Zero visual change when styles match fallback values.
- Migrated: `player_info_block`, `target_frame`, `exp_bar`, `boss_hp_bar`, `skill_panel`, `status_panel`, `chat_panel`, `confirm_dialog`, `slot` (hover border), `text_input` (focus border + placeholder).
- Semantic colors stay hardcoded: HP red, MP blue, XP gold, buff/debuff categories, chat channel colors, faction colors, rarity tiers, attack button red.

**Tests:** 956 total (954 pass, 2 pre-existing failures). 1 new test case (`UITheme: HUD styles load correctly`).

**Files changed:** `engine/render/sdf_text.cpp`, `assets/ui/themes/default.json`, `assets/ui/screens/fate_hud.json`, `tests/test_ui_theme.cpp`, `assets/fonts/default.png`, `assets/fonts/default.json`, `assets/fonts/charset.txt`, `engine/ui/widgets/player_info_block.cpp`, `engine/ui/widgets/target_frame.cpp`, `engine/ui/widgets/exp_bar.cpp`, `engine/ui/widgets/boss_hp_bar.cpp`, `engine/ui/widgets/skill_panel.cpp`, `engine/ui/widgets/status_panel.cpp`, `engine/ui/widgets/chat_panel.cpp`, `engine/ui/widgets/confirm_dialog.cpp`, `engine/ui/widgets/slot.cpp`, `engine/ui/widgets/text_input.cpp`

---

### March 24, 2026 - Editor Safety & Polish (10 Audit Fixes)

Full editor audit against Unity editor/game separation standards. Fixed 7 safety issues, added inspector undo, and hardened play-mode boundaries.

**Safe Entity Selection** (`engine/editor/editor.h`, `engine/editor/editor.cpp`):
- `selectedEntity_` was a raw `Entity*` that could dangle if the entity was destroyed by gameplay, network events, or undo. Added `EntityHandle selectedHandle_` as the authoritative selection. `refreshSelection(World*)` validates the handle each frame at the top of `renderUI()` — auto-clears if the entity is dead. All 12 assignment sites updated.

**Inspector Field Undo** (`engine/editor/editor.cpp`):
- All inspector component edits now have full undo/redo. Added `captureInspectorUndo()` (activate/deactivate snapshot pattern) after every `DragFloat`, `DragInt`, `InputText`, `Checkbox`, `ColorEdit4` widget (~116 call sites across 18 component sections + reflection fallback). Combo widgets use explicit before/after snapshot pattern. Undo/redo handlers changed from `clearSelection()` to `refreshSelection()` so the entity stays selected after reverting.

**Play-Mode Snapshot Filtering** (`engine/editor/editor.cpp`):
- `enterPlayMode()` now filters transient runtime entities (`mob`, `boss`, `player`, `ghost`, `dropped_item`) — same tags as `saveScene()`. Previously, playing then stopping while connected to a server would restore mobs/ghosts as permanent scene entities.

**Scene Ops Guarded During Play Mode** (`engine/editor/editor.cpp`):
- File > New Scene, Open Scene, Save, Save As menu items grayed out when `inPlayMode_` is true. Ctrl+S shortcut also blocked. Prevents corrupting `playModeSnapshot_` mid-play. `loadScene()` function itself remains unguarded since GameApp calls it for zone transitions.

**Entity Lock Enforcement** (`engine/editor/editor.cpp`):
- `isEntityLocked()` (tag == "ground") was defined but never called. Now enforced: ground tiles can be selected (inspector works) but cannot be dragged, resized, or deleted. Five guard sites: viewport click drag, resize handle, delete key, Entity > Delete menu, and the resize-handle hit test.

**Brush Stroke Undo Grouping** (`engine/editor/editor.h`, `engine/editor/editor.cpp`):
- Single-tile Paint tool now accumulates commands into a `CompoundCommand` during drag, pushed on mouse-up. A 50-tile brush stroke = 1 undo entry (was 50). `pendingBrushStroke_` reset on tool change and Escape. RectFill/LineTool/FloodFill already used this pattern.

**Game Logic Pause Gating** (`game/game_app.cpp`):
- `editorPaused` check moved earlier in InGame case. Death overlay, VFX (`SkillVFXPlayer`), and zone transitions now gated — they previously ticked during editor pause (zone transitions could destroy entities under the inspector). Network polling and audio intentionally kept running.

**Scene Save Hardening** (`engine/editor/editor.cpp`, `assets/scenes/WhisperingWoods.json`):
- `saveScene()` now filters `player`, `ghost`, and `dropped_item` tags (was only `mob`/`boss`). Removed stale player entity from WhisperingWoods.json. Auto-load code also strips leftover player entities via `forEach<PlayerController>` + destroy. Save log reports actual saved entity count instead of `world->entityCount()`.

**Duplicate `localPlayerPid_` Fix** (`game/game_app.h`):
- Removed duplicate declaration of `localPlayerPid_` (was defined on lines 102 and 108).

**Tests:** 956 total (954 pass, 2 pre-existing failures in test_ui_input.cpp). 4 new test cases, 31 assertions in `tests/test_editor_safety.cpp`.

**Files changed:** `engine/editor/editor.h`, `engine/editor/editor.cpp`, `game/game_app.h`, `game/game_app.cpp`, `assets/scenes/WhisperingWoods.json`, `tests/test_editor_safety.cpp`

---

### March 23, 2026 - Login Screen Fix, Input Separation & ImGui Game Rendering Removal

Fixed the retained-mode login screen (text invisible, input passthrough), established clean editor/game input separation, removed all ImGui rendering from the game client, and fixed several cascading UI/input bugs.

**SDF Text Rendering Fix** (`engine/render/sdf_text.cpp`):
- Root cause: atlas loaded with `stbi_set_flip_vertically_on_load(true)` but UV coordinates computed in image-space (y-down). After flip, all UV lookups hit empty pixels → alpha=0 → discarded. Fixed: load without flip.
- World-space text (nameplates, floating damage) needed `flipY = true` to correct UV-to-quad mapping in y-up coordinate space.
- Removed unused `atlasTexture_` shared_ptr from `sdf_text.h`.

**Editor/Game Input Separation** (`engine/app.cpp`, `engine/editor/editor.cpp`):
- Rewrote `processEvents()` with clean priority chain: **Paused** → ImGui + editor shortcuts, game gets nothing. **Playing** → ImGui (panels only) + UI text fields + game Input.
- Editor tool shortcuts (W=Move, E=Scale, R=Rotate, B=Paint, X=Erase, Delete) now gated behind `paused_` — no longer fire during gameplay.
- When paused, only `SDL_KEYUP` forwarded to game Input (stuck-key prevention). `SDL_KEYDOWN` fully blocked so ActionMap actions (chat toggle, movement) don't trigger while editing.
- Added `consumeKeyPress(SDL_Scancode)` and `consumeMousePress(int)` to `Input` class.

**UI Keyboard/Text Routing** (`engine/app.cpp`):
- When a UI node has focus (login screen text field, chat input), SDL_TEXTINPUT and SDL_KEYDOWN go to the focused node first. If consumed, event doesn't reach game Input.
- Shipping build has equivalent routing.

**ImGui Removed from Game Rendering** (`game/systems/combat_action_system.h`):
- Player nameplates, mob nameplates, quest markers, floating damage/XP text — all converted from `ImGui::GetForegroundDrawList()` to `SDFText::drawWorld()`. Now renders inside the Scene FBO (proper viewport containment, no editor bleed).
- 1px black outline via 8-pass offset rendering. Mob HP bars unchanged (already SpriteBatch).
- Removed `imgui.h`, `editor.h`/`editor_shim.h`, `game_viewport.h` includes. Removed `setViewportInfo()` and viewport members.

**UINode Parent-Aware Visibility** (`engine/ui/ui_node.h`, `engine/ui/ui_manager.cpp`):
- `visible()` now walks the parent chain — a node is only visible if ALL ancestors are visible. Added `visibleSelf()` for layout/render code that needs the raw flag.
- `handleInput()` clears stale `focusedNode_`/`pressedNode_`/`hoveredNode_` when they point to hidden nodes.
- `unloadScreen()` clears focus/hover/press if they point into the removed screen's subtree.
- Fixed `inventory_panel` missing `"visible": false` in `fate_menu_panels.json` — was defaulting to `true` despite parent screen being hidden, triggering `setUIBlocking(true)` and blocking WASD movement after login.

**Mouse Passthrough Fix** (`engine/app.cpp`):
- Moved `uiManager_.handleInput()` before `onUpdate()` so UI processes clicks first.
- Changed mouse consume check from `wantCaptureMouse()` (too broad — HUD StretchAll root always hit) to `pressedNode()` (only when a node's `onPress()` returned true).

**Re-Login Fix** (`engine/net/net_client.cpp`):
- `authToken_` cleared in `disconnect()` and `ConnectReject` handler. Previously stale tokens caused infinite reconnect loop with "Invalid or expired auth token" on every attempt.

**Inventory Panel Style Support** (`engine/ui/widgets/inventory_panel.cpp`):
- Background color, border color, and border width now read from `resolvedStyle_` with parchment fallback. UI Inspector color changes take effect immediately.

**Double Player Sprite Fix** (`game/game_app.cpp`):
- Server broadcasts `SvEntityEnter` for all players including yourself. Added filter: skip ghost creation when `entityType == 0 && name == pendingCharName_`. Stores `localPlayerPid_` for future use.

**Files changed:** 14 files (engine/app.cpp, engine/editor/editor.cpp, engine/input/input.h, engine/net/net_client.cpp, engine/render/sdf_text.cpp/.h, engine/ui/ui_manager.cpp, engine/ui/ui_node.h, engine/ui/widgets/inventory_panel.cpp, engine/ui/widgets/login_screen.cpp, game/game_app.cpp/.h, game/systems/combat_action_system.h, assets/ui/screens/fate_menu_panels.json)

---

### March 23, 2026 - Dungeon Client Wiring (End-to-End Playable Dungeons)

Wired the client side of the instanced dungeon system — server was already fully built (per-party ECS worlds, timers, boss rewards, daily tickets). Dungeons are now end-to-end playable.

**New Component:**
- `DungeonNPCComponent` (`game/components/game_components.h`) — marks an NPC as a dungeon entrance, holds `dungeonSceneId`. Registered in component registry. NpcDialoguePanel shows "Dungeon" role button when present.

**NetClient Wiring** (`engine/net/net_client.h/.cpp`):
- 3 new callbacks: `onDungeonInvite`, `onDungeonStart`, `onDungeonEnd`
- 2 new send methods: `sendStartDungeon(sceneId)`, `sendDungeonResponse(accept)`
- 3 switch cases for opcodes 0xB4/0xB5/0xB6 using static factory `read()` pattern

**NPC Dialogue** (`engine/ui/widgets/npc_dialogue_panel.h/.cpp`):
- `hasDungeon` flag + `onOpenDungeon` callback — 5th role button alongside Shop/Bank/Teleport/Guild
- Clicking "Dungeon" sends `CmdStartDungeon` with the NPC's configured `dungeonSceneId`

**game_app.cpp Callbacks:**
- `onDungeonInvite` — shows ConfirmDialog ("Ready to start {name} dungeon?" Accept/Decline)
- `onDungeonStart` — hides invite dialog, posts "[System] Entering dungeon..." chat, triggers deferred zone transition
- `onDungeonEnd` — posts "[System] Dungeon instance has ended ({reason})." chat message
- Extracted `captureLocalPlayerState()` helper (deduplicates zone transition + dungeon start state capture)
- `dungeonInviteDialog_` resolved from `fate_hud.json` ConfirmDialog node (zOrder 100, centered)

**Server Additions:**
- Per-minute "[System] Dungeon time remaining: X minutes" chat broadcast to all instance players
- 10-second decline cooldown per party — prevents invite spam after a member declines

**Message Struct Fix:**
- All 5 dungeon message structs (`CmdStartDungeonMsg`, `CmdDungeonResponseMsg`, `SvDungeonInviteMsg`, `SvDungeonStartMsg`, `SvDungeonEndMsg`) converted from instance `read()` to static factory `read()` pattern, matching every other message struct in the codebase

**Tests:** 6 new tests (5 serialization round-trips + 1 cooldown unit test), all passing. TwoBotFixture added to scenario framework for multi-player integration tests.

---

### March 23, 2026 - Skill VFX Pipeline (TWOM-Style Sprite Sheet + Particle Effects)

Built the composable skill VFX system using pre-rendered sprite sheet animations with optional particle embellishments — matching TWOM's visual style.

**VFX Definition Format** (`assets/vfx/*.json`):
- 4 optional phases: Cast (at caster), Projectile (travels caster→target), Impact (at target), Area (lingers at target with looping)
- Each phase: sprite sheet PNG strip + frameCount/frameSize/frameRate + pixel offset
- Optional particle embellishment per phase (count, color interpolation, speed, gravity, size)
- Area phase supports duration + looping for persistent ground effects (poison cloud, heal circle)

**SkillVFXPlayer** (`engine/vfx/skill_vfx_player.h/.cpp`):
- Singleton manager: loadDefinitions, play, update, render, clear
- Phase sequencing: Cast→Projectile→Impact→Area→Done (disabled phases skipped automatically)
- Projectile movement with arrival detection + overshoot handling
- Area looping with duration expiry
- 32 max active effects with oldest-eviction
- Renders sprite sheet frames via SpriteBatch::draw() with computed UV source rects
- Particle emitters created per-phase from VFXParticleConfig

**Integration** (`game/game_app.cpp`):
- Definitions loaded at startup from `assets/vfx/`
- New "SkillVFX" render pass in render graph (after Particles, before SDFText)
- VFX triggered on `SvSkillResultMsg` — client looks up skill's `vfxId` from `ClientSkillDefinitionCache`
- Per-frame update alongside FloatingText/DeathOverlay
- Cleared on disconnect and zone transitions

**Skill Definition Binding:**
- `vfxId` field added to `ClientSkillDef` and `CachedSkillDef`
- Server loads `vfx_id` column from `skill_definitions` table (auto-migration with graceful fallback)

**Example Definitions:** 3 JSON files — `slash.json` (impact-only melee), `fireball.json` (cast+projectile+impact), `heal.json` (cast-only self-buff)

**Tests:** 13 new (5 definition parsing + 8 player core) — all passing

---

### March 23, 2026 - ImGui Fully Retired (Data-Routing Migration)

Completed the ImGui retirement by migrating the 5 remaining data-routing classes to retained-mode equivalents and deleting them (~1,762 LOC).

**DeathOverlay Widget** (`engine/ui/widgets/death_overlay.h/.cpp`):
- New UINode subclass replacing DeathOverlayUI. Owns death state (xpLost, honorLost, countdown, deathSource, respawnPending). Renders fullscreen dark overlay with "You have died" title, loss text, countdown timer, 3 respawn buttons. Aurora deaths hide non-town respawn options. `update(dt)` ticks countdown. 6 tests.

**SkillArc Page Management** (`engine/ui/widgets/skill_arc.h/.cpp`):
- Extended with `currentPage`, `SLOTS_PER_PAGE` (5), `TOTAL_PAGES` (4), `nextPage()`/`prevPage()` with wrapping, `onSkillActivated` callback. Replaces SkillBarUI singleton. 4 new tests.

**ChatPanel API Update** (`engine/ui/widgets/chat_panel.h/.cpp`):
- `onSendMessage` callback updated to include `targetName` parameter for private whispers
- New `addMessage(channel, sender, msg, faction)` overload that converts faction ID to color (Xyros red, Fenor blue, Zethos green, Solis gold)

**game_app.cpp Rewiring** (~45 call sites):
- All `chatUI_.` → `chatPanel_->`, `deathOverlayUI_.` → `deathOverlay_->`, `SkillBarUI::instance().` → `skillArc_->`, `InventoryUI::instance().` → `inventoryPanel_->`, `HudBarsUI::instance().setViewportRect()` → deleted
- Widget pointers resolved from uiManager screen tree in `retainedUILoaded_` block
- Death overlay ticked per-frame with `deathOverlay_->update(dt)`

**Deleted:** 5 ImGui classes (10 files, ~1,762 LOC): ChatUI, DeathOverlayUI, SkillBarUI, InventoryUI, HudBarsUI

**ImGui is now fully retired from the game client.** Zero ImGui classes remain in `game/ui/`. ImGui is only used by the editor (`FATEMMO_EDITOR_BUILD`) and stripped from shipping builds (`FATE_SHIPPING`).

**Tests:** 933 total (931 pass, 2 pre-existing failures)

---

### March 23, 2026 - ImGui Retirement (Retained-Mode LoginScreen, Dead Code Cleanup, Shipping Strip)

Retired ImGui from the game client. The retained-mode UI system now handles all game UI including login/registration.

**New Widgets:**
- **LoginScreen** (`engine/ui/widgets/login_screen.h/.cpp`) — retained-mode login/register form replacing ImGui LoginScreen. Login + Register modes, server selector, remember-me persistence (saves username/server to `assets/config/login_prefs.json`, never passwords). Registration flow transitions to existing CharacterCreationScreen for name/class/faction selection. Validates via `AuthValidation` before firing callbacks.
- **Checkbox** (`engine/ui/widgets/checkbox.h/.cpp`) — toggle widget with box + label text, used for Remember Me
- **TextInput masked mode** — new `bool masked` property renders `*` characters for password fields

**Login State Machine Rewired** (`game/game_app.h/.cpp`):
- Replaced polled-flag ImGui LoginScreen (`loginSubmitted`/`registerSubmitted`) with callback-driven retained-mode widget (`onLogin`/`onRegister`)
- Added `ConnectionState::CharacterCreation` for registration flow via CharacterCreationScreen
- Login/character creation screens loaded at startup (not after InGame)

**Dead Code Deleted:**
- `game/ui/login_screen.h/.cpp` — replaced by retained-mode widget
- `game/ui/touch_controls.h/.cpp` — zero live calls, replaced by DPad + SkillArc
- 3 legacy JSON screens (`hud_bars.json`, `skill_bar.json`, `inventory.json`)
- Commented-out ImGui draw calls in onRender

**ImGui Stripped from Shipping Builds:**
- All ImGui init/frame/render/shutdown guarded with `#ifndef FATE_SHIPPING`
- `imgui_lib` conditionally linked only for non-shipping builds
- Editor build unchanged — full ImGui for property inspectors, scene tools, debug panels

**Still Using ImGui (live non-draw call sites, future migration):** ChatUI (~18 calls), DeathOverlayUI (~13), SkillBarUI (~10), InventoryUI (~3), HudBarsUI (1)

**Tests:** 927 total (925 pass, 2 pre-existing failures)

---

### March 23, 2026 - Aurora Rotation System (6-Zone PvP Gauntlet)

Implemented the Aurora rotation system — a TWOM-inspired faction-rotation PvP zone chain with hourly buff cycling and world boss.

**Aurora Rotation Core** (`game/shared/aurora_rotation.h`):
- Pure wall-clock rotation: `hour % 4` maps to Xyros → Fenor → Zethos → Solis
- `getFavoredFaction(time_t)` + `getTimeUntilNextRotation(time_t)` — stateless, restart-proof
- Server tick (1/sec) detects rotation changes, broadcasts system chat announcement

**Buff System** (`server/handlers/aurora_handler.cpp`):
- Favored faction inside Aurora zones receives +25% attack damage (`AttackUp`) + +25% exp gain (`ExpGainUp`)
- New `EffectType::ExpGainUp` (slot 16) with `StatusEffectManager::getExpGainBonus()`
- ExpGainUp wired into both XP award sites in `combat_handler.cpp` (skill-kill + auto-attack)
- Buff duration = time remaining in current hour; auto-expires naturally
- Applied on: zone entry, NPC teleport entry, login while in Aurora, rotation change
- Removed on: zone exit, death, voluntary recall

**Zone Structure** (6 overworld scenes, PvP-enabled):
- `aurora_1` (Aurora Threshold) → `aurora_2` (Aurora Ascent) → `aurora_3` (Aurora Veil) → `aurora_4` (Aurora Pinnacle) → `aurora_5` (Aurora Sanctum) → `aurora_borealis` (Borealis)
- One-way forward portals between zones (no backward traversal)
- `SceneInfoRecord::isAurora` + `Scene::Metadata::isAurora` flag loaded from DB

**Entry System**:
- NPC teleporter with Aether Stone material + 50,000 gold cost
- `TeleportDestination` extended with `requiredItem` / `requiredItemQty` fields (backward-compatible defaults)
- Teleport handler: item cost validation + consumption + cross-scene replication state clearing (pre-existing bug fix)
- `SvTeleportResult` client callback wired (was missing — needed for any cross-scene NPC teleport)

**Death & Exit**:
- `DeathSource::Aurora` (value 7) added — death UI shows only "Return to Town"
- All 5 death notification sites patched (PvE, PvP skill, PvP auto-attack, DoT, reconnect)
- Aurora recall: living players can use CmdRespawn type 0 to voluntarily exit to Town
- `needsFirstMoveSync_` set on all Aurora scene changes (prevents position desync)

**Network Protocol**:
- `SvAuroraStatusMsg` (PacketType 0xB9): `favoredFaction` (uint8) + `secondsRemaining` (uint32)
- Sent on zone entry, login, rotation change

**Aether World Boss** (Lv55, Borealis):
- 150M HP, 700 damage, 1200 armor, 0.35 crit, 155 magic resist
- 36hr respawn (129,600s), 200K XP, 333 honor, 50K-150K gold
- 23-item loot table: guaranteed HP/MP potions + Aether Stone (re-entry), 3 Legendary weapons (2%), 5 Epic armor (2%), 6 Lv55 skillbooks (1%), 3 stat scrolls (1%), Master Enhancement Stones (3%/1%), ultra-rare Fate-Touched Stormcaller's (0.1%)

**Aether Stone Economy**:
- Drops from 13 overworld elites/bosses (Lv25-60), scaling 1%-10% by mob tier
- Sources: Quartz Juggernaut (1%), Crystal Weaver (1%), Core Guardian (3%), Dune Scorpion King (3%), Sand Shark (2%), Wasteland Behemoth (2%), Pharaoh's Shadow (5%), Sludge Amalgam (3%), Flesh Construct (3%), Lich Lord (8%), Tempest Griffin (4%), Aether Drake (5%), Celestial Dragon (10%)
- Also guaranteed 1-2 drop from Aether itself (funds re-entry)

**Database** (migrations 016 + 017):
- 016: `ALTER TABLE scenes ADD COLUMN is_aurora`, 6 Aurora scene rows, Aether Stone item
- 017: Aether mob_definition, loot_aether table (23 drops), Aether Stone added to 13 overworld loot tables

**Client**:
- `onAuroraStatus` callback logs favored faction
- Death overlay UI hides "Respawn at Spawn" and "Phoenix Down" buttons for Aurora deaths
- Buttons restored on respawn for subsequent non-Aurora deaths

**Aurora Zone Scaling** (mob levels for when maps are built):
- `aurora_1` (Threshold) — Lv10 | `aurora_2` (Ascent) — Lv20 | `aurora_3` (Veil) — Lv30
- `aurora_4` (Pinnacle) — Lv40 | `aurora_5` (Sanctum) — Lv50 | `aurora_borealis` (Borealis) — Aether Lv55

**Tests:** 10 new test cases (2 rotation pure functions, 2 getExpGainBonus, 6 integration)

**Files:** 4 created (`aurora_rotation.h`, `aurora_handler.cpp`, `test_aurora_rotation.cpp`, `test_aurora_system.cpp`), 14 modified, 2 migration SQL files

**Deferred:** Per-zone mob spawn_zones, scene JSON map files, Aurora entrance NPC placement, BossSpawnPoint for Aether in Borealis

---

### March 23, 2026 - TWOM-Style UI Widgets (BuffBar, FloatingText, ConfirmDialog, NotificationToast, BossHPBar, CooldownOverlay)

Added 6 TWOM-inspired UI features: 4 new retained-mode widgets, a world-space floating combat text system, and enhanced skill cooldown overlays.

**BuffBar** (`engine/ui/widgets/buff_bar.h/.cpp`):
- Horizontal row of status effect icons below the HP bar
- Color-coded: red (debuffs: Bleed/Burn/Poison/Slow/ArmorShred/HuntersMark), green (buffs: AttackUp/ArmorUp/SpeedUp/ManaRegenUp/ExpGainUp), blue (shields: Shield/Invulnerable/StunImmune/GuaranteedCrit), yellow (utility: Transform/Bewitched)
- Clockwise radial sweep overlay showing remaining duration via `drawArc`
- Stack count badge (bottom-right) when stacks > 1
- Configurable: `iconSize` (24px), `spacing` (3px), `maxVisible` (12)

**BossHPBar** (`engine/ui/widgets/boss_hp_bar.h/.cpp`):
- Full-width bar for field boss / dungeon boss engagement
- Dark semi-transparent background, gold boss name, red HP fill with percentage text
- Hidden by default (`visible_ = false`), shown by game code when boss engaged

**ConfirmDialog** (`engine/ui/widgets/confirm_dialog.h/.cpp`):
- Modal yes/no popup for destructive actions (enchant, delete, spend gold, leave party)
- Dark panel with gold border, centered message, Confirm/Cancel buttons
- `onPress` always returns true (modal — blocks clicks through)
- Configurable message, button text, button sizing

**NotificationToast** (`engine/ui/widgets/notification_toast.h/.cpp`):
- Stacking notification banners (loot, level-up, quest completion, system announcements)
- 4 toast types: Info (blue), Success (green), Warning (yellow), Error (red) — each with colored left accent strip
- Fade-in/hold/fade-out alpha animation, configurable timing
- Queue management with `maxToasts` (5), expired toast auto-removal

**FloatingTextManager** (`engine/render/floating_text.h/.cpp`):
- World-space combat text system (not a UINode — renders via SpriteBatch/SDFText)
- 10 text types: Damage, CritDamage, Heal, Miss, Block, Absorb, Dodge, XPGain, GoldGain, LevelUp
- Per-type colors, font sizes, and animation behavior
- Crit damage: 1.5x scale punch that lerps to 1.0x over 0.2s
- Alpha: full for first 60% of lifetime, linear fade over last 40%
- Deterministic X offset to prevent stacking, 64-entry cap

**CooldownOverlay Enhancement** (`engine/ui/widgets/skill_arc.cpp`):
- Doubled segment count (16→32) for smoother radial sweep
- Added leading edge highlight (thin white line at sweep boundary)
- Added remaining seconds text centered on slot during cooldown

**Tests:** 39 new — 922 total (920 pass, 2 pre-existing failures in test_ui_input.cpp)

**Files:** 10 created (5 widget h/cpp pairs + 5 test files), 4 modified (ui_manager.cpp, ui_serializer.cpp, skill_arc.cpp, plus earlier session files)

---

### March 23, 2026 - ImageBox Widget & ScrollView Scissor Clipping

Added two missing UI widgets: ImageBox for texture display and scissor clipping for ScrollView.

**ImageBox Widget** (`engine/ui/widgets/image_box.h/.cpp`):
- New `UINode` subclass for rendering textures in the retained-mode UI
- `textureKey` — path/key for `TextureCache` lookup (lazy-loads on first render)
- `fitMode` — `Stretch` (fills widget, may distort) or `Fit` (preserves aspect ratio, letterboxes)
- `tint` — color multiplier (useful for greying out disabled items), modulated by style opacity
- `sourceRect` — normalized UV rect for atlas sub-region support
- Full JSON serialization round-trip via `UISerializer` and `UIManager::parseNode`

**ScrollView Scissor Clipping** (`engine/ui/widgets/scroll_view.cpp`):
- Children now pixel-clipped to scroll viewport via `pushScissorRect`/`popScissorRect`
- Partially visible items at scroll boundaries are properly clipped instead of fully shown or fully hidden
- Still skips fully off-screen children for draw call savings

**SpriteBatch Scissor Stack** (`engine/render/sprite_batch.h/.cpp`):
- `pushScissorRect(Rect)` / `popScissorRect()` — nestable stack with rect intersection
- Forces mid-frame sort+flush at scissor boundaries (entries accumulated so far are drawn before scissor state changes)
- GL: `glScissor` with Y-flip for bottom-left origin (queries viewport via `glGetIntegerv`)
- Metal: `setScissorRect:` on encoder (top-left origin, no flip needed)

**Tests:** 7 new (4 ImageBox + 3 ScrollView) — 884 total (882 pass, 2 pre-existing failures in test_ui_input.cpp)

**Files:** 2 created (`image_box.h/.cpp`), 2 test files created, 5 modified (`sprite_batch.h/.cpp`, `scroll_view.cpp`, `ui_manager.cpp`, `ui_serializer.cpp`)

---

### March 23, 2026 - NPC UI Retained-Mode Migration

Migrated all 6 ImGui NPC panels to the retained-mode UI system. 4 panels rebuilt as retained-mode widgets (NpcDialogue, Shop, Bank, Teleporter), 2 removed as dead code (SkillTrainer, QuestLog). All NPC transactions are now server-authoritative with NPC proximity validation.

**New Retained-Mode Panels (4 UINode subclasses):**
- **NpcDialoguePanel** (604 LOC) — NPC interaction hub with conditional role buttons (Shop/Bank/Teleport/Guild) based on NPC components. Quest accept/complete section. Story NPC mode with branching dialogue tree
- **ShopPanel** (693 LOC) — Dual-pane: NPC shop items on left with Buy buttons, player 4×4 inventory on right. Double-click non-soulbound item to sell with quantity confirmation popup
- **BankPanel** (~600 LOC) — Dual-pane: bank stored items on left with Withdraw buttons, player 4×4 inventory on right (click to deposit). Bottom bar with gold deposit/withdraw and flat 5,000 fee display
- **TeleporterPanel** (~300 LOC) — Destination list with level/cost gating, disabled entries with red requirement text

**Network Protocol (7 new server-authoritative message pairs):**
- `CmdShopBuy/Sell` → `SvShopResult` — shop buy/sell with NPC proximity + ShopComponent validation
- `CmdBankDepositItem/WithdrawItem/DepositGold/WithdrawGold` → `SvBankResult` — split from monolithic CmdBank, each with NPC proximity validation
- `CmdTeleport` → `SvTeleportResult` — index-based (server resolves destination, doesn't trust client data)

**Bank Fee Change:** Deposit fee changed from 2% to flat 5,000 gold (`BankStorage::BANK_DEPOSIT_FEE` constant shared by client + server). `BankerComponent.depositFeePercent` removed.

**Dead Code Removed:**
- SkillTrainerUI + SkillTrainerComponent + TrainableSkill struct (skill system uses leveling/skill books/point allocation, not NPC trainers)
- QuestLogUI (quest state hidden from player, managed through NPC dialogue)

**Server Handlers:**
- New `shop_handler.cpp` (processShopBuy/Sell) and `teleport_handler.cpp` (processTeleport)
- `bank_handler.cpp` rewritten: monolithic `processBank` split into 4 methods, each with NPC proximity validation
- Rate limiter updated for 7 new packet types (replacing single CmdBank entry)

**ImGui Removal:** Removed `#include "imgui.h"` from `npc_interaction_system.h`. Replaced `ImGui::GetIO().WantCaptureMouse` with `UIManager::wantCaptureMouse()` and `ImGui::GetIO().DisplaySize` with `GameViewport::width()/height()`.

**Tests:** 880 total (878 pass, 2 pre-existing failures)

**Files:** 8 deleted, 9 created, ~15 modified

---

### March 23, 2026 - Boss/Mini-Boss Combat Leash

Added combat leash mechanic for bosses and mini-bosses: if no player deals damage for 15 seconds, the mob resets to full HP and clears its threat table. Prevents players from chipping away over multiple lives and forces committed fights. Applies to MiniBoss, Boss, and RaidBoss types in both the main world and dungeon instances. Regular mobs are unaffected.

**Implementation:**
- `EnemyStats` — new `lastDamageTime` field, `LEASH_TIMEOUT = 15s` constant, `tickLeash(gameTime)` method that resets HP + clears threat when timeout elapses
- `combat_handler.cpp` — stamps `lastDamageTime = gameTime_` after both attack and skill damage paths
- `server_app.cpp` — new tick step 3b: iterates all non-Normal mobs in main world + dungeon instances, calls `tickLeash()`, debug-logs resets

**Tests:** 4 new (timeout reset, full HP skip, dead skip, never-hit skip) — 940 total

**Files changed:** `enemy_stats.h`, `enemy_stats.cpp`, `combat_handler.cpp`, `server_app.cpp`, `test_enemy_stats.cpp`

---

### March 22, 2026 - Input Fixes & Server Tick Profiling

Fixed game keyboard shortcuts, D-pad hit testing, and added per-section tick profiling to the server.

**Input — Keyboard Shortcuts (game_app.cpp):**
- Removed redundant `!Editor::instance().wantsKeyboard()` guards from all 6 game shortcut checks (ToggleInventory, ToggleSkillBar, ToggleQuestLog, OpenChat, SkillPagePrev/Next). The keyboard routing in `app.cpp` already gates events when the editor is paused, so the second check was incorrectly blocking all shortcuts during play mode (ImGui panels always report wanting keyboard)

**Input — Deprecated F3 Editor Toggle (action_map.h):**
- Removed dead `ToggleEditor` ActionId and its F3 keybinding (never consumed anywhere)

**UI — D-pad Hit-Test / Render Mismatch (dpad.cpp):**
- `isInsideCross()` used `0.35f`/`0.9f` arm proportions but `render()` drew `0.32f`/`0.85f`. Aligned hit-test to match render

**UI — Viewport Input Transform (ui_manager.h/.cpp, app.cpp):**
- Added `setInputTransform(offsetX, offsetY, scaleX, scaleY)` replacing `setInputOffset()`. Mouse coordinates are now translated (subtract viewport position) AND scaled (multiply by FBO/display ratio) so hit testing works correctly across all editor device resolution presets. Previously only iPad Pro 12.9" worked because its FBO-to-display scale was ~1:1

**Server — Tick Profiling (server_app.cpp):**
- Replaced single "Slow tick" warning with per-section breakdown. When a tick exceeds 50ms, logs: `net`, `world`, `ecs`, `despawn`, `repl`, `events`, `save` with individual ms values. Identified that slow ticks in local dev are caused by remote DB latency (`save`) and initial connection handshake (`net`), not game logic

**Files changed:** `action_map.h`, `game_app.cpp`, `dpad.cpp`, `ui_manager.h`, `ui_manager.cpp`, `app.cpp`, `server_app.cpp`

---

### March 22, 2026 - Phase 27: Stat Enchant Client Wiring

Wired stat enchant end-to-end. Player drags a stat scroll (`item_scroll_stat_*`) onto an unequipped accessory (Belt/Ring/Necklace/Cloak) in the inventory grid. Server validates, consumes scroll, rolls tier, applies enchant, returns result. Client shows feedback in ChatTicker + SFX.

**Protocol:**
- `CmdStatEnchantMsg.equipSlot` renamed to `targetSlot` (now inventory slot index, not equipment slot)
- Server ignores `scrollStatType` — derives stat type from scroll's item definition (`attributes.stat_type`) via `ItemDefinitionCache::getStatTypeForScroll()`

**Server:**
- `processStatEnchant` rewritten: reads target from `Inventory::getSlot()`, validates via `isAccessory()` on item definition, derives stat type from DB, writes back via new `Inventory::setSlot()`. No `recalcEquipmentBonuses` (item is unequipped). Trade-lock checks on both scroll and target slots preserved

**Client:**
- `NetClient::sendStatEnchant(targetSlot, scrollItemId)` — new send method
- `InventoryPanel` drag-and-drop: press on inventory slot initiates drag, release on different slot with `item_scroll_stat_*` prefix triggers `onStatEnchantRequest` callback. Blue accent highlight on source slot during drag
- `game_app.cpp`: wires `onStatEnchantRequest` → `sendStatEnchant`, wires `onStatEnchantResult` → ChatTicker message + enchant SFX

**Data:**
- `Inventory::setSlot(int, ItemInstance)` — new method for in-place item replacement (2 tests)
- `ItemDefinitionCache::getStatTypeForScroll()` — looks up `stat_type` attribute from item definition
- Migration `015_stat_enchant_scrolls.sql` — 7 stat scroll consumables (STR/INT/DEX/VIT/WIS/HP/MP)

**Files changed:** `game_messages.h`, `net_client.h/.cpp`, `inventory.h/.cpp`, `item_definition_cache.h`, `crafting_handler.cpp`, `inventory_panel.h/.cpp`, `game_app.cpp`, `test_inventory.cpp`

---

### March 22, 2026 - Phase 26: Broad Audit Sweep — 13 Confirmed Fixes + Pre-existing Build Fixes

Verified 14 audit findings across server security, economy, combat, and character systems. 13 confirmed and fixed, 1 deferred (NonceManager protocol wiring). Also fixed 4 pre-existing build issues exposed by CMake reconfigure.

**Economy & Trade Exploits (4 fixes):**
- **#18 Bank withdraw WAL:** Added `wal_.appendGoldChange()` to gold withdrawal path — deposit was WAL-protected but withdrawal was not (gold loss on crash)
- **#23 Bounty refund gold convention:** Changed `addGold(refund)` to `setGold(getGold() + refund)` in CmdBounty CancelBounty handler (server-authoritative gold convention)
- **#25 Crafting trade-lock bypass:** Added `isSlotLocked()` checks to 5 crafting operations (repair, extract core, craft, socket, stat enchant) — only `processEnchant` had the check. Core extraction could **delete** trade-locked items. Socket/stat enchant also check `isInTrade()` for equipment modifications
- **#29 Trade scene client-side SceneManager:** Replaced `SceneManager::instance().currentScene()` (always nullptr on server) with `charStats->stats.currentScene` for trade session scene tracking

**Character & Combat (4 fixes):**
- **#19 Gauntlet double-counting deaths:** Removed `deaths++` from `onPlayerKilled` — `onPlayerDied` is the canonical death counter
- **#20 Passive skill bonuses lost on relog:** Added `recomputePassiveBonuses()` method that iterates learned passive skills and reaccumulates HP/crit/speed/damageReduction/stat bonuses. Called after skill definitions are registered on relog
- **#24 maxHP/maxMP negative clamp:** Added `std::max(1, maxHP)` and `std::max(1, maxMP)` after stat calculation — corrupted equipment bonuses could cause respawn-death loops
- **#26 Gauntlet duration wrong time:** Changed `completeMatch()` to accept `currentTime` parameter instead of using `waveStartTime` (undercounted match duration)

**Server Security (4 fixes):**
- **#22 Registration className whitelist:** Added validation rejecting any className not in {"Warrior", "Mage", "Archer"} — previously accepted arbitrary strings
- **#28 Gauntlet return scene client-side SceneManager:** Same SceneManager fix as #29, used `charStats->stats.currentScene` with "WhisperingWoods" fallback
- **#31 Pending auth sessions DoS:** Added `MAX_PENDING_SESSIONS = 1000` cap — `pendingSessions_` map had no size limit, vulnerable to memory exhaustion under auth flooding
- **#32 Arena mode enum validation:** Added bounds check (`msg.mode < 1 || > 3`) before `static_cast<ArenaMode>` — invalid uint8_t values created undefined enum behavior
- **#34 Dungeon respawn escape:** Added `dungeonManager_.getInstanceForClient()` check before town respawn — players could escape dungeon instances via CmdRespawn

**Pre-existing Build Fixes (4):**
- `combat_handler.cpp`: Added missing `#include "game/entity_factory.h"` (EntityFactory undeclared)
- `equipment_handler.cpp`: Added missing `#include "game/components/pet_component.h"` (PetComponent undeclared)
- `server_app.cpp` CancelListing: Added entity/inventory lookup + error lambda (was using out-of-scope `inv`/`sendMarketError` from BuyItem case)
- `test_player_lock.cpp`: Fixed binding rvalue `shared_ptr` to lvalue reference (PlayerLockMap::get returns by value)

**Deferred:**
- **#17 NonceManager wiring:** `issue()`/`consume()` require protocol changes (nonce fields in CmdTrade/CmdMarket messages + client support). Tracked separately

**Files changed:** `gauntlet.cpp`, `gauntlet.h`, `gauntlet_handler.cpp`, `bank_handler.cpp`, `skill_manager.h`, `skill_manager.cpp`, `character_stats.cpp`, `crafting_handler.cpp`, `auth_server.cpp`, `pvp_event_handler.cpp`, `server_app.cpp`, `combat_handler.cpp`, `equipment_handler.cpp`, `test_player_lock.cpp`

---

### March 22, 2026 - Phase 25: Economy & Precision Audit (7 fixes)

Verified 7 audit findings from a targeted review of trade, market, inventory, and numeric precision code. 6 confirmed and fixed, 1 false positive.

**Critical Economy Exploits (3 fixes):**
- **#8 Trade item re-validation:** Trade confirm now verifies all offered items still exist in both players' inventories (via `findByInstanceId`) before executing. Aborts with error if any item was consumed/sold between lock and confirm
- **#9 Market cancel item return:** `CancelListing` handler now fetches listing record, reconstructs full `ItemInstance` (rolled stats, socket, display name, rarity), adds back to seller inventory, persists, and sends updated state. Was: DB row deactivated but item permanently lost (H2 doc fix was incomplete — handler was never wired)
- **#11 Trade gold re-validation:** Added `goldA >= offeredGold` / `goldB >= offeredGold` check before transaction. `updateGold()` return values now checked — failure throws `runtime_error`, aborting the entire transaction (no partial item+gold transfer)

**Equipment Stacking Exploit (1 fix):**
- **#6 Equipment stacking destroys unique items:** `moveItem()` now requires `from.isStackable() && to.isStackable()` before merging. New `ItemInstance::isStackable()` returns false for items with enchants, rolled stats, sockets, stat enchants, or bag status. Non-stackable items with matching `itemId` fall through to swap. Was: any two items with same `itemId` would stack regardless of instance data — a +0 sword merged onto a +15 produced "stack of 2" with +15 stats

**Numeric Precision (2 fixes):**
- **#12 Market tax float→int64:** `calculateTax()` return type changed from `float` to `int64_t`, uses `double` internally. Prevents precision loss above 16M gold (float has only 24-bit mantissa)
- **#13 XP loss float→double:** `applyPvEDeathXPLoss()` changed `static_cast<float>` to `static_cast<double>` for both `currentXP` and `lossPercent`. Correct XP loss at high levels

**Defensive Hardening (1 fix):**
- **#14 setSerializedState gold clamp:** `gold_ = gold` → `gold_ = std::clamp(gold, 0, MAX_GOLD)`. Every other gold setter already clamped; corrupted DB data no longer propagates

**False Positive (1):**
- **#10 Core extraction slot corruption:** Claimed `removeItem(itemSlot)` shifts higher slots, corrupting `scrollSlot`. Actually, `removeItem()` just sets `slots_[i] = empty()` — fixed-size array with gaps, no shifting. Enchant handler's higher-index-first removal is defensive but not required

**Files changed:** `item_instance.h`, `inventory.h`, `inventory.cpp`, `server_app.cpp`, `market_manager.h`, `market_manager.cpp`, `character_stats.cpp`

---

### March 22, 2026 - Phase 24: Fiber-Based Async Asset Loading + Bugfix Sweep (5 fixes)

Moved asset deserialization from the main thread to fiber jobs. Texture decoding (stbi_load) and KTX file reads now run on worker fibers; only GPU upload remains on the main thread. Also swept 4 carried/audit bugs.

**Async Asset Loading (engine/asset/):**
- `AssetRegistry::loadAsync()` submits decode to fiber job system, returns handle immediately in `Loading` state
- `AssetRegistry::processAsyncLoads()` called each frame — finalizes decoded assets on main thread (GPU upload for textures, direct assignment for JSON)
- `AssetLoader` extended with optional `decode`/`upload`/`destroyDecoded` callbacks; loaders without `decode` fall back to synchronous `load()`
- `AssetSlot` state machine: `Empty → Loading → Ready | Failed` (replaces `bool loaded`)
- Texture loader split: `textureDecodeAsync()` (fiber-safe stbi_load + KTX read, `stbi_set_flip_vertically_on_load_thread` for thread safety) + `textureUploadToGPU()` (main-thread `createTexture`/`createCompressedTexture`)
- JSON loader: `decode = jsonLoad` (CPU-only, no upload step needed); Shader loader: sync-only (requires GL context)
- `Texture::loadFromMemoryCompressed()` added for async KTX GPU upload path
- `TextureCache::requestAsyncLoad()` now delegates to registry fiber pipeline (replaced detached `std::thread`)
- `App::update()` calls `processAsyncLoads()` after `processReloads()` each frame
- 5 new tests: async decode-only, async with upload, failure handling, deduplication, sync fallback

**Bugfix Sweep (4 fixes):**
- **#30 Chat profanity preview:** `ProfanityFilter::filterChatMessage(msg, FilterMode::Censor)` applied client-side at both chat send paths in `game_app.cpp` — sender now sees censored preview matching what others receive
- **#31 Trade gold validation:** `TradeManager::setGoldOffer()` gains `onQueryGold` callback and rejects amounts exceeding player balance (prep for when gold input UI is wired)
- **#32 WAL CRC32 race:** Replaced global `s_crc32Table` + `s_crc32TableReady` bool with `constexpr Crc32Table` struct in function-local `static const` — C++11 thread-safe one-time init, eliminates race if WAL is ever called from worker threads
- **#33 Bare catch blocks:** Replaced `catch(...)` with `catch(const std::exception& e)` + `LOG_WARN` at `ui_hot_reload.cpp` (file mod time) and `game_app.cpp` (quest ID parsing ×2) — silent failures now logged

**Drive-by fix:** `gl_device.cpp` — `DeviceImpl::UniformKey` → `Device::Impl::UniformKey` (was not compiling)

**Files changed:** `asset_registry.h/.cpp`, `loaders.cpp`, `texture.h/.cpp`, `app.cpp`, `game_app.cpp`, `trade_manager.h/.cpp`, `write_ahead_log.cpp`, `ui_hot_reload.cpp`, `gl_device.cpp`, `test_asset_registry.cpp`

---

### March 22, 2026 - Phase 23: Deep Audit Fixes — Memory Leaks, Hash Collisions, Economy Exploits (7 fixes)

Targeted fixes from deep audit findings: 3 engine/infrastructure issues (memory leak, hash collision, O(N) lookup) and 4 critical economy exploits (item duplication, free stats, gold underflow, trade escape).

**Engine/Infrastructure (3 fixes):**
- Arena ended-match purge: `tickMatches` now lazy-stamps `endedAt` on ended matches, erases after 60s linger — was unbounded `matches_` growth. `ArenaMatch::ENDED_LINGER = 60.0f`. 1 new test
- Uniform cache collision fix: key changed from truncated `(program << 32) | (nameHash & 0xFFFFFFFF)` to `UniformKey{GLuint program, std::string name}` struct with `operator==` + boost-style hash combiner — eliminates silent wrong-location returns on 32-bit hash collision
- Entity→client O(1) reverse index: `ConnectionManager` gains `entityToClient_` (uint64_t) + `entityToClientLow32_` (uint32_t, arena interop) maps with `findByEntity`/`findByEntityLow32`/`mapEntity`/`unmapEntity`. `removeClient` auto-cleans both indices. 4 forEach scans replaced (combat fury sync, death notify, arena match end, arena match create)

**Economy Exploits (4 fixes):**
- Market buy race (item duplication): `deactivateListing()` now uses `AND is_active = TRUE RETURNING listing_id` — returns false if another buyer already claimed it. Buy handler aborts transaction on false. Was: two concurrent buyers both get item + both pay gold
- Stat enchant free stats: `CmdStatEnchantMsg` gains `scrollItemId` field. `processStatEnchant` now validates scroll exists in inventory and consumes it via `removeItemQuantity`. Was: infinite free stat re-rolls with no scroll consumed
- Zone transition during active trade: zone transition handler cancels active trade session and notifies partner before allowing transition. Was: trade session stayed alive, partner could still confirm, items locked forever
- Bank deposit during active trade (gold underflow): bank handler blocks all operations during active trade. Trade execution now validates `goldA >= offeredGold` before committing (defense-in-depth underflow guard). Was: deposit gold to bank, trade executes with negative gold

**Files changed:** `arena_manager.h`, `gl_device.cpp`, `connection.h/.cpp`, `server_app.cpp`, `market_repository.cpp`, `game_messages.h`, `crafting_handler.cpp`, `bank_handler.cpp`, `test_arena_manager.cpp`

---

### March 22, 2026 - Phase 22: Deep Security & Stability Audit (17 fixes)

Systematic audit of server infrastructure — 17 issues fixed across security, crash safety, threading, and architecture.

**Critical (crash/security):**
- Circuit breaker null guard: `Guard::connection()` throws `runtime_error` when null — was nullptr deref crash in all 13 repos + 3 dispatcher tasks
- TLS hardened: TLS 1.2+ minimum enforced, AEAD-only ciphers, no compression on auth channel (both server and client)
- Plaintext key exchange removed: legacy 64-byte raw key path deleted, DH required for all connections, clients without crypto rejected
- PlayerLockMap: `unique_ptr<mutex>` → `shared_ptr<mutex>` prevents use-after-free when disconnect erases map while async fiber holds lock
- Arena/Pool allocators: `assert` → `LOG_FATAL + abort()` for allocation failures (active in release/shipping builds)
- Auth server DoS: `handleClient()` dispatched on threads (max 8), 10s `SO_RCVTIMEO` per connection, `dbMutex_` protects DB (bcrypt outside lock)

**High (data loss/integrity):**
- DbDispatcher: error callback added to all 3 dispatch methods; async save re-dirties player on failure for retry
- WAL: fwrite/fflush return values checked; `writeError_` flag triggers forced sync DB flush for all players
- Zone transitions: server loads portal positions from scene JSONs at startup, validates player proximity before allowing transition
- Gauntlet tiebreaker: flag now checks both state AND score equality
- Profanity filter added to character name registration
- Character names: 1-10 alphanumeric only (was 2-16 with spaces)
- Trade/Inventory slots aligned: trade 9→8, inventory 15→16 (4×4), UI MAX_SLOTS 20→16

**Architecture/Performance:**
- **server_app.cpp split**: 14 handler files extracted to `server/handlers/` (combat 1,107, dungeon 774, crafting 669, persistence 437, GM 350, gauntlet 258, pet 257, bank 202, ranking 193, sync 183, consumable 172, equipment 138, pvp_event 127, maintenance 62). Core: 7,919→3,104 lines. CMake GLOB_RECURSE auto-discovers
- Tick overrun protection: per-tick timing, slow warnings >50ms, MAX_CATCHUP_TICKS=3 cap, debt reset
- PersistenceQueue: `nextStarvationTime_` fast path — O(1) skip when no items due (was O(N log N) full drain per tick)
- ReliabilityLayer: `pending_` vector → deque for O(1) front eviction

---

### March 22, 2026 - Phase 21: Fate HUD — Game-Specific UI Widgets (4 sub-phases)

17 game-specific widgets built on the retained-mode UI system, covering the full MMORPG UI: HUD, menu panels, flow screens, and social/economy. Percentage-based sizing added to anchor system. Circle/ring/arc rendering primitives added to SpriteBatch. Proprietary code removed from git tracking (server/, Docs/, scripts/). Test count: 877 + 39 UI = 916 total.

**Phase 1 — Core HUD (7 widgets, 1,129 lines):**
- `SpriteBatch::drawCircle/drawRing/drawArc` — N-segment rotated quad approximations for circular UI elements
- `DPad` — cross-shaped directional input with dead zone, cardinal direction detection, ActionMap injection
- `SkillArc` — **signature element**: semicircular 5-slot layout with polar coordinate positioning, central attack button, cooldown sweep overlays via drawArc, polar hit detection (attack vs slot vs miss), wired to SkillManager
- `PlayerInfoBlock` — portrait circle + stacked HP/MP bars + level + gold (K/M formatting)
- `TargetFrame` — enemy name + HP bar, auto-shows on target, auto-hides when no target
- `EXPBar` — full-width bottom bar, gold fill proportional to XP, percentage + raw value text
- `MenuButtonRow` — 5 circular icon buttons (Event/Shop/Map/Inv/Menu) with labels, Inv wired to inventory toggle
- `ChatTicker` — single-line scrolling text ticker, 50-message queue, SDL_GetTicks-driven scroll

**Phase 2 — Menu Panels (4 widgets, 1,321 lines):**
- `LeftSidebar` — vertical quick-nav icon strip, active panel gold highlight, click switches panels
- `InventoryPanel` — parchment bg (warm beige 0.85/0.78/0.65), paper doll with 8 equipment slots arranged anatomically around character placeholder, 4x5 item grid with quantity badges, Gold/Platinum currency, ARMOR value, close button
- `StatusPanel` — parchment bg, character placeholder with class diamond + faction banner, name/level/XP bar, 3x3 stat grid (STR/INT/DEX/CON/WIS/ARM/HIT/CRI/SPD) in inset panel
- `SkillPanel` — parchment bg, 5 numbered set-page tab circles (active=blue), semicircular skill wheel (reuses SkillArc angle math), remaining points orange badge, 4-column skill list grid with orange/grey level dots

**Phase 3 — Flow Screens (2 widgets, 934 lines):**
- `CharacterSelectScreen` — dark atmospheric bg, character display area, horizontal slot bar (up to 7 circles, class-colored ring borders, gold selected ring, "+" for empty), Entry button (cyan), Swap/Delete utility buttons
- `CharacterCreationScreen` — split layout: left art placeholder with class-colored border, right form with diamond class selectors (rotated squares, 3 classes), 4 faction badges (Xyros red, Fenor blue, Zethos green, Solis gold), inline name input with cursor (max 16 chars), class description text, Next/Back buttons, status messages

**Phase 4 — Social/Economy (4 widgets, 1,303 lines):**
- `ChatPanel` — semi-transparent dark panel, 7 channel tabs (All/Map/Global/Trade/Party/Guild/PM) with per-channel colors, message history rendered bottom-up, text input with cursor, party/guild tab gating, close button, onSendMessage callback
- `TradeWindow` — parchment bg, "Trade with [partner]" title, two-sided 3x3 slot grids (my offer interactive, their offer read-only), gold display, Lock button (green border when locked), Accept button (only when both locked), Cancel button
- `PartyFrame` — compact member cards anchored left edge, portrait circle + leader crown diamond, name/level text, thin HP (red) + MP (blue) bars, stacked vertically
- `GuildPanel` — parchment bg, guild name/level/emblem placeholder, scrollable roster with alternate row shading, rank-colored text, online/offline status dots

**Infrastructure:**
- `UIAnchor::sizePercent/offsetPercent` — percentage-based sizing (0-1 range) for viewport-responsive layouts (used by HUD bars, chat panel, EXP bar)
- 7 parchment theme styles added to `default.json` (panel_parchment, text_parchment_title/body/label, button_parchment, slot_parchment)
- 8 JSON screen definitions (fate_hud, fate_menu_panels, fate_social, character_select, character_creation, + 3 migration screens)
- All widgets wired in `game_app.cpp` with per-frame data push from CharacterStats/Inventory/Party/Combat systems
- Proprietary code cleanup: server/ (66 files), Docs/ (130 files), game-specific widgets (14 files), scripts/ (1 file with DB credentials) removed from git tracking via .gitignore + git rm --cached

---

### March 22, 2026 - Phase 21: HUD Polish, UI Editor Overhaul, Movement Fix

TWOM-matching HUD visual overhaul, full UI editor tooling with undo/redo, critical input/movement bugfixes. Test count: 916 (4928 assertions).

**HUD Visual Polish (8 widget files rewritten to match TWOM proportions):**
- PlayerInfoBlock: 230x100, 60px portrait, 150x20 bars with borders + text shadows + dark bg panel
- MenuButtonRow: warm tan buttons (0.75/0.68/0.55), abbreviation text inside, labels below
- DPad: 200px, light grey arms on dark circle, directional arrow indicators, 3px border ring
- SkillArc: 270px, 96px attack button (glow + highlight + "ATK"), 64px slots (empty=faded+"+", filled=LV text)
- ChatTicker: vertical stacked messages (was horizontal scroller)
- EXPBar: 22px, "EXP XX.XXX%" format, text shadow
- Entity HP bars: 32x4px with dark border track (was 28x2)

**UI Editor Overhaul (5 files):**
- Widget-specific inspectors for all 30 widget types (DragFloat/DragInt/ColorEdit4)
- UISerializer round-trips all widget-specific properties
- Ctrl+S saves focused UI screen alongside scene
- Selection outline (cyan rect + white corner handles) in viewport
- Viewport widget drag (click selected widget to reposition, creates undo command)
- UIPropertyCommand undo/redo (full screen JSON snapshots, Ctrl+Z/Y)

**Input Fixes (3 files):**
- UIManager::setInputOffset — maps window mouse coords to FBO-space widget coords
- handlePress screen-layered click-through — non-interactive StretchAll roots pass clicks through
- Editor checks UI widget hit before entity/tile selection

**Movement Fix (3 files):**
- MovementSystem world bounds clamping from scene ZoneComponent
- Move deduplication (only send when position changed >0.1px)
- Server bounds: [-32768, 32768] symmetric (negative coords allowed)

**Editor Grid Fix (2 files):**
- grid.frag fade distance scales with zoom: `max(80, 40/zoom) * gridSize`
- Grid alpha 0.12 → 0.20

---

### March 22, 2026 - Phase 20: Custom Retained-Mode UI System

Complete data-driven UI system built across 4 phases. Three authoring paths: JSON data files, ImGui visual editor, C++ runtime. Test count: 877 + 35 UI = 912 total.

**Phase 1 — Foundation (8 commits, 1,425 lines):**
- `engine/render/nine_slice.h` — NineSlice struct shared by SpriteBatch and UI
- `engine/ui/ui_anchor.h` — AnchorPreset enum (12 presets: TopLeft through StretchAll), UIAnchor struct (offset, size, margin, padding as Vec4)
- `engine/ui/ui_style.h` — UIStyle struct (background/border/text colors, 9-slice, font, opacity, hover/pressed/disabled overrides)
- `engine/ui/ui_theme.h/cpp` — JSON theme loading, style registry with default theme (10 MMORPG styles: panel_dark, button_default, text_gold, bar_hp/mp, slot_default, etc.)
- `engine/ui/ui_node.h/cpp` — Base UINode: tree operations (add/remove/find), recursive anchor layout computation, hit-testing, virtual render dispatch. Size=0 means "inherit parent dimension"
- `engine/ui/widgets/panel.h/cpp` — Panel with background fill, non-overlapping border (no double-blend at corners), optional title text
- `engine/ui/widgets/label.h/cpp` — Label with Left/Center/Right alignment and vertical centering
- `engine/ui/ui_manager.h/cpp` — JSON screen loading, screen lifecycle, layout pass, rendering, reverse-z-order hit-test
- `engine/render/sprite_batch.h/cpp` — Added `drawNineSlice()` (9 quads from one texture, zero batch breaks)
- `engine/app.h/cpp` — UIManager wired into all 4 render paths (GL/Metal x Editor/Shipping) with screen-space projection

**Phase 2 — Interaction (6 commits, 700 lines):**
- `engine/ui/ui_input.h` — DragPayload struct, UIClickCallback/UITextCallback typedefs
- `engine/ui/ui_node.h/cpp` — 10 virtual interaction hooks: onPress, onRelease, onHoverEnter/Exit, onFocusGained/Lost, onKeyInput, onTextInput, acceptsDrop, onDrop
- `engine/ui/ui_manager.cpp` — handleInput() polls Input singleton, updateHover/handlePress/handleRelease/handleKeyInput/handleTextInput, focus/hover/press node tracking
- `engine/ui/widgets/button.h/cpp` — State visuals (hover/pressed/disabled color overrides from style), onClick callback, press cancels on hover exit
- `engine/ui/widgets/text_input.h/cpp` — Cursor positioning, character insertion, backspace/delete, left/right/home/end navigation, maxLength enforcement, Enter-to-submit callback, focused border highlight, placeholder text
- `engine/ui/widgets/scroll_view.h/cpp` — Scroll offset, Up/Down/PgUp/PgDn keyboard nav, auto-computed content height, scrollbar track+thumb rendering, child culling
- `engine/ui/widgets/panel.h/cpp` — Draggable panels: onPress captures offset, UIManager tracks mouse and updates anchor offset

**Phase 3 — Game Widgets (6 commits, 849 lines):**
- `engine/ui/widgets/progress_bar.h/cpp` — fillRatio() clamped 0-1, 4 BarDirection modes (LeftToRight, RightToLeft, BottomToTop, TopToBottom), optional "value/max" text overlay
- `engine/ui/widgets/slot_grid.h/cpp` — Auto-generates columns x rows child Slot nodes with padding, anchored positions
- `engine/ui/widgets/slot.h/cpp` — Item slot with itemId, quantity, icon, slotType, acceptsDragType, createDragPayload(), hover-highlight border, quantity badge
- `engine/ui/widgets/window.h/cpp` — Enhanced panel: title bar (dark), close button (X, top-right), drag-to-move, resize handle indicator, minimizable, onClose callback
- `engine/ui/widgets/tab_container.h/cpp` — Tab bar with active/inactive colors, gold underline on active tab, click-to-switch visibility of content children
- `engine/ui/widgets/tooltip.h/cpp` + UIManager — Tooltip: 0.5s hover delay, positioned at cursor+12/16, screen edge clamping, depth 900 (always on top). Triggered by `properties["tooltip"]` on any node
- `engine/ui/ui_data_binding.h/cpp` — DataProvider callback, `{token}` parsing and resolution. UIManager::update() walks tree resolving Label::text and ProgressBar::value/maxValue from bindings

**Phase 4 — Editor (4 commits, 847 lines):**
- `engine/ui/ui_serializer.h/cpp` — UINode tree → JSON round-trip serialization. Handles all 10 widget types via dynamic_cast. Omits default-valued fields. SlotGrid skips auto-generated children. `saveToFile()` writes pretty-printed JSON
- `engine/editor/ui_editor_panel.h/cpp` — ImGui docked panels: Hierarchy tree (TreeNodeEx per screen/node, selection, right-click add/delete) + Property inspector (anchor combo, offset/size editors, style combo from theme, color pickers, widget-specific sections for all 10 types). View menu toggles
- `engine/editor/editor.h/cpp` — UIEditorPanel member, `setUIManager()` setter, wired into renderUI()
- `engine/ui/ui_hot_reload.h/cpp` — File timestamp polling via `std::filesystem::last_write_time`, checks every 0.5s, triggers `loadScreen()` on changed files

**Assets:** `assets/ui/themes/default.json` (10 styles), `assets/ui/screens/test_panel.json` (test screen)

**Tests:** 35 new tests (118 assertions) across 7 test files: test_ui_node (11), test_ui_theme (2), test_ui_manager (3), test_ui_input (3), test_ui_button (3), test_ui_text_input (5), test_ui_progress_bar (1), test_ui_slot (3), test_ui_data_binding (2), test_ui_serializer (2)

**Files:** ~50 new/modified files, ~3,100 lines added

---

### March 22, 2026 - Phase 19: Metal Rendering Backend

Full Metal rendering backend for all Apple platforms (iOS + macOS) alongside the existing GL backend. Test count: 877 (unchanged — Metal code behind `#ifdef FATEMMO_METAL`, not compiled/tested on Windows).

**Metal Backend Core (5 new files):**
- `metal_types.h` — Resource entry structs with `#ifdef __OBJC__` guards for C++/ObjC++ dual inclusion
- `metal_device.h/.mm` (~460 lines) — Full `gfx::Device` implementation: texture/buffer/shader/pipeline/framebuffer creation via Metal API, RGB8→RGBA8 padding, `MTLStorageModeShared`, format mapping (EAC_RGBA8, ASTC_4x4_LDR, ASTC_8x8_LDR)
- `metal_command_list.mm` — Deferred encoder creation (setFramebuffer stores target, encoder created lazily on first draw), scratch uniform buffer (4KB, `setVertexBytes`/`setFragmentBytes`), `currentEncoder()` + `setDrawable()` helpers
- `metal_shader_lib.h/.mm` — Runtime MSL compilation (`newLibraryWithSource:`) in debug, pre-compiled `.metallib` loading (`newLibraryWithURL:`) in shipping, function name caching

**MSL Shader Ports (9 files in `assets/shaders/metal/`):**
- `sprite.metal` — All 6 render modes (simple, SDF normal/outlined/glow/shadow, palette swap)
- `tile_chunk.metal` — `texture2d_array` sampling for zero-bleed tiles
- `fullscreen_quad.metal` — `vertex_id`-generated fullscreen triangle, Y-flipped UV for Metal top-left origin
- `blit.metal`, `light.metal`, `postprocess.metal`, `bloom_extract.metal`, `blur.metal`, `grid.metal`

**Engine Class Adaptations (17 modified files):**
- `device.h` — `#ifndef FATEMMO_METAL` around `resolveGL*`, `#else` with `resolveMetalTexture/Buffer/PipelineState/DepthStencilState` returning `void*`, `initMetal(void*)`, `resolveMetalCommandQueue()`, `resolveMetalFramebufferPassDesc()`
- `command_list.h` — `#ifdef FATEMMO_METAL` adds `currentEncoder()`, `setDrawable()`
- `shader.h/cpp` — `bind()`/`unbind()`/`setUniform*()` become no-ops under Metal (pipeline state replaces shader binding)
- `texture.h/cpp` — `bind()` no-op, `stbi_set_flip_vertically_on_load(false)` for Metal top-left origin, `GPUCompressedFormats::detect()` hardcodes ETC2+ASTC support on Apple Silicon
- `framebuffer.h/cpp` — `create()`/`isValid()`/`bind()`/`unbind()` Metal paths, `fbo_`/`texture_` ifdef'd out
- `fullscreen_quad.h/cpp` — `draw(void* encoder)` overload for Metal
- `sdf_text.h/cpp` — `atlasTexId_` ifdef'd out
- `sprite_batch.h/cpp` — Direct Metal flush path (memcpy VBO, `setRenderPipelineState`, `drawIndexedPrimitives` with `MTLIndexTypeUInt32`), `setMetalEncoder()`, GL raw texture ID paths guarded
- `chunk_renderer.h/cpp` + `tile_texture_array.h/cpp` — Metal `renderLayer()` path, `MTLTextureType2DArray` creation, `replaceRegion:slice:` per-tile upload

**App Integration:**
- `app.h/cpp` — `SDL_WINDOW_METAL`, `SDL_Metal_CreateView`, `CAMetalLayer` setup (`pixelFormat=BGRA8Unorm`, `maximumDrawableCount=3` for ProMotion), `@autoreleasepool` frame loop, `ImGui_ImplMetal_Init`, Metal blit pass, `SDL_Metal_DestroyView` shutdown
- `editor.h/cpp` — `ImGui_ImplMetal_Init`/`NewFrame`/`Shutdown` on macOS, split init signature

**CMake:**
- `FATEMMO_METAL` defined on all Apple platforms, Metal + QuartzCore frameworks linked
- GL backend excluded on Apple, Metal `.mm` sources added
- 7 files compiled as ObjC++ via `set_source_files_properties(... LANGUAGE OBJCXX)`
- `imgui_impl_metal.mm` replaces `imgui_impl_opengl3.cpp` on Apple
- Shipping metallib build step (xcrun metal → .air → xcrun metallib)

**Files changed:** `device.h`, `command_list.h`, `shader.h/cpp`, `texture.h/cpp`, `framebuffer.h/cpp`, `fullscreen_quad.h/cpp`, `sdf_text.h/cpp`, `sprite_batch.h/cpp`, `chunk_renderer.h/cpp`, `tile_texture_array.h/cpp`, `app.h/cpp`, `editor.h/cpp`, `CMakeLists.txt`, + 15 new Metal files

---

### March 22, 2026 - Phase 18: DH Key Exchange + Compressed Texture Pipeline

Security hardening and mobile rendering infrastructure. Test count: 870 → 877 (+7 tests, 4795 assertions).

**DH Key Exchange (Security Fix):**
- Replaced plaintext AEAD key exchange with X25519 Diffie-Hellman via libsodium `crypto_kx_*`
- Client generates keypair, appends 32-byte public key to Connect payload
- Server generates ephemeral keypair, derives shared keys, sends only its 32-byte public key back
- Both sides derive identical session keys locally — keys never cross the wire
- Ephemeral secret keys wiped immediately via `sodium_memzero`
- Backward compatible: server detects client DH capability, falls back to legacy for old clients
- 5 new tests (keypair generation, key derivation matching, bidirectional encrypt/decrypt, uniqueness, secureWipe)

**Compressed Texture Pipeline (Mobile VRAM):**
- Added `ETC2_RGBA8`, `ASTC_4x4_RGBA`, `ASTC_8x8_RGBA` to `gfx::TextureFormat`
- Added `glCompressedTexImage2D` to GL loader (desktop runtime + iOS static)
- `Device::createCompressedTexture()` uploads pre-compressed blocks with GL error validation
- KTX1 file loader: header parsing, endianness check, format mapping, GPU cap check, 64MB size guard
- `GPUCompressedFormats::detect()` queries GL extensions for ASTC/ETC2 at startup
- On `FATEMMO_MOBILE`, `loadFromFile()` auto-checks for `.ktx` sibling before PNG fallback
- `.ktx` registered in asset loader with magic-byte validation
- TextureCache VRAM estimation now format-aware via `estimateTextureBytes()`
- **Decision:** pixel-art sprites stay uncompressed RGBA (lossy block compression degrades GL_NEAREST)
- 7 new tests (format identification, VRAM estimation, compression ratios, KTX header validation)

**Files changed:** `packet_crypto.h/cpp`, `net_client.h/cpp`, `net_server.cpp`, `connection.h`, `server_app.cpp`, `gfx/types.h`, `gfx/device.h`, `gl_device.cpp`, `gl_loader.h/cpp`, `texture.h/cpp`, `loaders.cpp`, + 2 test files

### March 22, 2026 - Phase 17 Audit: 11 Critical Fixes + Cast Time System

Full system-wide audit (6 parallel agents: security, combat, networking, inventory, party/dungeon/events, engine/editor). 52 findings catalogued (12 critical, 10 high, 22 medium, 8 low). 11 critical + 7 high fixes applied, cast-time system implemented, 3 quick-fix bugs resolved. Test count: 857 → 877 (+20 tests).

**Critical Fixes (C2-C12):**
- **C2 FIXED** trade gold duplication: `addGold(-amount)` silently failed (rejects negative). Changed to `setGold()` with explicit arithmetic for in-memory trade sync
- **C3 FIXED** equipment class/level requirements: `processEquip()` now validates `classReq` and `levelReq` from `ItemDefinitionCache` before allowing equip
- **C4 FIXED** crafted/extracted items missing UUID: added `generateItemInstanceId()` for crafted items and extracted cores
- **C5 FIXED** crafting resource loss on full inventory: pre-checks for empty slot BEFORE consuming gold/ingredients
- **C9 FIXED** NPC scene filtering: added `sceneId` field to `NPCComponent`, wired into replication scene filter and serializer
- **C10 FIXED** SkillManagerComponent not networked: added `Serializable | Networked | Persistent` trait specialization
- **C11 FIXED** hitFrame not serialized in Animator: added to both toJson and fromJson in register_components.h
- **C12 FIXED** createPlayer() missing EquipVisualsComponent: added to entity factory (24 components total)

**Quick Fixes (3 bugs from plan):**
- Cooldown tolerance tightened 0.8x → 0.9x (latency forgiveness was too generous at 20%, now 10%)
- Party cleanup on disconnect: `leaveParty()` called in `onClientDisconnected()` before entity destruction
- Trade gold overflow: `setGold()` now clamps to `[0, MAX_GOLD]`, trade rejects if either player would exceed cap

**Cast Time System (5 tasks):**
- `CastingState` struct added to `CharacterStats` (beginCast/tickCast/interruptCast/isCasting)
- Server `processUseSkill()` checks `CachedSkillDef::castTime` — skills with castTime > 0 enter casting state instead of instant execution
- Server tick loop advances active casts, executes skill on completion (re-entry guard via `castCompleting_` flag)
- Movement interrupts active cast (standard MMO behavior), equipment changes blocked during cast
- Stun/freeze CC interrupts active cast via `interruptCast()` in skill_manager.cpp
- Cast fizzles if target dies during cast window (target alive re-validated at completion)

**Remaining HIGH fixes (7 fixes):**
- H1: First-move position bounds-checked to [0, 32768] range
- H2: Movement destination bounds-checked (rejects out-of-world positions)
- H3: Target alive re-validated at damage application time (not just context build)
- H4: Caster alive re-validated before dealing damage (dead casters can't hit)
- H5: AOE skills stop hitting remaining targets if caster gets CC'd mid-loop
- H9: Dungeon start rejects if any non-leader party member is offline
- H10: Animator `flipXPerAnim` now serialized (survives play-in-editor and scene save/load)

**Files changed:** `server_app.cpp`, `server_app.h`, `game_components.h`, `entity_factory.h`, `register_components.h`, `replication.cpp`, `inventory.h`, `character_stats.h`, `skill_manager.cpp`, + 4 test files

---

### March 22, 2026 - Full System Audit: 12 Critical Fixes

Full system-wide audit across all subsystems (server, combat, inventory, network, DB, editor, game systems). 117 total findings catalogued (12 critical, 22 high, 28 medium, 40 low). All 12 critical-severity issues verified and fixed. High/medium/low fixes handled in parallel session.

**Trade System (C1/C2) — Item Duplication & Slot Locking:**
- **FIXED** item duplication exploit: trade execution now syncs in-memory `Inventory` objects (moves items between players, transfers gold) before `saveInventoryForClient()` writes to DB. Previously, the DB transfer was correct but the in-memory save overwrote it with stale state
- **WIRED** `lockSlotForTrade()` / `unlockAllTradeSlots()` — slots locked on AddItem, unlocked on Cancel/Complete. Market listing already checked `isSlotLocked` but trade never set it

**Server Authority (C4/C5):**
- **C4 FIXED** zone transition bypass: server now rejects transitions to unknown scenes (`!targetScene` → break with LOG_WARN). Previously, invalid scene names skipped the level gate and proceeded
- **C5 FIXED** GM admin_role: `admin_role` loaded from `accounts` table via `AccountRecord` → `PendingSession` → `clientAdminRoles_`. Previously hardcoded to 0 for all players

**Combat (C6) — Dead Caster Skill Exploit:**
- **FIXED** `SkillManager::executeSkill()` and `executeSkillAOE()` now check `casterStats->isAlive()` before execution. Dead players could cast skills via crafted packets

**Item Instance IDs (C7/C8/C9):**
- **ADDED** `generateItemInstanceId()` UUID v4 generator in `game_types.h` (thread-safe, `mt19937_64`)
- **FIXED** all item pickup paths (loot drop, dungeon auto-loot, boss treasure box) — replaced `steady_clock` nanosecond strings with proper UUID v4
- **FIXED** bank withdrawal and core extraction — both assigned UUIDs (previously empty `instanceId` corrupted UUID PK column)

**Editor (C10) — Undo/Redo Dangling Pointer:**
- **FIXED** `clearSelection()` called after every undo/redo operation (menu + Ctrl+Z/Y). Previously, undoing entity creation left `selectedEntity_` pointing to freed memory

**NPCInteractionSystem (C11) — Stale Pointer Crash:**
- **ADDED** `resetCachedPointers()` method and per-frame `world_->isAlive(handle)` validation on `localPlayer` and `interactingNPC`. Auto-recovers on entity destruction during zone transitions

**Spawn System (C12) — Copy-Paste Bug:**
- **FIXED** Y-axis zone containment clamping: `bounds.x` → `bounds.y`. Mobs escaped vertical bounds on non-square zones

**Files changed:** `server_app.cpp`, `skill_manager.cpp`, `game_types.h`, `spawn_system.h`, `npc_interaction_system.h`, `editor.cpp`, `account_repository.h/.cpp`, `auth_protocol.h`, `auth_server.cpp`

### March 22, 2026 - Shipping Client Build + Engine Hardening

Shipping client build pipeline, fiber system Release hardening, play-in-editor snapshot fix, zone transition portal fix, and combat log noise reduction.

**Shipping Client Build (x64-Shipping preset):**
- New CMake preset `x64-Shipping` with `FATE_SHIPPING=ON`
- `editor_shim.h` provides no-op Editor stubs (beginFrame/endFrame/isPlaying/getPrefabManager/etc.) so game code compiles without editor dependency
- ImGui initialized standalone in `app.cpp` for game UI (network panel, game HUD) without editor framework
- Network panel always visible in shipping build (essential for connection diagnostics)
- `glBindFramebuffer(GL_FRAMEBUFFER, 0)` fix ensures rendering targets default framebuffer in non-editor mode

**Fiber System Release Build Hardening:**
- `#pragma optimize("", off)` for entire `job_system.cpp` — prevents MSVC from reordering fiber context switches in Release/RelWithDebInfo
- `__declspec(noinline)` on `fiber::switchTo` in `fiber_win32.cpp` — prevents inlining that corrupts fiber stack frames
- Leaked singleton pattern for `JobSystem` instance — prevents static destruction order crash on shutdown (intentional leak, OS reclaims)

**Play-in-Editor Fix:**
- `PlayerController` changed from `ComponentFlags::None` to `ComponentFlags::Serializable` in `register_components.h`
- Fixes play/stop/play cycle: `isLocalPlayer` field now survives ECS snapshot/restore round-trip

**Zone Transition Fix:**
- `CmdZoneTransition` handler changed `sceneCache_.getByName()` to `sceneCache_.get()` in `server_app.cpp`
- Portals use `scene_id` (integer key), not display name — `getByName()` always failed

**Combat Log Noise:**
- `CombatDbg` log level changed from `LOG_INFO` to `LOG_DEBUG` in `game_app.cpp` to reduce console spam during normal gameplay

### March 22, 2026 - High-Severity Bug Sweep (20 fixes)

Systematic audit of 22 high-severity findings — 20 confirmed and fixed, 1 false positive (H22: skill rank already validated by `executeSkill()`), 1 deferred (H14: replication placeholders for unimplemented features).

**Economy Exploits (4 fixes):**
- H1: Bounty placement now deducts gold from placer (was free — infinite bounty exploit)
- H2: Market cancel listing returns item to seller inventory (was deleted — item loss) — **NOTE: DB deactivation added here but handler item return was incomplete; fully fixed in Phase 25**
- H3: Trade SetGold rejects negative gold values (negative gold passed `getGold() < gold` check)
- H8: GM `/givegold` now sets dirty flag, WAL entry, and enqueuePersist (gold vanished on restart)

**Trade/Equipment Safety (2 fixes):**
- H4: Equipment changes blocked during active trade session (could equip items offered in trade)
- H5: Enchant stone removal uses higher-indexed slot first (prevents index invalidation if inventory compacts)

**Data Integrity (5 fixes):**
- H6: Bank deposit/withdraw preserves full `ItemInstance` metadata — enchant level, rolled stats, sockets, rarity (`StoredItem` extended with `fullItem` field)
- H7: WAL crash recovery now replays GoldChange/XPGain entries to DB in a single transaction (was log-and-truncate only — data loss on crash)
- H9: Dungeon auto-loot (gold and item paths) now calls `enqueuePersist()` (dirty flag was set but flush never queued)
- H10: Character save (`savePlayerToDBAsync`) uses single `pqxx::work` transaction for character record + skills + skill bar + skill points (was 4 separate transactions — partial save on crash)
- H20: Zone transition clears `combatTimer` and auto-attack state (stale combat state carried across zones)

**Network/Security (3 fixes):**
- H11: `DbPool::Guard` gains `operator bool()` + null checks at market buy and trade execute call sites (circuit breaker open → nullptr deref → server crash)
- H12: Rate limiter state preserved per-account across reconnects via `accountRateLimiters_` map (disconnect+reconnect reset gave fresh token buckets)
- H13: `ConnectionManager::addClient()` enforces `maxClients_` cap (default 2000, configurable via `setMaxClients()`)

**Game Logic (4 fixes):**
- H15: ArmorShred stacks now reduce effective armor in physical damage calculation (`skill_manager.cpp` — was applied but never read)
- H16: Collision check lambdas early-exit on first hit (`if (blocked) return` — avoids redundant overlap tests)
- H18: Mob threat-target override uses `acquireRadius` instead of `contactRadius` (mobs oscillated at leash boundary)
- H19: Animator `getFrameIndex()` guards against `frameCount == 0` (returns 0 instead of div-by-zero crash)

**Editor (1 fix):**
- H21: Undo system gains entity handle remap table — `DeleteCommand::undo` registers old→new handle mapping, all transform commands (`Move/Rotate/Scale`) resolve through remap before operating

### March 22, 2026 - Production Readiness Hardening (24 fixes) + Connection Pool Migration

Security, persistence, network, and ECS hardening pass. 28 findings from production readiness review — 24 fixed, 4 deferred. Plus: migrated all 12 game repositories from shared single DB connection to pool-based per-operation acquisition.

**Server Security (5 fixes):**
- First move after connect now bounds-checked (0..32768) — prevents teleport exploit on reconnect (M2)
- Skill cooldown tolerance tightened 80% → 90% — closes faster-than-intended casting (M3)
- Combat results (SvSkillResult) now scene-filtered — only clients in caster's scene receive broadcasts (M4)
- Trade gold reserved on SetGold — deducted immediately, restored on cancel (M5)
- NPC interaction range validation helper added to NPCTemplate (M27)

**Persistence & DB (6 fixes):**
- Dungeon boss gold reward: inventory dirty + enqueuePersist added (M9)
- Phoenix Down consumption: WAL-logged + enqueuePersist(IMMEDIATE) (M10)
- SkillBar save: DELETE-then-INSERT clears removed skills (M11)
- WAL: fflush after every append (M12)
- Trade gold SQL: `AND gold + $2 >= 0` guard (M8)
- Trade history: in-transaction overload prevents lost audit trail (M15)

**Items & Inventory (2 fixes):**
- Bag-in-bag prevention via isBag flag check (M6)
- Bank StoredItem uint16_t overflow guard (M7)

**Network (4 fixes):**
- Ack bitfield expanded 16→32 bits, header 16→18 bytes (M17)
- Pending reliable packet queue capped at 256 (M16)
- payloadSize cross-validated against actual packet length (M18)
- Auth client uses getaddrinfo for DNS resolution (M20)

**ECS (4 fixes):**
- forEach RAII iteration guard + assert on structural change during iteration (M21)
- Arena allocation null check with LOG_ERROR (M22)
- PersistentId memory ordering upgraded to acq_rel/acquire/release (M23)
- Archetype hash collision probing with secondary hash (M24)

**Input & UI (3 fixes):**
- Focus loss resets all key/action/touch states (M25)
- Arena match cleanup: endedAt + purgeEndedMatches(60s retention) (M26)
- Quest setProgress matches by objective index, not targetCount (M28)

**Connection Pool Migration (M13 resolution):**
- All 12 game repositories migrated from `pqxx::connection& conn_` to `DbPool& pool_`
- Each repo method now acquires its own pooled connection via `acquireConn()` (RAII auto-release)
- Dual constructors: pool-based for persistent ServerApp repos, legacy `pqxx::connection&` for temp repos in async fibers
- `DbPool::Guard::wrap()` added for non-owned connection references
- `gameDbConn_` retained for one-time startup operations (definition caches, WAL replay)
- ~101 transaction sites across 24 files (12 headers + 12 sources) migrated
- `AccountRepository` unchanged (belongs to AuthServer, separate connection)

**Deferred (2 remaining):**
- ~~M1: Key exchange sends AEAD keys in plaintext UDP~~ — **FIXED** (X25519 DH key exchange, session 18)
- M14: DbConnection::reconnect blocks up to 6s — needs async pattern
- M19: Connection cookie FNV-1a hash — acceptable short-term with secret key

### March 21, 2026 - Instanced Dungeons (Production)

TWOM-style instanced dungeon system with per-party isolated ECS worlds. 8 implementation tasks, 3 critical bug fixes. Test count: 839 → 844 (+5 tests).

**Architecture:**
- `DungeonInstance` owns a `World` + `ReplicationManager` — fully isolated from overworld
- `getWorldForClient()` / `getReplicationForClient()` routes all 30+ server handlers to the correct world
- `transferPlayerToWorld()` snapshots all player components, destroys in source world, recreates in destination world

**Entry flow:**
- Party leader sends `CmdStartDungeon`, server validates (party 2+, level req, event lock, daily ticket)
- Non-leader members receive `SvDungeonInvite`, respond with `CmdDungeonResponse`
- All accept → save return points, consume tickets, set event locks, transfer players, spawn mobs
- 30s invite timeout, decline cancels for everyone

**Dungeon rules:**
- 10-minute timer, no mob respawn, no XP loss on death
- Death respawns at dungeon spawn point (must run back)
- Honor: +1 per mob kill, +50 per boss kill (to all party members)
- Boss kill triggers 15s celebration window, then exit

**Boss rewards (to all members):**
- Gold: 10,000 × difficulty tier (WAL-logged, server-authoritative)
- Boss treasure box item added to inventory (tier-specific: Goblin Hoard / Crypt Reliquary / Dragon Hoard)
- +50 honor

**Exit/cleanup:**
- Timer expiry: teleport back with no rewards
- Boss kill + 15s celebration: teleport back with rewards
- All disconnect: instance destroyed immediately, no rewards
- Event locks cleared, return positions restored

**GM commands:** `/dungeon start <sceneId>` (solo, bypasses party/ticket), `/dungeon leave`, `/dungeon list`

**Protocol:** 5 new messages (CmdStartDungeon 0x2A, CmdDungeonResponse 0x2B, SvDungeonInvite 0xB4, SvDungeonStart 0xB5, SvDungeonEnd 0xB6)

**DB:** Migration 011 (last_dungeon_entry timestamp, difficulty_tier on scenes, 3 treasure box items). Daily ticket reset at midnight Central Time.

**3 dungeon scenes:** GoblinCave (tier 1, level 3), UndeadCrypt (tier 2, level 8), DragonLair (tier 3, level 15)

**Bug fixes:** PersistentId→EntityHandle truncation in reward/honor distribution, event lock clearing before/after transfer

### March 21, 2026 - Phase 14: Deferred Items

Implemented 7 deferred items from Phase 13 backlog. Test count: 758 → 801 (+43 tests).

**Pet definitions from DB:**
- `PetDefinitionCache::initialize(pqxx::connection&)` loads from `pet_definitions` table at startup
- Replaced 22-line hardcoded wolf/hawk/turtle block with 2-line DB load
- Implementation split: header declares, `.cpp` file has DB code (prevents pqxx leaking into test target)

**Pet auto-loot:**
- `tickPetAutoLoot()` runs every 0.5s in server tick loop
- Scans unclaimed dropped items within `autoLootRadius` (64px/2 tiles) of players with `autoLootEnabled` pets
- Validates loot ownership (owner or FreeForAll party member), scene match, inventory capacity
- Gold uses `setGold()` (server-authoritative), items use WAL-logged `addItem()`
- Sends `SvLootPickupMsg` + `sendInventorySync()` on pickup
- Safe entity destruction (deferred after iteration, unregister from replication first)

**FATE_SHIPPING build flag:**
- `cmake -DFATE_SHIPPING=ON` excludes `engine/editor/*` from sources and defines `FATE_SHIPPING=1`
- Guards in: `app.h`, `app.cpp`, `game_app.cpp`, `combat_action_system.h`, `npc_interaction_system.h`
- Shipping mode: no editor UI, input goes directly to game, renders to default framebuffer
- **x64-Shipping CMake preset** for one-command shipping builds
- **`editor_shim.h`** provides no-op stubs (`Editor::beginFrame`, `endFrame`, `isPlaying`, `getPrefabManager`, etc.) so game code compiles without editor headers
- ImGui initialized standalone in `app.cpp` (init/render/shutdown) for game UI without editor framework
- Network panel always visible in shipping (connection diagnostics)
- `glBindFramebuffer(GL_FRAMEBUFFER, 0)` ensures rendering targets default framebuffer in non-editor mode

**Heartbeat timeout hardening:**
- `lastHeartbeat` initialized to `currentTime` in `addClient()` (was 0.0f → premature timeout)
- Client timeout increased 5s → 8s
- Every 3rd heartbeat sent via reliable channel as UDP packet loss fallback
- `heartbeatCounter_` reset on disconnect

**Elemental resist in skills:**
- `getElementalResist(DamageType)` on CharacterStats returns per-element resist capped at 75%
- `targetElementalResists[8]` array added to `SkillExecutionContext`
- Populated from player target stats (mobs default to 0 — no per-element resist)
- Applied in both single-target and AOE damage paths, multiplicative with base MR
- Fire skill vs 20% MR + 25% Fire resist = `damage * 0.80 * 0.75 = 60%`

**Instanced dungeons (foundation):**
- `DungeonManager` manages `DungeonInstance` objects (per-party isolated `World` + `ServerSpawnManager`)
- Party-to-instance and client-to-instance bidirectional tracking
- `tick(dt)` updates all instance worlds, `getExpiredInstances()` finds empty timed-out instances
- 30-minute timeout, auto-cleanup when empty
- Wired into ServerApp tick loop
- **Follow-up needed:** dungeon scene DB entries, enter/exit transition flow, dungeon-specific replication

**Migration 010 (scene mob sync):**
- SQL ready at `Docs/migrations/010_scene_mob_sync.sql`
- 5 WhisperingWoods mob definitions + 5 spawn zones
- Must be run manually against dev DB

**Audit fixes:**
- Auto-loot gold pickup corrected from `addGold()` to `setGold()` (server-authoritative gold rule)
- Two unguarded Editor references in `combat_action_system.h` and `npc_interaction_system.h` fixed for FATE_SHIPPING

### March 21, 2026 - Batch D: Server Handler Wiring (6 systems)

Wired 6 remaining game systems into the server with protocol messages, command handlers, validation, and DB persistence. Test count: 698 → 758 (+60 tests). Total server command handlers: 26. Total client callbacks: 37.

**Bank system:**
- `processBank()` handler with 4 actions: deposit gold (2% fee), withdraw gold, deposit item (stacks), withdraw item
- **Full item metadata preserved** — deposit captures `ItemInstance` from inventory (enchant level, rolled stats, sockets, rarity), withdraw restores complete item
- Validates player alive, sufficient gold/items, bank capacity (30 slots)
- DB persistence via `bankRepo_->depositGold()`, `withdrawGold()`, `depositItem()`, `withdrawItem()`
- 16 new tests (gold fee calculation, edge cases, item stacking, serialization roundtrips)

**Socket system:**
- `processSocketItem()` handler: validates equipped accessory (Ring/Necklace/Cloak), consumes scroll from inventory, calls `SocketSystem::trySocket()` with weighted 1-10 roll
- Added `Inventory::setEquipment()` for writing modified items back to equipment slots
- Recalculates equipment bonuses after socketing

**Stat enchant system:**
- `processStatEnchant()` handler: validates accessory slot (Belt/Ring/Necklace/Cloak), rolls tier 0-5 (25%/30%/25%/12%/6%/2%), applies via `StatEnchantSystem::applyStatEnchant()`
- Tier 0 = fail (clears existing enchant), tier 1-5 = success with stat value

**Consumable system:**
- `processUseConsumable()` handler: validates item is consumable type, 5-second cooldown, applies HP/MP restore
- Looks up item definition for heal/mana amounts, falls back to itemId pattern matching
- WAL logs item removal, consumes 1 unit

**Ranking system:**
- `processRankingQuery()` handler: 60-second DB cache, paginated 50/page, JSON serialized
- Queries characters table ordered by level/XP, caches in `RankingManager`
- Supports global, per-class, honor, and guild categories

**Bag expansion:**
- Integrated into existing `processEquip()` — detects bag items via item definition cache
- Bags reworked from slot expansion to nested containers — bag item occupies one of 15 fixed inventory slots and contains up to 10 internal sub-items
- `addItemToBag(bagSlot, item)`, `removeItemFromBag(bagSlot, subSlot)`, `getBagItem()`, `setBagCapacity()`
- Cannot remove a bag from inventory until its contents are emptied
- Handles bag-for-bag swaps with net delta calculation
- 15 new tests for expand/shrink edge cases

**Server-side HP/MP regen:**
- Added to server tick loop between timer ticking and death transitions
- HP: 1% maxHP + equipBonusHPRegen every 10 seconds
- MP: WIS * 0.5 + equipBonusMPRegen every 5 seconds (mana classes only)

**Files:** server_app.h/cpp, net_client.h/cpp, game_messages.h, packet.h, rate_limiter.h, inventory.h/cpp, 4 new test files
**New protocol messages:** 10 (5 Cmd + 5 Sv results)

### March 21, 2026 - Batch E: Pet System Wiring

Pet system end-to-end wiring: equip/unequip, stat bonuses, XP sharing. Test count: 693 → 698 (+5 tests).

**Pet equip/unequip:**
- `CmdPetMsg` / `SvPetUpdateMsg` protocol messages for equip/unequip actions
- `PetDefinitionCache` on server loaded from `pet_definitions` DB table at startup (3 starter pets: wolf, hawk, turtle)
- Server handler validates pet ownership, applies stat bonuses via `PetSystem::applyToEquipBonuses()`
- Client receives `SvPetUpdate` with pet instance data, updates PetComponent

**Stat bonuses & XP sharing:**
- Equipped pet bonuses (HP, CritRate, ExpBonus) applied to CharacterStats on equip, removed on unequip
- 50% XP sharing: player XP gain sends proportional XP to pet via `PetSystem::addXP()`
- Pet level capped at player level

**Database:**
- Migration 008: `pet_definitions` table with 3 starter pets
- `PetRepository` load/save on connect/disconnect

**Server fixes:**
- HP/MP regen tick now server-authoritative (was client-only, server HP overrode regen)
- `pk_status` and `faction` now persisted to database across sessions

### March 21, 2026 - Batch D: Protocol Message Expansion

Added protocol messages for 5 remaining systems that lacked client↔server communication.

**New protocol messages:**
- `CmdBankMsg` / `SvBankUpdateMsg` — deposit/withdraw items and gold
- `CmdSocketItemMsg` / `SvSocketResultMsg` — accessory socket rolling
- `CmdStatEnchantMsg` / `SvStatEnchantResultMsg` — accessory stat enchanting
- `CmdUseConsumableMsg` / `SvConsumeResultMsg` — consumable item usage with 16 effect types
- `CmdRankingQueryMsg` / `SvRankingResultMsg` — leaderboard queries (global/class/guild/honor)

### March 21, 2026 - Batch C: PvP Events & Rankings

Added arena, battlefield, event scheduler, and honor ranking systems. Test count: 635 → 693 (+58 tests).

**Event Scheduler:**
- `EventScheduler` FSM with 3 states (Idle→Signup→Active), configurable intervals/durations
- 4 callback hooks (OnSignupStart/OnEventStart/OnEventEnd/OnTick)
- Drives both Battlefield and Arena event cycles

**Battlefield:**
- `BattlefieldManager` with 4-faction PvP (Xyros/Fenor/Zethos/Solis)
- Per-faction kill tracking, personal K/D stats, winning faction determination
- Minimum 2-faction requirement, no death penalties (DeathSource::Battlefield)
- Integrated with EventScheduler for registration and rewards
- Protocol: `CmdBattlefieldMsg` / `SvBattlefieldUpdateMsg`

**Arena (1v1 / 2v2 / 3v3):**
- `ArenaManager` with queue-based matchmaking by mode and level bracket
- `ArenaMode` enum: Solo (1v1), Duo (2v2), Team (3v3)
- 3-second countdown, 3-minute match duration, AFK detection (30s timeout)
- Honor rewards: Win=30, Loss=5, Tie=5
- Party sync for duo/team modes, return-position tracking
- Protocol: `CmdArenaMsg` / `SvArenaUpdateMsg`

**Honor Ranking:**
- Honor badges replicated to all players via delta compression bit 15 (`honorRank` field)
- Integrated with arena honor rewards

**Infrastructure:**
- `DeathSource` enum expanded: PvE=0, PvP=1, Arena=2, Environment=3, Battlefield=4
- New packet IDs for battlefield and arena commands/responses
- Event lock system prevents simultaneous event participation

### March 21, 2026 - Batch B: Enchant +15, Core Extraction, Crafting

Extended enchant system, added item disassembly and crafting. Test count: 635.

**Enchant system extended to +15:**
- `MAX_ENCHANT_LEVEL` raised from 12 to 15
- New success rates: +9=50%, +10=40%, +11=30%, +12=20%, +13=10%, +14=5%, +15=2%
- Gold costs for +13-15: 500K, 1M, 2M
- Secret bonuses at +15 with additional stat scaling
- Break mechanic: failed enchant above safe level can set `isBroken = true` on item
- Migration 007: `is_broken` column + crafting book tiers

**Core extraction:**
- `CoreExtraction::determineCoreResult()` — rarity-based tier determination (1st-7th cores)
- Uncommon items: tier by item level (1st-5th). Rare: 6th. Epic+: 7th
- Bonus quantity from enchant level (+1 per 3 enchant levels)
- Common items cannot be extracted
- Server handler + client callback wired

**Crafting system:**
- `RecipeCache` loads recipes from database with tier-based lookup (Novice/Book I/II/III)
- `CachedRecipe` with ingredients, level/class requirements, gold costs
- Server handler validates ingredients, gold, level/class, produces result item
- Protocol: `CmdCraftMsg` / `SvCraftResultMsg`

**Item pipeline:**
- `isBroken` field added to ItemInstance, DB schema, and sync messages
- Broken items cannot be traded or marketed (`isMarketable()` checks `!isBroken`)
- Client callbacks for enchant and repair results (UI feedback)
- Server enchant and repair handlers with break mechanic

### March 21, 2026 - Audio Integration (SoLoud)

Integrated SoLoud audio library providing SFX, music streaming, spatial audio, and volume control. Test count: 573 → 635 (+62 tests).

**SoLoud integration:**
- Added via FetchContent (RELEASE_20200207), built as static library with SDL2 + null backends
- `AudioManager` class in `engine/audio/audio_manager.h/cpp` wrapping SoLoud engine
- AUTO backend detection with NULLDRIVER fallback (headless/CI environments)
- Max 32 active voices, SoLoud auto-virtualizes overflow by priority/distance

**SFX system:**
- `loadSFX(name, path)` preloads WAV/OGG into memory for low-latency playback
- `loadSFXDirectory(dirPath)` batch-loads all .wav/.ogg files with filename stem as key
- `playSFX(name, volume)` fire-and-forget playback with SFX volume bus multiplier
- `playSFXSpatial(name, worldX, worldY, listenerX, listenerY, maxDistance)` for distance-based volume + stereo pan
- Missing sounds warn once per unique name, then silently skip

**Music system:**
- `playMusic(path, fadeIn)` streams OGG with automatic looping and crossfade
- Crossfade: fades out current track over half the fade duration, fades in new track
- Music voice is protected from priority stealing (`setProtectVoice`)
- `stopMusic(fadeOut)` with graceful fade or immediate stop

**Volume buses:**
- Master volume → SoLoud global volume
- SFX volume → multiplied into each playSFX call
- Music volume → applied to music voice handle
- All clamped 0-1, real-time adjustable

**Game integration (10 hooks in game_app.cpp):**
- `onCombatEvent`: kill SFX (local, on server confirm); spatial hit_melee/miss (remote). Local hit/crit/miss audio moved to animation hit frame via `CombatActionSystem::onPlaySFX`
- `onSkillResult`: hit_skill, hit_crit, miss, kill (via HitFlags bitmask)
- `onDeathNotify`: death SFX
- `onLootPickup`: loot_gold, loot_item
- `onRespawn`: respawn SFX
- `onZoneTransition`: zone music (assets/audio/music/{zoneName}.ogg)
- Chat send: chat_send SFX
- `onUpdate`: audioManager_.update(dt) ticked each frame

**Expected SFX files** (drop into `assets/audio/sfx/`):
hit_melee.wav, hit_crit.wav, hit_skill.wav, miss.wav, kill.wav, death.wav, loot_item.wav, loot_gold.wav, respawn.wav, chat_send.wav

**Expected music files** (drop into `assets/audio/music/`):
{zoneName}.ogg (e.g., WhisperingWoods.ogg)

**Tests:** 27 new in test_audio_manager.cpp (lifecycle, volume clamping, spatial math, safety edge cases)
**Files:** 3 new (audio_manager.h/cpp, test), 2 modified (CMakeLists.txt, game_app.h/cpp)

### March 21, 2026 - Engine Hardening, Mobile Platform, Infrastructure (Phase 9)

Cross-referenced 9 research documents against the full engine codebase. Implemented 16 systems across 4 domains. Test count: 478 → 573 (+95 new tests, 3732 assertions).

**Network Security:**
- **AEAD packet encryption:** XChaCha20-Poly1305 via libsodium (vcpkg). **Upgraded to X25519 DH key exchange in session 18** — client/server exchange only public keys, derive shared session keys via `crypto_kx_*`. Keys never cross the wire. Sequence-as-nonce. All non-system payloads encrypted. Tampered packets silently dropped. Plaintext fallback without libsodium. 16 tests.
- **IPv6 dual-stack sockets:** Refactored `NetAddress` from `uint32_t ip` to `sockaddr_storage`. Socket tries AF_INET6 with IPV6_V6ONLY=0 (dual-stack), falls back to AF_INET. Auto-converts IPv4-mapped IPv6 addresses. `toString()` for logging. iOS App Store mandatory since 2016. 7 tests.
- **One-time economic nonces:** `NonceManager` issues random uint64 per-client, validates single-use, 60s expiry. Wired into server disconnect + maintenance. Infrastructure for trade/market replay prevention. 8 tests.
- **Auto-reconnect wired:** NetClient detects heartbeat timeout (8s), auto-reconnects with stored auth token. Exponential backoff 1s→30s, 60s total timeout. ConnectAccept clears state. Reliable heartbeat fallback every 3rd beat. 4 tests.

**Combat & PvP:**
- **Two-tick death lifecycle:** `LifeState` enum (Alive→Dying→Dead). `takeDamage()` transitions to Dying (on-death procs fire). `advanceDeathTick()` called per server tick transitions Dying→Dead. Dying/Dead entities reject damage. Replication sends 3-state value. 11 tests.
- **Full PvP target validation:** Replaced `canAttackPlayer()` stub with 4-parameter version checking safe zone → party → dead/dying → same-faction innocents → cross-faction. 10 tests.
- **Inventory slot safety verified:** `addItemToSlot()` already had bounds + trade-lock + occupancy checks. 5 regression tests added.
- **Optimistic combat feedback:** Client plays attack animation immediately on input. `CombatPredictionBuffer` ring buffer. Attack flash (warm yellow melee, cool blue spell). Server reconciles with final damage. No server changes needed.

**Mobile Platform:**
- **GLES shader preamble injection:** `loadFromSource()` prepends `#version 330 core` (desktop) or `#version 300 es` + precision qualifiers (mobile). Removed hardcoded `#version` from SpriteBatch fallback shaders.
- **SDL lifecycle events:** Already existed — verified working (App class with virtual callbacks).
- **Device info:** `DeviceInfo` detects RAM (Windows/macOS/Linux/Android), classifies Low/Medium/High tiers with VRAM budgets. Thermal state stubs. 5 tests.
- **iOS build pipeline:** Updated Info.plist.in, LaunchScreen.storyboard. Created `ios/build.sh` (build/device/testflight modes) and ExportOptions.plist.
- **Android Gradle project:** Updated to C++23, NDK r27, AGP 8.7, 16KB page sizes. Created `FateMMOActivity.java`, `android/build.sh`.

**Engine Infrastructure:**
- **PhysicsFS VFS:** Virtual filesystem with mount/overlay/read. FetchContent integration. 9 tests.
- **Telemetry collector:** Records named float metrics, serializes to JSON with session ID. 5 tests.
- **Palette swap registry:** `PaletteRegistry` loads named 16-color palettes from JSON. Added `Color::fromHex(string)`. 5 starter palettes. 5 tests.

**Verified as already done:** Profanity filter server wiring, system() command injection (no system() calls found).

**Files:** 69 changed, +2992/-197 lines, 19 new files created.

### March 21, 2026 - Batch A Combat & Class Mechanics

- Fury per-hit generation wired (0.5 normal, 1.0 crit, 0.2 on damage for Warriors)
- SvBossLootOwnerMsg (0xA7): boss kill notification with top damager
- pkStatus (bit 14) replicated to remote players for PK name colors
- Double-cast end-to-end test verified

### March 21, 2026 - Combat Sync Hardening & Equipment Wiring

- Enriched SvSkillResultMsg (hitFlags bitmask, overkill, targetNewHP, cooldownMs, resourceCost)
- Derived stats in SvPlayerStateMsg (armor, crit, speed, dmg mult, PK status)
- SvLevelUpMsg packet with full stat snapshot
- Equipment system fully wired server-side (CmdEquip, stat recalc, DB load into slots)
- Status effect mask replication fixed (was hardcoded 0)
- DoT ticking wired into server loop (was dead code)
- PK transitions + decay timers on both skills AND auto-attacks
- 6 edge-case bugs fixed (iterator invalidation, lifesteal cap, passive stat ordering, HP clamp ordering)

### March 20, 2026 - Party Loot, Mob Aggro, WAL Integration

**Party-aware loot ownership:**
- `EnemyStats::getTopDamagerPartyAware(partyLookup)` aggregates threat table by party — party members' combined damage counts as one unit. Returns `LootOwnerResult` with `topDamagerId`, `winningGroupId`, `isParty` flag. Deterministic tie-breaking: lower entity/party ID wins on equal damage.
- **FreeForAll mode:** Top damager in winning party owns loot, any party member can pick up.
- **Random mode:** Each individual item/gold drop randomly assigned to a different online party member in the same scene. Per-item rolls, not per-kill. Only the assigned owner can pick up.
- Pickup validation gated on `PartyLootMode` — same-party fallback only applies in FreeForAll, not Random.
- 8 tests covering solo, party-beats-solo, solo-beats-party, ties, competing parties, isParty flag.

**Threat-based mob aggro (matches Unity ServerZoneMobAI):**
- MobAISystem now checks `EnemyStats::getTopThreatTarget()` after spatial grid nearest-player lookup. If the top threat is alive, same scene, and within leash range (`contactRadius`), it overrides the nearest player as the AI target.
- Both single-threaded and parallel fiber AI paths updated. Reading `damageByAttacker` from worker fibers is safe (written only on game thread during `takeDamageFrom`).
- Mobs now hold aggro on the highest damage dealer, not just the closest player.

**WAL wired into ServerApp:**
- 16MB safety cap on WAL file size (force-truncates with warning if exceeded).
- Lifecycle: `open()` in init, `flush()` at end of each tick, `truncate()` after auto-save, `close()` in shutdown.
- 9 mutation points instrumented (market buy/list/cancel, guild create, skill/auto-attack XP, loot pickup gold/item). WAL append called BEFORE each mutation.
- Crash recovery on startup: reads/logs leftover entries, then truncates.

**Test count:** 455 → 479 (24 new tests)

### March 21, 2026 - Scenario Test Bot (TestBot + fate_scenario_tests)

**Automated end-to-end gameplay testing infrastructure:**
- **TestBot class** (`tests/scenarios/test_bot.h/.cpp`): Wraps `AuthClient` (TLS TCP login) + `NetClient` (UDP gameplay) into a synchronous test harness. Typed event queues for all 16 server message types. `waitFor<T>(timeout)` blocks until a matching event arrives. `pollFor(duration)` collects events without failing. Auto-generates timestamps for movement, maps `sendAttack()` to `sendAction(0, id, 0)`.
- **ScenarioFixture** (`tests/scenarios/scenario_fixture.h`): doctest fixture that reads credentials from env vars (`TEST_USERNAME`, `TEST_PASSWORD`), auto-logins via TLS auth, and connects UDP per test case. Disconnects in destructor.
- **Separate build target**: `fate_scenario_tests` executable, independent from `fate_tests` unit suite. Guarded by `if(NOT iOS AND NOT ANDROID)`. `tests/scenarios/` excluded from unit test glob via `list(FILTER ... EXCLUDE REGEX)`.

**10 scenario test cases across 4 files:**
- `test_login_stats.cpp` (3): Auth response validation, SvPlayerState cross-check (HP/MP/XP/gold/level), initial sync verification
- `test_combat.cpp` (2): Attack mob → SvCombatEvent, entity update HP change verification
- `test_zone_transition.cpp` (3): Server zone ack, stat preservation across scenes, connection stability
- `test_movement.cpp` (2): Entity visibility on move, no rubber-band for valid movement

**Test count:** 555 unit tests, 3,685 assertions + 10 scenario tests

**AOI investigation — root cause of entity flickering documented:**
- **Bug 1 (flickering):** Distance-based AOI with 640px activation / 768px deactivation caused entities at the boundary to oscillate visible/invisible when moving near the radius edge. The 128px hysteresis gap was too narrow for actively moving mobs.
- **Bug 2 (combat broken, fixed in 4e7166e):** `TargetValidator::isInAOI()` checked `aoi.current` which is empty between ticks (cleared by `advance()`). All combat validation failed. Fixed by switching to `aoi.previous`.
- **Current state:** Distance-based AOI disabled; scene-wide replication used instead. Spatial hash grid rebuilt every tick but never queried (dead code). This is the main scaling bottleneck — O(N²) replication per zone.
- **Fix plan (deferred):** Wider hysteresis (500px/900px), minimum visibility duration (N ticks before entity can leave), use spatial hash for neighbor queries. Expected 3-5x capacity improvement when implemented.

### March 20, 2026 - Bugfixes & Auth Response Expansion

**Critical bugfixes:**
- **AOI target validation fix:** `TargetValidator::isInAOI()` was casting 64-bit PersistentId to 32-bit EntityHandle (truncation), causing every combat action to fail "not in AOI". Now resolves PersistentId → EntityHandle via ReplicationManager. Also switched to check `aoi.previous` (sorted visibility from last tick) instead of `aoi.current` (empty between ticks, cleared by `advance()`).
- **CmdMove rate limit raised:** Token bucket for movement was burst=25/sustained=20 per second, but client sends at 60fps. Players were disconnected for "rate limit abuse" after ~5 seconds. Raised to burst=65/sustained=60.
- **Gold doubling fix:** `addGold()` was called from auth response, pending SvPlayerState, and zone transitions — each adding on top of the previous. Replaced all with `setGold()` (server-authoritative, no accumulation). Added `Inventory::setGold()` method.
- **Paper-doll EquipLayer alignment:** Replaced generic Cape/Legs/Body/Head/Helmet/WeaponFront layers with actual game equipment slots: Cloak/Shoes/Armor/Body/Gloves/Hat/Weapon (matches `EquipmentSlot` enum in game_types.h).

**Auth response expansion:**
- `AuthResponse` expanded from 6 to 18 fields — now sends full character snapshot: level, currentXP, gold, currentHP, maxHP, currentMP, maxMP, currentFury, honor, pvpKills, pvpDeaths, isDead, faction. Client applies all immediately at player entity creation, eliminating the Lv1 flash and blank stats on login.
- Server `processLogin()` populates all new fields from CharacterRecord loaded from DB.
- Client `onPlayerState` handler now applies gold via `setGold()` (was missing entirely).
- Zone transition player recreation now applies gold from last SvPlayerState.

**Known issue documented:** Server loads equipped items into inventory bag (not equipment slots) — `equipBonusHP/STR/etc.` are zero when `recalculateStats()` runs on login. MaxHP is calculated without equipment. Equipment stat application on server needs wiring.

### March 20, 2026 - Engine Polish & Production Hardening (14 issues, #21-34)

**Rendering & art pipeline:**
- **Palette swap shader (#21):** New RenderType 5 in `sprite.frag` — samples sprite as 4-bit grayscale index, looks up color from a 16-entry `u_palette[]` uniform array. `SpriteBatch::drawPaletteSwapped()` sets palette, draws, clears in one call. One equipment sprite × 5 rarity palettes = 5 visual variants from one asset.
- **Paper-doll equipment compositing (#22):** `CharacterAppearance` struct with 6 `EquipLayer` slots (Cape/Legs/Body/Head/Helmet/WeaponFront). `drawPaperDoll()` renders layers in direction-aware order — north-facing swaps weapon behind body. Each layer references a spritesheet path + palette index. Per-layer 0.001 depth offsets prevent z-fighting.
- **Blob-47 autotiling (#23):** `engine/tilemap/autotile.h` — 8-bit neighbor bitmask with diagonal gating (NE only counts if both N and E are same terrain). `applyDiagonalGating()` reduces 256 raw masks to 47 unique configurations. `autotileLookup()` uses a precomputed 256-entry O(1) table. `computeAutotileMask()` template queries neighbors via user-provided lambda. 4 tests, 262 assertions.

**Networking & replication:**
- **Expanded delta compression (#24):** `SvEntityUpdateMsg` expanded from 4 to 16 fields — added maxHP, moveState, animId, statusEffectMask, deathState, castingSkillId+castingProgress, targetEntityId, level, faction, equipVisuals, pkStatus, honorRank (bits 4-15). Wire format is self-describing via 16-bit bitmask — only dirty fields are sent. Common position-only delta remains ~15 bytes. `buildCurrentState()` and `sendDiffs()` updated. 3 tests, 27 assertions.
- **Tiered update frequency (#25):** Distance-based update throttling in `engine/net/update_frequency.h`. Near (≤10 tiles): every tick (20Hz). Mid (10-25 tiles): every 3rd tick (~7Hz). Far (25-40 tiles): every 5th (4Hz). Edge (40+ tiles): every 10th (2Hz). HP changes bypass throttling and always send immediately. Integrated into `ReplicationManager::sendDiffs()` with per-client distance computation. 3 tests.
- **Protocol version handshake (#29):** Client now prepends `PROTOCOL_VERSION` byte to Connect payload (before auth token). Server validates version before allocating a client slot — rejects mismatched clients with `sendConnectReject()` and descriptive reason string. Prevents stale client builds from corrupting sessions. 2 tests.
- **POSIX socket abstraction (#30):** `engine/net/socket_posix.cpp` implements the full `NetSocket` interface using POSIX APIs — `fcntl(O_NONBLOCK)`, `EAGAIN`/`EWOULDBLOCK` mapping, `socklen_t`. CMakeLists.txt filters `socket_posix.cpp` on Windows and `socket_win32.cpp` on POSIX. Enables iOS/Android/Linux server builds.

**Asset & texture infrastructure:**
- **LRU texture cache with VRAM budget (#27):** `TextureCache` expanded with `CacheEntry` (texture + lastAccessFrame + estimatedBytes), configurable VRAM budget (512MB default), `evictIfOverBudget()` walks entries to find oldest with refcount=1 and evicts to 85% of budget. `touch()` and `advanceFrame()` for access tracking. 4 tests.
- **Async asset loading (#26):** `TextureCache::requestAsyncLoad()` spawns a worker thread to `stbi_load()` off the main thread, pushes decoded pixels into a mutex-guarded `PendingUpload` queue. `processUploads(maxPerFrame=2)` drains the queue on the main thread with `loadFromMemory()` GPU uploads. Inserts a 1×1 magenta placeholder immediately so the cache entry exists during decode.

**Balance & configuration:**
- **PvP balance hot-reloadable config (#28):** All combat tuning values externalized to `assets/data/pvp_balance.json` — pvpDamageMultiplier, skillPvpDamageMultiplier, 3×3 class advantage matrix (warrior/mage/archer), hit rate table, crit config. `CombatConfig::loadFromJsonString()` applies partial JSON gracefully (unset fields keep defaults). `loadFromFile()` convenience wrapper. New `skillPvpDamageMultiplier` field (0.30) separated from auto-attack multiplier (0.05). 5 tests.
- **Structured error handling (#31):** `engine/core/engine_error.h` — `EngineError` struct with `ErrorCategory` enum (Transient/Recoverable/Degraded/Fatal), error code, message. `Result<T>` alias over `std::expected<T, EngineError>`. Factory helpers. `engine/core/circuit_breaker.h` — generic `CircuitBreaker` class (configurable failure threshold + cooldown, Closed→Open→HalfOpen state machine). 6 tests.

**Build & ops:**
- **Precompiled headers (#34):** `target_precompile_headers(fate_engine PRIVATE ...)` for 11 STL headers (vector, string, unordered_map, memory, functional, optional, variant, cstdint, algorithm, array, cmath). Excludes nlohmann/json.hpp, SDL.h, imgui.h to avoid macro conflicts.
- **Database backup script (#33):** `scripts/backup_db.sh` — pg_dump in custom format (-Fc), supports DATABASE_URL or individual PGHOST/PGPORT/PGDATABASE vars, 14-day retention pruning via `find -mtime`, backup verification via `pg_restore --list`. Cron-ready.
- **RmlUi migration plan (#32):** `docs/rmlui_migration_plan.md` — 4-phase plan: (1) integrate RmlUi alongside ImGui, (2) port core game UI (inventory/chat/HUD/dialogue), (3) advanced UI (trade/quest/map), (4) gate ImGui behind `#ifdef FATEMMO_EDITOR` in release builds.

### March 20, 2026 - Server Hardening & Infrastructure (14 tasks, 455+ tests)

**Network security hardening:**
- **Token bucket rate limiting:** Per-client, per-message-type rate limiters (`server/rate_limiter.h`). Configurable burst capacity and sustained rate per packet type (e.g., CmdUseSkill: burst=3, sustained=1/sec; CmdMove: burst=25, sustained=20/sec). Cumulative violation tracking with auto-disconnect after threshold. Wired as first check in `onPacketReceived()` before any payload parsing. 7 tests.
- **Server-side profanity filter:** `ProfanityFilter::filterChatMessage()` now called in CmdChat handler with Censor mode (asterisks). Empty/oversized messages dropped. 50+ word list with leetspeak normalization applied server-side before broadcast. 5 tests.
- **Connection cookies:** `ConnectionCookieGenerator` (`engine/net/connection_cookie.h`) uses FNV-1a keyed HMAC with 10-second time buckets. Foundation for netcode.io-style stateless challenge-response handshake preventing spoofed-IP flooding. 5 tests.
- **Server-side AOI target validation:** `TargetValidator` (`server/target_validator.h`) checks every CmdAction and CmdUseSkill target against the server-maintained AOI visibility set (binary search on sorted vector) and validates distance within action range + latency tolerance. Prevents ghost entity targeting and entity ID enumeration. 2 tests.

**Inventory & economy safety:**
- **addItemToSlot() overwrite fix:** `Inventory::addItemToSlot()` now rejects writes to occupied slots (`slots_[slotIndex].isValid()` check). Previously silently overwrote, enabling item destruction. Migration 006 adds `UNIQUE` index on `(character_id, slot_index)` as database-level safeguard. 1 test.
- **Loot pickup atomicity:** `DroppedItemComponent` gains `tryClaim(entityId)` / `releaseClaim()`. Server pickup handler calls `tryClaim()` before inventory mutation — second caller in same tick gets rejected. Claim released if inventory full so item stays on ground. Prevents TOCTOU item duplication. 3 tests.
- **Per-player mutation locks:** `PlayerLockMap` (`server/player_lock.h`) provides `unique_ptr<mutex>` per character ID. Serializes concurrent inventory/gold mutations between game thread and async fiber DB operations. Two-player trades acquire both locks in consistent address order to prevent deadlocks. Wired into `savePlayerToDBAsync()`, `onClientDisconnected()`, and trade execution. 4 tests.

**Database resilience:**
- **Write-ahead log:** `WriteAheadLog` (`server/wal/write_ahead_log.h/.cpp`) journals critical mutations (gold, items, XP) in a binary format with CRC32 per entry. Batched `fsync()` per tick. On crash recovery, replays entries beyond last DB checkpoint. `truncate()` after successful auto-save. 4 tests.
- **DB circuit breaker:** `DbCircuitBreaker` (`server/db/circuit_breaker.h`) — 3-state machine (Closed→Open→HalfOpen). Opens after 5 consecutive `pqxx::broken_connection` failures, rejects all DB requests for 30s cooldown, then allows one probe. Integrated into `DbPool::acquire()` and `release()`. ServerApp ticks breaker time each frame. WAL captures mutations during outage. 6 tests.

**Cross-platform & mobile:**
- **IPv6 via getaddrinfo:** `NetAddress::resolve()` (`engine/net/socket.h`) uses `getaddrinfo()` with `AF_UNSPEC` for hostname resolution. Both `NetClient::connect()` and `connectWithToken()` now use it instead of manual dotted-quad parsing. iOS DNS64/NAT64 compatible (mandatory for App Store since 2016). 2 tests.
- **Cross-platform fibers:** Vendored `minicoro.h` (v0.2.0, single-header stackful coroutines). `engine/job/fiber_minicoro.cpp` implements the `fiber::` API on non-Windows using minicoro's asymmetric resume/yield model. CMake conditionally selects Win32 or minicoro backend. Supports Windows x64, macOS/iOS ARM64, Android ARM64, Linux x64.
- **Mobile reconnection state machine:** `ReconnectState` (`engine/net/reconnect_state.h`) — exponential backoff (1s, 2s, 4s... cap 30s), 60-second total timeout. Foundation for automatic WiFi→cellular handoff recovery. 5 tests.
- **Android build pipeline:** Complete Gradle project in `android/` — AGP 8.5, NDK r27 (LLVM 18, C++20), arm64-v8a, minSdk 24, GLES 3.0 required. `FateActivity` extends `SDLActivity`, loads `libmain.so` via JNI. JNI `CMakeLists.txt` wraps the engine's root CMake. Shared assets directory. `./gradlew installDebug` one-command deploy.

**CI/CD:**
- **GitHub Actions:** `.github/workflows/ci.yml` — 3-job matrix: MSVC on windows-latest (vcpkg for OpenSSL/libpq), GCC-13 on ubuntu-24.04, Clang-17 on ubuntu-24.04. Linux jobs run headless via Xvfb + Mesa software renderer with `MESA_GL_VERSION_OVERRIDE=3.3`. Build caching via actions/cache.

**Test count:** 400 → 455+ (55+ new tests, 3100+ assertions)

### March 20, 2026 - Server Authority Overhaul, Replication Fixes, Session Management

**Server authority overhaul (13 tasks, 400 tests):**
- **Auto-attacks server-authoritative:** CombatActionSystem stripped of all state changes (335 lines removed). Client predicts damage text, sends CmdAction to server. Server validates (alive, range, cooldown, same-scene), calculates damage, applies to mob/player HP, determines kill, awards XP, rolls loot. Client never modifies CharacterStats or EnemyStats directly.
- **PvP auto-attacks server-authoritative:** New server-side PvP path in processAction() — applies 0.05x PvP damage multiplier, armor reduction, handles PvP death/honor/kills, broadcasts SvCombatEvent, sends SvDeathNotifyMsg to killed player.
- **Server skill cooldown validation:** Per-client skill cooldown tracking (skillCooldowns_ map). Server rejects CmdUseSkill if cast within 80% of cooldown duration.
- **Server auto-attack cooldown:** Per-client tracking (lastAutoAttackTime_ map). Rejects CmdAction spam faster than weapon attack speed.
- **Respawn server-gated:** Client sends CmdRespawn, waits for SvRespawnMsg before clearing death state. Local respawn() removed. Death overlay shows "Respawning..." pending state with 5s retry timeout.
- **isDead guard re-enabled:** CmdRespawn handler rejects if player not dead (prevents double-respawn exploits).
- **Server-side XP with level-up:** addXP() handles level-up with overflow carry, recalculateStats(), recalculateXPRequirement(). Both processAction and processUseSkill kill paths use addXP().
- **sendPlayerState after kill:** processAction now sends updated XP/HP/gold to client immediately after mob kill.
- **Honor in SvPlayerStateMsg:** Added honor, pvpKills, pvpDeaths fields to SvPlayerState — synced to client on every state update.
- **Double damage text suppressed:** onCombatEvent skips floating text when attackerId is the local player (prediction already showed it).

**Full state sync on connect:**
- **SvSkillSyncMsg (new):** Server sends all learned skills, activated ranks, and 20-slot skill bar layout.
- **SvQuestSyncMsg (new):** Server sends active quests with per-objective progress.
- **SvInventorySyncMsg (new):** Server sends full inventory slots + equipped items (with rolled stats, sockets, enchant levels).
- All three sent after sendPlayerState() on connect via ReliableOrdered channel.
- Client applies sync to SkillManagerComponent, QuestComponent, InventoryComponent.
- Pending player state pattern: SvPlayerState that arrives before player entity creation is stored and applied after EntityFactory::createPlayer().

**Replication overhaul:**
- **Scene-based visibility replaces distance-based AOI:** buildVisibility() now includes ALL registered entities regardless of distance. Eliminated invisible mob bugs caused by AOI activation/deactivation hysteresis. At current scale (12 mobs + few players per zone), bandwidth is negligible.
- **AOI cleared on zone transition:** Server clears client.aoi.previous and lastAckedState on CmdZoneTransition and town-respawn. Forces fresh SvEntityEnter for all entities in the new scene.
- **Deferred zone transitions:** onZoneTransition callback stores pendingZoneScene_ instead of loading scene mid-poll(). Scene load happens after poll() completes, preventing use-after-free crashes from accessing destroyed world entities.
- **Ghost interpolation seeded on enter:** ghostInterpolation_.onEntityUpdate() called immediately in onEntityEnter handler — prevents brief snap to (0,0).

**Session management fixes:**
- **Reconnect reliability:** ReliabilityLayer::reset() added — clears sequence numbers on disconnect so reconnect's ConnectAccept isn't treated as duplicate.
- **Auth race fix:** consumePendingSessions() runs before server_.poll() so auth token exists when Connect packet arrives.
- **Connect timeout 5s→15s:** Remote DB on DigitalOcean takes ~10s to load character; client no longer times out.
- **Disconnect on shutdown:** GameApp::onShutdown() calls netClient_.disconnect() with 50ms flush — server saves correct state (alive, position, XP) instead of timing out while mobs kill AFK entity.
- **Disconnect cleans up local player:** Disconnect button and onDisconnected callback destroy the local player entity and reset localPlayerCreated_.
- **Scene-aware player save:** Server saves CharacterStats::currentScene instead of broken SceneManager::instance().currentScene() (which returns nullptr on server). Legacy "Scene2" default migrated to "WhisperingWoods".
- **Auth response includes sceneName:** Client loads the correct scene on connect regardless of what the editor has open. Skips reload if already in the right scene.
- **Editor pause gates combat:** onCombatEvent and onDeathNotify return early when Editor::isPaused(), preventing mobs from killing paused players.

**Mob AI fixes:**
- **roamRadius set from zone data:** Was stuck at 3px default (header value in tile units used in pixel space). Now set to 40% of zone radius.
- **baseReturnSpeed set:** Was stuck at 3px/s default. Now set to moveSpeed * 32.

**Test count:** 356 → 400 (44 new tests, 2955 assertions)

### March 20, 2026 - Security Hardening, Protocol Improvements, Mobile Prep

**Network security hardening:**
- ByteReader: NaN/Inf float rejection, string length cap (default 4096), `readEnum<E>()` with bounds checking, `ok()` alias — prevents malicious packet crashes
- Per-tick skill command cap: max 1 `CmdUseSkill` per client per tick prevents cooldown bypass exploits
- Replaced `system()` calls in editor with `ShellExecuteW` (UTF-8 safe via `MultiByteToWideChar`) — eliminates command injection vector

**Entity replication improvements:**
- Per-entity `uint8_t updateSeq` counter on all `SvEntityUpdate` packets (unreliable channel)
- Server increments seq per-entity per update in `ReplicationManager::sendDiffs()`
- Client-side wrapping comparison rejects stale/out-of-order updates — prevents phantom HP bar flickering
- First update per entity accepted unconditionally (avoids false-reject when server seq > 127 at client join)

**Editor safety:**
- `isValidAssetName()` rejects path separators and `..` in Scene Save As and Prefab Save dialogs
- Delete File and Delete Prefab now show confirmation modal popups instead of deleting immediately
- Console commands (`delete`, `spawn`, `tp`) wrapped in try-catch — invalid input logs warning instead of crashing

**Shader preamble injection (GLES portability):**
- `#version` lines stripped from all 9 shader files (sprite, fullscreen_quad, blit, grid, light, postprocess, bloom_extract, blur)
- `getShaderPreamble()` in `shader.cpp` injects `#version 330 core` on desktop, `#version 300 es` + precision qualifiers on mobile (`FATEMMO_GLES`)
- Applied in both `loadFromFile()` and `reloadFromFile()` — hot-reload preserved

**spdlog logging replacement:**
- Custom `Logger` singleton replaced with spdlog wrapper — same `LOG_INFO(cat, fmt, ...)` macro interface, zero changes to 51 calling files
- Per-subsystem named loggers created on first use (e.g., `spdlog::get("Net")`, `"Shader"`, `"ECS"`)
- Rotating file sink (5MB max, 3 rotated files), stdout color sink, callback sink for editor LogViewer
- Backtrace ring buffer (64 entries) for crash breadcrumbs
- Android logcat sink ready via `FATEMMO_PLATFORM_ANDROID` define

**Mobile lifecycle handling:**
- Platform detection defines: `FATEMMO_PLATFORM_WINDOWS`/`IOS`/`ANDROID`/`MACOS`/`LINUX`, `FATEMMO_MOBILE`, `FATEMMO_GLES`
- SDL event filter registered via `SDL_SetEventFilter()` catches `SDL_APP_WILLENTERBACKGROUND`, `SDL_APP_DIDENTERFOREGROUND`, `SDL_APP_LOWMEMORY` before main loop (iOS 5-second deadline safe)
- `AppLifecycleState` enum (Active/Background/Suspended) with `isBackgrounded()` query
- Virtual callbacks `onEnterBackground()`/`onEnterForeground()`/`onLowMemory()` for game-specific save/reconnect
- Game loop skips update/render when backgrounded (sleeps 100ms for battery saving)
- 4 unit tests covering state machine transitions

**TWOM-style touch controls:**
- `ActionMap` gains `setActionPressed()`/`setActionReleased()`/`setActionHeld()` for programmatic input injection
- Virtual D-pad (bottom-left): 18% viewport height radius, 25% dead zone, cardinal direction snapping
- Attack button (larger, red tint) + 5 skill buttons in arc (bottom-right): individual press/release tracking per finger
- Multi-finger support: move + attack simultaneously via independent finger ID tracking
- Tap-to-target: touches not claimed by D-pad or buttons register as world taps
- Rendered via `ImGui::GetForegroundDrawList()` using `GameViewport` coordinates
- F4 toggle on desktop for testing; auto-enabled on `FATEMMO_MOBILE`
- 8 unit tests covering hit detection, D-pad classification, and action injection

**iOS build pipeline:**
- `ios/Info.plist.in`: landscape-only, fullscreen, arm64 + GLES 3.0 required, no encryption flag
- `ios/LaunchScreen.storyboard`: minimal black launch screen
- `ios/build-ios.sh`: one-command CMake Xcode generator script
- `CMakeLists.txt`: iOS target with GLES framework linking, `FATEMMO_GLES` define, server deps skipped, asset bundling into .app, Automatic code signing
- `gl_loader.h/cpp`: conditional `<OpenGLES/ES3/gl.h>` headers on iOS, `loadGLFunctions()` no-op (static linking)
- `app.cpp`: `SDL_GL_CONTEXT_PROFILE_ES` for GLES, `SDL_WINDOW_ALLOW_HIGHDPI`, `SDL_GL_GetDrawableSize()` for Retina
- Deploy path: push repo → clone on MacBook → `./ios/build-ios.sh` → open Xcode → Cmd+R to iPhone

### March 20, 2026 - Server Mob Spawning, Scene-Aware Combat, Combat Event HP Sync

**Server-side mob spawning (DB-driven):**
- `SpawnZoneCache` loads `spawn_zones` table (Migration 005), `ServerSpawnManager` creates mob entities with all stats from `MobDefCache`
- `MobAISystem` added to server World — mobs roam, chase, attack players
- `SvEntityEnterMsg` extended with `mobDefId` + `isBoss` for client sprite/nameplate
- Client `createGhostMob` enhanced — loads `mob_<mobDefId>.png`, creates EnemyStatsComponent + MobNameplateComponent
- Client `SpawnSystem` disabled when connected (server replicates mobs)
- Mob AI radii converted from tiles→pixels (DB stores tiles, AI uses pixels)
- 6 placeholder sprites generated for WhisperingWoods mobs

**Scene-aware combat:**
- `CharacterStats::currentScene` set on connect and zone transition
- `EnemyStats::sceneId` set from spawn zone config
- MobAISystem filters nearest player by scene match before targeting
- `resolveAttack()` validates scene match as safety net
- Prevents cross-scene attacks (e.g., WhisperingWoods mobs attacking Town players)

**Combat event HP sync:**
- Client `onCombatEvent` now applies damage to local player HP immediately
- Previously only showed floating text — HP stayed unchanged, allowing portal traversal while at 0 HP
- Now calls `die(DeathSource::PvE)` when HP reaches 0, blocking movement instantly

**Attack range visualization:**
- `showAttackRange` toggle on CombatControllerComponent
- Draws cyan circle in viewport showing attack range in pixels
- Combat Controller section added to editor inspector

### March 20, 2026 - PvP Combat, Scene Snapshots, Skill Cooldown Fix

**PvP auto-attack targeting:**
- Ghost player entities now have CharacterStatsComponent, FactionComponent, DamageableComponent, PlayerController(isLocalPlayer=false)
- `processClickTargeting()` finds both mobs and players under click via new `findPlayerAtPoint()` helper
- `tryAttackTarget()` refactored with two paths: mob (unchanged PvE) and player (PvP)
- PvP path: faction check (same faction blocked), 0.05x damage multiplier, target's evasion/block/armor/MR used, PKSystem::processPvPAttack() for White→Purple transition
- Target info accessors (name/HP/level) fall back to CharacterStatsComponent when no EnemyStatsComponent present
- No XP/gold on player kill, fury generation preserved

**Scene snapshot save/restore:**
- `onExit()` serializes non-player entities to ZoneSnapshot via `PrefabLibrary::entityToJson()`
- `onEnter()` restores entities via `PrefabLibrary::jsonToEntity()`
- EntitySnapshot changed from opaque bytes to JSON (reuses existing component registry deserialization)
- Player entities skipped (managed by auth/connect flow)

**Skill cooldown client-side fix:**
- GameplaySystem now ticks `SkillManager::tick()` each frame with accumulated game time
- Cooldowns properly expire client-side (skill bar buttons ungray after cooldown ends)

### March 19, 2026 - Stability Tests + Skill Bar Client Wiring

**95 new tests (261 → 356) covering critical stability gaps:**
- Network robustness: empty buffers, truncated messages, oversized strings, garbage values, double-read past end (14 tests)
- Inventory: full inventory, gold overflow/underflow, stacking, trade locking, out-of-bounds, callbacks (24 tests)
- Enemy stats: threat table multi-attacker, top threat, respawn clears, scaling, damage/death/heal edge cases (30 tests)
- CC precedence: shorter stun no-override, stun overrides root/freeze, invulnerability blocks CC, stun immunity, min duration, tick expiry (14 tests)
- Skill null safety: null casterStats/SEM/CC, both targets null, range 0, self-buff, rank validation (13 tests)

**Bug found and fixed:** `executeSkill(skillId, 0, ctx)` caused vector out-of-bounds (`ri = rank-1 = -1`). Now rejects rank < 1.

**Skill bar client wiring:**
- `onSkillActivated` callback on SkillBarUI, fired on left-click of assigned skill (not on cooldown)
- GameApp wires callback to look up target PersistentId from ghostEntities_ and call `netClient_.sendUseSkill()`
- `getTargetEntityId()` accessor added to CombatActionSystem
- Full end-to-end pipeline: SkillBarUI click → NetClient → Server processUseSkill → executeSkill → broadcast SvSkillResult → client floating text

### March 19, 2026 - Server Wiring: Shop, Inventory, Skills, Protocol

**Shop buy fix:**
- Shop buy button now creates ItemInstance from ShopItem, adds to inventory. Refunds gold if inventory full.

**Inventory load from DB on connect:**
- `inventoryRepo_->loadInventory()` called in `onClientConnected()` after gold loading
- Converts InventorySlotRecord → ItemInstance with rolled stats, sockets, display name + rarity from ItemDefinitionCache

**Skill definitions loaded from DB:**
- `convertCachedToSkillDef()` converts CachedSkillDef + CachedSkillRank rows → SkillDefinition
- All skills for player's class registered on SkillManager during connect
- Enables executeSkill to look up damage, cooldowns, effects, passive bonuses

**CmdUseSkill protocol + server handler:**
- New packet types: CmdUseSkill (0x1C), SvSkillResult (0xA2)
- CmdUseSkillMsg carries skillId (string), rank, targetId (PersistentId)
- SvSkillResultMsg carries damage, isCrit, isKill, wasMiss for floating text
- Server `processUseSkill()` builds SkillExecutionContext from ECS components, calls executeSkill, broadcasts result, handles mob death (loot/gold/gauntlet) and PvP death
- Client `onSkillResult` callback displays floating damage/miss text on target
- NetClient::sendUseSkill() for client → server skill requests

### March 19, 2026 - Skill System Completion (Full Unity Port)

**Skill execution pipeline (`executeSkill` + `executeSkillAOE`):**
- 7-step validation: learned/rank, CC canAct, cooldown (double-cast bypass), resource cost (MP/Fury), target alive, range, target type
- Full damage pipeline: base damage x skill% x status effect mult x level mult x PvP mult (0.30 for skills)
- Execute threshold: instant kill if target HP% below threshold (bosses exempt)
- Defense pipeline: shield absorption → block roll (physical/PvP) → armor/MR reduction → Hunter's Mark → Bewitch
- Post-damage: freeze break, status effect application (bleed/burn/poison/slow/freeze DoTs), CC application (stun/freeze), self-buffs (invulnerability, debuff cleanse, stun immunity, guaranteed crit, transform)
- Lifesteal, fury on hit, armor shred on crit
- Special mechanics: Cataclysm (scales with mana spent), double-cast (free second cast window), teleport/dash (non-damaging)
- AOE: `executeSkillAOE` with per-target hit roll + damage + effects, capped by `maxTargetsPerRank`

**SkillDefinition expansion (~30 new fields):**
- Resource & scaling: resourceType, canCrit, usesHitRate, furyOnHit, scalesWithResource
- Effect flags: appliesBleed/Burn/Poison/Slow/Freeze, stunDurationPerRank, effectDuration/ValuePerRank
- Passive bonuses: passiveDamageReduction/CritBonus/SpeedBonus/HPBonus/StatBonusPerRank
- Special mechanics: isUltimate, executeThresholdPerRank, grantsInvulnerability, removesDebuffs, grantsStunImmunity, grantsCritGuarantee, aoeRadius, maxTargetsPerRank, teleportDistance, dashDistance, transformDamageMult/SpeedBonus

**Skill bar utilities + learn validation:**
- `clearSkillSlot`, `swapSkillSlots`, `autoAssignToSkillBar` (auto-assigns on first learn)
- Learn validation: class requirement, level requirement, sequential rank enforcement
- Skill definition registry on SkillManager

**ClientSkillDefinitionCache:**
- Header-only static cache with ClientSkillDef/ClientSkillRankData structs
- `populate`, `clear`, `getSkill`, `getAllSkills` (sorted by level), `hasSkill`

**Passive skill integration:**
- Passive bonus accumulators on SkillManager (HP, crit, speed, damage reduction, primary stat)
- `activateSkillRank` accumulates for passive skills, pushes to CharacterStats
- `recalculateStats` applies passiveHPBonus, passiveCritBonus, passiveSpeedBonus, passiveStatBonus
- `takeDamage` applies passiveDamageReduction (capped 90%)

**Combat pipeline wiring (auto-attack + movement):**
- CombatActionSystem::tryAttackTarget: CC canAct check, status effect damage multiplier, guaranteed crit from effects, magic/armor resistance, Hunter's Mark, Bewitch, shield absorption, freeze break, lifesteal, armor shred on crit, block roll
- MovementSystem: CC canMove check, status effect speed modifier
- Floating text: heal (green) and block (light blue) spawners added

**Item rarity display:**
- Added displayName and rarity fields to ItemInstance
- InventoryUI uses item.rarity instead of hardcoded Common
- Tooltip shows displayName, rarity colors match Unity prototype exactly (#FFFFFF, #4ADE80, #60A5FA, #A855F7, #FB923C, #EF4444)
- Floating text colors fixed to match C# (crit, resist)

**55 new tests (203 → 258 total), all passing.**

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
- **Pickup System**: `CmdAction(actionType=3)` targets a dropped item's PersistentId. Server validates proximity (48px), loot ownership, atomic claim via `tryClaim()` (prevents TOCTOU duplication), adds item/gold to inventory, sends `SvLootPickupMsg`, destroys entity. Claim released if inventory full.
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
- **Market CancelListing**: Deactivates listing. Item return to inventory added in Phase 25 (was missing — only DB deactivation was wired here).
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

- **Connection Pool** (`server/db/db_pool.h/.cpp`): Thread-safe pqxx connection pool. Min 5, max 50 connections. RAII `Guard` class for automatic release (+ `Guard::wrap()` for non-owned connections). Eager creation of min connections at startup, overflow safety valve, dead connection detection on release, `testConnection()` health check. **All 12 game repos now pool-based** — each operation acquires its own connection via `acquireConn()`, legacy single-connection constructor preserved for temp repos in async fibers.
- **Mob Definition Cache** (`server/db/definition_caches.h/.cpp`): Loads 73 mobs from `mob_definitions` at startup. Full stat loading (HP, damage, armor, crit, speed, scaling per level, aggro/attack/leash ranges, spawn weights, loot table IDs, gold drops, honor rewards).
- **Skill Definition Cache** (`server/db/definition_caches.h/.cpp`): Loads 60 skills + 174 ranks from `skill_definitions` and `skill_ranks`. Lookup by ID, by class, rank data with all passive/active/transform/resurrect fields.
- **Scene Cache** (`server/db/definition_caches.h/.cpp`): Loads 3 scenes from `scenes` table. PvP status queries.
- **Data Migration**: Migrated game data from `fate_mmo` (Unity prod DB) to `fate_engine_dev` via CSV export/import. FK dependency order: loot_tables before loot_drops, item_definitions before loot_drops, skill_definitions before skill_ranks. See `Docs/DATABASE_REFERENCE.md` for full schema reference.
- **Database Reference Doc** (`Docs/DATABASE_REFERENCE.md`): Complete reference for all 65 tables with column names/types, FK relationships, migration history, C++ query patterns, and connection details.

**Data loaded into fate_engine_dev:** 748 items, 73 mobs, 60 skills, 174 skill ranks, 72 loot tables, 835 loot drops, 3 scenes, 3 pet definitions, crafting recipes.

### March 18, 2026 - Bounty, Ranking, Profanity, Consumables, Bags, Input Validation

**Ported 6 remaining missing systems from Unity prototype:**

- **Bounty System** (`bounty_system.h`): Full PvE bounty board ported from `NetworkBountyManager.cs` + `BountyService.cs` + `BountyRepository.cs`. Constants: 50K min, 500M max, 10 board slots, 48hr expiry, 2% tax, 12hr guild-leave cooldown. Validation (self-bounty, guild-mate, board capacity), payout calculation with party split, cancel/refund with tax, expiration tick, human-readable result messages. DB callbacks for persistence layer.
- **Ranking System** (`ranking_system.h`): Leaderboards ported from `NetworkRankingManager.cs` + `RankingRepository.cs`. 6 categories (Global/Warrior/Mage/Archer/Guild/Honor), paginated at 50/page, 60s cache. Data structs: `PlayerRankingEntry`, `GuildRankingEntry`, `HonorRankingEntry` (with K/D ratio), `PlayerRankInfo` (global+class+guild rank).
- **Profanity Filter** (`profanity_filter.h`): Full port from `ProfanityFilter.cs`. 3 modes (Validate/Censor/Remove), leetspeak normalization (8 char mappings), 50+ word list (English + Spanish), 4 blocked phrases, 11 blocked characters. Character name validation (1-16 chars, starts with letter), guild name validation (1-20 chars), chat message filtering (max 200 chars), per-character input validation. Word-boundary logic for short words (<=3 chars).
- **Input Validator** (`input_validator.h`): Port from `InputValidator.cs`. Chat/Name validation modes, per-character rejection, username validation (3-20 chars, alphanumeric + underscore), password validation (8-128 chars). Delegates to ProfanityFilter for name/chat validation.
- **Consumable Definition** (`consumable_definition.h`): Port from `ConsumableDefinition.cs`. 16 effect types: RestoreHealth/Mana/Both, 8 buff types (STR/INT/DEX/VIT/ATK/DEF/Speed/EXP), Teleport, RevealMap, SkillBook, StatReset. Cooldown groups, safe-zone-only/out-of-combat-only/while-moving flags, effects description builder.
- **Bag Definition** (`bag_definition.h`): Port from `BagDefinition.cs`. Nested container bags with 1-10 internal sub-slot range, rarity, validation.

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

- **Auth Server**: TLS-encrypted TCP auth server (port 7778) with OpenSSL. **Multi-threaded** — `handleClient()` dispatched on detached threads (max 8 concurrent, `SO_RCVTIMEO` 10s timeout). DB operations protected by `dbMutex_`; bcrypt runs outside lock. Handles RegisterRequest and LoginRequest. bcrypt password hashing (work factor 12). Character name validation: 1-10 alphanumeric, profanity filtered. Thread-safe result queue consumed by game thread.
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

- **Fiber Job System**: Win32 fibers with 4 worker threads and a 32-fiber pool. Lock-free MPMC queue for job dispatch. Counter-based suspend/resume so fibers can wait on dependencies without blocking. Fiber-local scratch arenas for per-job temporary allocations. **Release build hardening:** `#pragma optimize("", off)` for entire job_system.cpp (prevents MSVC reordering fiber context switches), `__declspec(noinline)` on `fiber::switchTo` (prevents inlining that corrupts fiber stack frames), leaked singleton pattern for JobSystem (prevents static destruction order crash on shutdown).
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
- **Auto-loot wired:** `tickPetAutoLoot()` every 0.5s picks up nearby drops (64px radius), ownership+party aware, WAL-logged

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
