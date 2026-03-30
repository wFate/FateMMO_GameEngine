# FateEngine — Game Systems & Mechanics

> **66 files** in `game/shared/` | **~12,900 lines** of C++ | **1,093 tests** (doctest)
> **16 DB repositories** in `server/db/` | **19 server handler files**

All game systems from the C#/Unity prototype have been ported to C++, plus new systems
built from scratch (Arena, Battlefield, Event Scheduler, Core Extraction, Instanced Dungeons, Crafting, Aurora Rotation).
All major systems are DB-wired with message handlers, load-on-connect, save-on-disconnect,
periodic maintenance, and async auto-save. Combat formulas match the Unity prototype exactly.
Trade uses in-memory inventory sync + slot locking to prevent item duplication.
Item instance IDs use UUID v4. Zone transitions validated against scene cache. `AdminRole` enum (`Player=0`, `GM=1`, `Admin=2`) loaded from DB, used across 16 admin commands with category-grouped `/admin` help.

---

## Core Gameplay

Systems ported directly from the C#/Unity prototype — logic identical.

| System | Files | ~Lines | Notes |
|--------|-------|--------|-------|
| Game Types & Enums | `game_types.h` | 400 | All enums, ClassDefinition struct, rarity/mob colors, constants, UUID v4 item ID generator. `BASE_INVENTORY_SLOTS = 16` (4x4 grid), `MAX_TRADE_SLOTS = 8`. `EffectType` (17 values including `ExpGainUp`), `DeathSource` (8 values including `Aurora`). `ResourceType` enum: `None` (0, auto-attacks/passives), `Fury` (1, warriors/archers), `Mana` (2, mages). `AdminRole` enum: `Player` (0), `GM` (1), `Admin` (2) |
| Character Stats | `character_stats.h/.cpp` | 460 | HP/MP/XP/level, stat calc with VIT multiplier, damage formulas, death/respawn, fury/mana, **CastingState** (beginCast / tickCast / interruptCast), `recallScene` (default "Town", used by Recall Scroll), `shouldPreventXPLoss` callback (Soul Anchor check before `applyPvEDeathXPLoss`) |
| Enemy Stats | `enemy_stats.h/.cpp` | 290 | Mob HP, threat table (damage attribution), scaling, death events, **combat leash** (boss/mini-boss reset to full HP after 15s no damage) |
| Combat System | `combat_system.h/.cpp` | 470 | Hit rate with coverage, spell resist, block, armor reduction, PvP, class advantage |
| Mob AI | `mob_ai.h/.cpp` | 570 | TWOM cardinal-only movement, L-shaped chase, axis locking, wiggle unstuck, roam/idle phases, `shouldBlockDamage` callback (god mode) |
| Status Effects | `status_effects.h/.cpp` | 460 | DoTs (bleed/burn/poison), buffs, shields, invuln, transform, bewitch, stacking, `getExpGainBonus()` for Aurora, `removeEffectBySource()` for source-tagged removal (e.g. `SOURCE_AURORA` preserves non-Aurora AttackUp buffs) |
| Crowd Control | `crowd_control.h/.cpp` | 220 | Stun/freeze/root/taunt with priority hierarchy, immunity checks |
| XP Calculator | `xp_calculator.h` | 50 | Gray-through-red level scaling, 0%–130% XP multipliers |
| Honor System | `honor_system.h/.cpp` | 145 | PvP honor gain/loss tables, 5 kills/hr tracking per player pair |
| Enchantment | `enchant_system.h` | 225 | +1 to +15 with success rates (+1–8 safe, +9–15 risky), protection scrolls, secret bonuses (+11/+12/+15), stone tiers, gold costs |
| Item Instance | `item_instance.h` | 120 | Item data with rolled stats, sockets, enchant, soulbound |
| Item Stat Roller | `item_stat_roller.h/.cpp` | 340 | Weighted stat rolling, exponential decay distribution, JSON serialization |
| Socket System | `socket_system.h` | 170 | Accessory socketing (Ring/Necklace/Cloak), weighted rolls (+1: 25% … +10: 0.5%), stat scroll validation, server-authoritative trySocket with re-socket |
| PK System | `pk_system.h/.cpp` | 120 | Status transitions (White → Purple → Red → Black), attack/kill processing, decay timers, cooldowns |

---

## NPC & Quest Systems

New — TWOM-inspired.

| System | Files | Notes |
|--------|-------|-------|
| Quest Manager | `quest_manager.h/.cpp` | Progress tracking, accept/abandon/turn-in, 5 objective types (Kill/Collect/Deliver/TalkTo/PvP), max 10 active, prerequisite chains, client-side state sync |
| Quest Data | `quest_data.h` | Hardcoded registry with 6 starter quests, 4 tiers (Starter/Novice/Apprentice/Adept) |
| NPC Types | `npc_types.h` | NPCTemplate, ShopItem, TeleportDestination structs (with `requiredItem`/`requiredItemQty` for item-cost teleports) |
| Dialogue Tree | `dialogue_tree.h` | Branching dialogue with enum-based actions (GiveItem/GiveXP/GiveGold/SetFlag/Heal) and conditions (HasFlag/MinLevel/HasItem/HasClass) |
| Bank Storage | `bank_storage.h` | Persistent bank with full `ItemInstance` metadata preserved through deposit/withdraw, gold deposit/withdraw, flat 5,000 gold deposit fee (`BANK_DEPOSIT_FEE`) |
| Serialization | `register_components.h` | Custom toJson/fromJson for all NPC/Quest components. Entities persist across scene save/load and prefab round-trips |

> See `Docs/Guides/QUEST_AND_NPC_GUIDE.md` for the full quest and NPC creation guide.

---

## Game Systems

| System | Files | ~Lines | DB | Notes |
|--------|-------|--------|----|-------|
| Inventory | `inventory.h/.cpp` | 410 | Wired | 16 fixed slots, equipment map, gold (`setGold` clamps to `MAX_GOLD`), trade slot locking, equip validates class/level, `setSlot()` for in-place replacement. `addItem()` auto-stacks consumables/materials by `itemId` (no rolled stats/socket/enchant). `moveItem()` stacks same-itemId stackables or swaps different items. **Drag-to-stack/swap**: `CmdMoveItem` (0x33) → `processMoveItem` handler validates slots, blocks during trade, calls `moveItem()`, resyncs. **Drag-to-destroy**: dragging item outside panel fires `onDestroyItemRequest` → confirmation dialog → `CmdDestroyItem` (0x34) → `processDestroyItem` handler calls `removeItem()`, resyncs. Full tooltip data via sync: displayName, rarity, itemType, levelReq, damageMin/Max, armor, rolled stats. Server sends "Inventory full!" on rejected pickup; drops stay on ground until despawn |
| Skill Manager | `skill_manager.h/.cpp` | 340 | Wired | Skill learning (skillbook + points), cooldowns, 4x5 skill bar, **cast-time system** (server ticks CastingState, CC/movement interrupts, fizzle on dead target). Resource deduction respects `ResourceType::None` (skips all cost checks), default `None` for new definitions. **End-to-end wired:** `SvSkillDefs` (0xBB) sends class skill catalog on login → `ClientSkillDefinitionCache`. `CmdActivateSkillRank` (0x35) spends skill points. `CmdAssignSkillSlot` (0x36) rearranges skill bar. Skillbook consumption in `consumable_handler.cpp` (SkillBook subtype → `learnSkill()`). Level-up grants 1 skill point. `/addskillpoints` GM command for testing |
| Party Manager | `party_manager.h/.cpp` | 410 | Runtime | 3-player parties, +10%/member XP bonus, loot modes, invites |
| Guild Manager | `guild_manager.h/.cpp` | 280 | Wired | TWOM guilds, ranks, 16x16 pixel symbols, XP contribution |
| Friends Manager | `friends_manager.h/.cpp` | 380 | Wired | 50 friends, 100 blocks, profile inspection, online status |
| Chat Manager | `chat_manager.h/.cpp` | 120 | Runtime | 7 channels (Map/Global/Trade/Party/Guild/Private/System), cross-faction garbling, server-side mute check (blocks all channels, timed expiry) |
| Trade Manager | `trade_manager.h/.cpp` | 355 | Wired | Two-step security (Lock → Confirm → Execute), 8 item slots + gold, client-side balance validation, atomic transfer, slot locking prevents market/enchant during trade, auto-cancel on zone transition |
| Market Manager | `market_manager.h/.cpp` | 235 | Wired | Marketplace with jackpot, merchant pass, 2% tax, offline seller credit, expiry maintenance, atomic buy via `AND is_active = TRUE RETURNING` |
| Gauntlet | `gauntlet.h/.cpp` | 850 | Wired | Full event scheduler (2hr cycle, 10min signup), 3 divisions, division matchmaking, team scoring/MVP, wave spawning, reward configs |
| Faction System | `faction.h` | 130 | — | 4 factions (Xyros/Fenor/Zethos/Solis), registry, deterministic chat garbling, faction picker on registration |
| Aurora Rotation | `aurora_rotation.h`, `aurora_handler.cpp` | 180 | Wired | 6-zone PvP gauntlet with hourly faction-rotation buff (+25% ATK/EXP), Aether Stone + 50K gold entry, wall-clock `hour%4` rotation, death ejects to Town, voluntary recall. Aurora buffs tagged with `SOURCE_AURORA` — rotation only strips Aurora-sourced AttackUp/ExpGainUp (preserves potion/party buffs). Aether world boss (Lv55, 150M HP, 36hr respawn) in Borealis with 23-item loot table. Aether Stone drops from 13 overworld elites/bosses (Lv25-60, 1%-10%). Zone scaling: Lv10/20/30/40/50/55 |
| Pet System | `pet_system.h/.cpp` | 120 | Wired | Leveling, rarity-tiered stats, XP sharing (50%), auto-loot (0.5s tick, 64px radius, ownership+party aware), PetDefinitionCache from DB |
| Stat Enchant | `stat_enchant_system.h` | 80 | Wired | Accessory enchanting (Belt/Ring/Necklace/Cloak), 6-tier roll table, HP/MP x10 scaling, scroll consumed on use. 7 scroll items in DB (`item_scroll_stat_*`) |
| Bounty System | `bounty_system.h` | 250 | Wired | PvE bounty board (max 10 active, 50K–500M gold, 48hr expiry), 2% tax, guild-mate protection, 12hr guild-leave cooldown, party split on claim |
| Ranking System | `ranking_system.h` | 170 | Wired | Global/class/guild/honor/mob kills/collection leaderboards, paginated (50/page), 60s cache, K/D ratio, faction filtering |
| Collection System | `collection_system.h`, `server/cache/collection_cache.h`, `server/db/collection_repository.h/.cpp`, `server/handlers/collection_handler.cpp` | ~300 | Wired | Passive achievement tracking across 3 categories (Items/Combat/Progression). DB-driven definitions (`collection_definitions` table, 30 seeded). Server checks conditions at 9 event trigger points (mob kill, level up, enchant, loot, arena/battlefield win, dungeon clear, guild join, skill learn). Permanent stat bonuses (STR/INT/DEX/CON/WIS/MaxHP/MaxMP/Damage/Armor/CritRate/MoveSpeed) stack additively with no cap. `CollectionComponent` on player entity caches completed IDs + bonus totals. `SvCollectionSync` + `SvCollectionDefs` packets. Private to player (only completion count feeds leaderboard). **Costume reward type**: `rewardType == "Costume"` grants costume via `costumeRepo_->grantCostume()` using `conditionTarget` as the costume_def_id |
| Costume System | `server/cache/costume_cache.h`, `server/db/costume_repository.h/.cpp`, `server/handlers/costume_handler.cpp`, `engine/ui/widgets/costume_panel.h/.cpp` | ~500 | Wired | Cosmetic costume/closet system. DB-driven definitions (`costume_definitions`, `player_costumes`, `player_equipped_costumes`). 5 rarity tiers (Common→Legendary), per-slot equip (matches equipment slot types), master show/hide toggle. `CostumeCache` loads definitions + mob costume drops at startup. `CostumeRepository` handles persistence (grant, equip, unequip, toggle). `CostumeComponent` on player entities. Replication via 32-bit field mask bit 16 (costumeVisuals). `SvCostumeDefs` (0xC0) sends full definition catalog on login → client `costumeDefCache_`. `SvCostumeSync` (0xBE) + `SvCostumeUpdate` (0xBF) for state sync. 3 grant paths: mob drops (`mob_costume_drops` table, per-mob drop chance), collection rewards (`rewardType == "Costume"`), shop purchase (`itemType == "Costume"`) |
| Stat Allocation | `character_stats.h/.cpp`, `server/handlers/stat_allocation_handler.cpp` | ~80 | Wired | **DISABLED** — stats are fixed per class, only increased through equipment and collections. Fields retained at 0 for DB/network compat. Handler returns "disabled". UI "+" buttons removed |
| Profanity Filter | `profanity_filter.h` | 320 | — | Leetspeak normalization (8 mappings), 52-word list (EN+ES), 4 blocked phrases, 3 modes (Validate/Censor/Remove) |
| Input Validator | `input_validator.h` | 75 | — | Username (3–20 alphanumeric+underscore), password (8–128) validation, delegates to ProfanityFilter |
| Consumable Def | `consumable_definition.h` | 105 | — | 18 effect types (HP/MP restore, 8 buffs, teleport, skill book, stat reset, town recall), cooldown groups, safe-zone/combat restrictions. **Elixir of Forgetting** (StatReset subtype, `elixir_of_forgetting`, 11K gold): resets all activated skill ranks to 0, refunds spent skill points, preserves learned/unlocked ranks from skillbooks. **Recall Scroll** (TownRecall subtype): teleports to `recallScene` (default "Town", settable via future Innkeeper NPC), blocked during combat/arena/battlefield/dungeon, cancels active trades, purges mob threat. **Fate Coin** (`fate_coin` subtype): consumes 3 coins → grants level×50 XP, validates quantity >= 3. **EXP Boost Scrolls** (`exp_boost` subtype): `minor_exp_scroll` (10%) / `major_exp_scroll` (20%) apply `EffectType::ExpGainUp` for 1 hour; same-tier rejects, different tiers stack. **Beacon of Calling** (`beacon_of_calling` subtype): teleports a party member to user's location, validates party membership / alive / not in instanced content, handles cross-scene zone transitions. Uses extended `CmdUseConsumableMsg` with `targetEntityId`. **Soul Anchor** (`soul_anchor` subtype): NOT manually consumed — auto-consumed on death via `shouldPreventXPLoss` callback in `CharacterStats::die()` to prevent XP loss. **SkillBook consumption wired**: `consumable_handler.cpp` handles subtype `SkillBook` — reads `skill_id`/`rank` from item JSON attributes, validates class + level against `skillDefCache_`, calls `learnSkill()`, consumes item, re-syncs. **Item use trigger**: inventory panel second-tap on same slot fires `onUseItemRequest` → `sendUseConsumable()`. **Consume result feedback**: `SvConsumeResult` wired to `onConsumeResult` in `game_app.cpp` — displays `[Item]` messages in ChatPanel for both success and error |
| Bag Definition | `bag_definition.h` | 30 | — | Nested container bags (1–10 sub-slots), rarity, validation |
| Arena Manager | `arena_manager.h` | 510 | — | 1v1/2v2/3v3, queue matchmaking, AFK detection (30s), 3-min matches, honor rewards (Win=30 / Loss=5 / Tie=5), ended match purge (60s linger) |
| Battlefield Mgr | `battlefield_manager.h` | 130 | — | 4-faction PvP, per-faction kill tracking, personal K/D, winning faction determination, no death penalties. **Reconnect grace period** (180s): `markDisconnected` preserves player state, `restorePlayer` re-adds on reconnect, `tickGracePeriod` expires entries when event ends or timer runs out |
| Event Scheduler | `event_scheduler.h` | 80 | — | FSM for timed events (Idle → Signup → Active), configurable intervals, 4 callback hooks, drives Arena and Battlefield |
| Core Extraction | `core_extraction.h` | 40 | — | Equipment disassembly into crafting cores, rarity-based tier (1st–7th), bonus from enchant level (+1 per 3 levels), Common excluded |
| Crafting | `server/cache/recipe_cache.h` | 65 | Wired | Recipe cache with tier-based lookup (Novice/Book I/II/III), ingredients, level/class requirements, gold costs |
| Instanced Dungeons | `server/dungeon_manager.h/.cpp` | 230 | — | Per-party ECS worlds, 10-min timer, boss rewards, daily tickets, invite system (30s timeout), celebration phase, GM commands. Event lock set AFTER `transferPlayerToWorld` (matches post-transfer entity ID), cleared BEFORE exit transfer. **Reconnect grace period** (180s): `markDisconnected` preserves instanceId + return point, `consumeGraceEntry` restores on reconnect, grace expires if instance completes/expires. **Client fully wired:** NPC dialogue entry via `DungeonNPCComponent`, ConfirmDialog invite popup (Accept/Decline), deferred zone transition, per-minute "[System]" time remaining chat, end-of-dungeon chat message, 10s decline cooldown per party |

---

## Key Formulas

All formulas match the C# prototype exactly.

```
XP TO NEXT LEVEL
  xpRequired = max(100, round(0.35 * level^5.1))

HP WITH VITALITY
  baseHP = round(baseMaxHP + hpPerLevel * (level - 1))
  maxHP  = round(baseHP * (1.0 + bonusVitality * 0.01)) + equipBonusHP

ARMOR MITIGATION (capped 75%)
  reduction% = min(75, armor * 0.5)
  finalDamage = max(1, round(rawDamage * (1 - reduction / 100)))

DAMAGE MULTIPLIER (class primary stat)
  Warrior:  1.0 + bonusSTR * 0.02
  Mage:     1.0 + bonusINT * 0.02
  Archer:   1.0 + bonusDEX * 0.02

HIT RATE (coverage system)
  coverage = hitRate / 2.0  (levels covered)
  Within coverage:  90%, 85%, 80%, 75%, 70%  (-5% per level)
  Beyond coverage:  50%, 30%, 15%, 5%, 0%   (steep dropoff)

CRIT RATE
  critRate = 0.05 + (isArcher ? bonusDEX * 0.005 : 0) + equipCritRate

FURY GENERATION
  normalHit: +0.5 fury    critHit: +1.0 fury
  maxFury = 3 + floor(level / 10)

ENCHANT WEAPON DAMAGE
  multiplier = 1 + (enchantLevel * 0.125)
  +11 secret: *1.05    +12 secret: *1.10    +15 secret: additional (stacks)
  +12 max damage bonus: +30%

ENCHANT GOLD COSTS
  +1–3: 100g    +4–6: 500g    +7–8: 2,000g    +9: 10K    +10: 25K
  +11: 50K      +12: 100K     +13: 500K        +14: 1M    +15: 2M

ENCHANT SUCCESS RATES (MAX_ENCHANT_LEVEL = 15)
  +1 to +8: 100% (safe)
  +9: 50%    +10: 40%    +11: 30%    +12: 20%    +13: 10%    +14: 5%    +15: 2%

SOCKET VALUE PROBABILITIES (weighted roll 1–10)
  +1: 25%    +2: 20%    +3: 17%    +4: 13%    +5: 10%
  +6: 7%     +7: 4%     +8: 2.5%   +9: 1%     +10: 0.5%

PVP DAMAGE (loaded from assets/data/pvp_balance.json)
  pvpAutoAttack = baseDamage * pvpDamageMultiplier       // default 0.05
  pvpSkill      = baseDamage * skillPvpDamageMultiplier   // default 0.30
  classBonus    = classAdvantageMatrix[attacker][defender] // default 1.0
```

---

## Entity Components

### Player

| Component | Notes |
|-----------|-------|
| CharacterStatsComponent | HP/MP/XP/level/stats/fury/death/respawn, stat allocation fields (disabled, always 0), 11 collection bonus fields, `recallScene`, `shouldPreventXPLoss` callback |
| CombatControllerComponent | Target tracking, auto-attack state, attack cooldown |
| DamageableComponent | Marker for entities that can receive damage |
| InventoryComponent | 16 fixed slots, equipment, gold, nested bag contents. DB `UNIQUE(character_id, slot_index)` |
| SkillManagerComponent | Skill learning, cooldowns, 4x5 bar. **Networked** (skill bar changes replicated) |
| StatusEffectComponent | Buffs, debuffs, DoTs, shields |
| CrowdControlComponent | Stun/freeze/root/taunt |
| TargetingComponent | Selected target ID, type, max range, click consumed flag |
| ChatComponent | 7-channel chat |
| GuildComponent | Ranks, symbols, XP |
| PartyComponent | 3-player parties, invites, loot mode |
| FriendsComponent | 50 friends, 100 blocks |
| MarketComponent | Listings, jackpot |
| TradeComponent | Two-step security trading |
| NameplateComponent | Display name, level, PK color, guild info, role subtitle |
| FactionComponent | Faction (Xyros/Fenor/Zethos/Solis), permanent, set at creation |
| PetComponent | Equipped pet instance, auto-loot radius |
| QuestComponent | Quest progress, active/completed tracking |
| BankStorageComponent | Persistent bank item/gold storage |
| CollectionComponent | Completed collection IDs + cached bonus totals. Loaded from DB on connect, updated on completion |
| CostumeComponent | Owned costume set, equipped-by-slot map, show toggle. Loaded from DB on connect via `loadPlayerCostumes()`. Replication via costumeVisuals (bit 16) |

### Mob

| Component | Notes |
|-----------|-------|
| EnemyStatsComponent | Mob HP, threat table, scaling |
| MobAIComponent | TWOM cardinal AI, L-shaped chase |
| MobNameplateComponent | Display name, level, boss/elite flags |

### NPC

| Component | Notes |
|-----------|-------|
| NPCComponent | Identity, greeting, interaction radius, face direction, sceneId. `NPCInteractionSystem` caches via `EntityHandle` (not raw pointers) for zone-transition safety |
| QuestGiverComponent | List of quest IDs this NPC offers |
| QuestMarkerComponent | `?` / `!` marker state and tier |
| ShopComponent | Shop name, item inventory with buy/sell prices |
| BankerComponent | Storage slots (flat 5,000 gold deposit fee) |
| GuildNPCComponent | Guild creation cost and level requirement |
| TeleporterComponent | Destination list with costs, level gates, and optional item costs (`requiredItem`/`requiredItemQty`) |
| StoryNPCComponent | Branching dialogue tree with action/condition system |
| DungeonNPCComponent | Dungeon entrance NPC, holds `dungeonSceneId` for instanced dungeon entry. FATE_REFLECT, editor menu, EntityFactory |
| ArenaNPCComponent | Arena NPC marker component (FATE_COMPONENT_COLD, registered, editor menu, EntityFactory). Triggers ArenaPanel via NpcDialoguePanel |
| BattlefieldNPCComponent | Battlefield NPC marker component (FATE_COMPONENT_COLD, registered, editor menu, EntityFactory). Triggers BattlefieldPanel via NpcDialoguePanel |

### World

| Component | Notes |
|-----------|-------|
| SpawnPointComponent | Player respawn marker (isTownSpawn flag), placed via editor |
| ZoneComponent | Named region with size, level range, PvP flag, zone type (town/zone/dungeon) |
| PortalComponent | Trigger area, target scene/zone/spawn position, fade transition |
| SpawnZoneComponent | Region-based mob spawning config with per-mob rules, tick interval, zone containment |
| DroppedItemComponent | Ground loot — itemId, rarity, owner, despawn timer, atomic `tryClaim()` |
| BossSpawnPointComponent | Fixed-position boss spawning, respawn at different position |

---

## Game UI

### Retained-Mode Fate HUD

Data-driven JSON screens rendered via SpriteBatch. **52 widget types** across `engine/ui/widgets/`.
Unity-style ImGui editor: hierarchy with colored type badges + visibility toggles, full property inspector for all 48 widget types, undo/redo, Ctrl+S save, viewport-scaled selection outline, viewport drag, percentage-based sizing. All widget properties serialized to JSON and editable live in the inspector.

**HUD Elements** (TWOM-style gold metallic aesthetic)

| Widget | Position | Notes |
|--------|----------|-------|
| FateStatusBar | Top (full-width, 60px) | All layout properties editable in inspector: `topBarHeight`, `portraitRadius`, `barHeight`, `menuBtnSize`/`chatBtnSize` (radius), `coordFontSize`/`coordOffsetY`/`coordColor`, 5 font sizes, `hpBarColor`/`mpBarColor`, show/hide toggles for Menu button, Chat button, and coordinates. HP/MP bars stretch to fill width. Gold metallic Menu button (7-item parchment popup). Gold metallic Chat button (toggles ChatPanel). Replaces PlayerInfoBlock + MenuButtonRow |
| BuffBar | Top-left (below status bar) | Status effect icons with color-coded backgrounds, radial duration sweep, stack badges |
| TargetFrame | Top-right (below status bar) | Enemy name + HP bar (180x36), auto-shows on target selection |
| DPad | Bottom-left | 150px cross-shaped directional input on gold metallic circle (0.85 opacity), tan embossed arms with highlight ridges, gold active direction |
| SkillArc | Bottom-right | C-shaped arc (290°→190°, radius 180) with gold metallic Action (80px) + Pick Up (60px) buttons, individually positionable via Vec2 offsets. 5 skill slots/page × 4 pages. **SlotArc** page selector (1/2/3/4) on its own C-arc. All positions editable in UI Inspector during play. 60px skill slots with cooldown sweep overlays |
| ChatPanel (idle mode) | Bottom-left | Floating messages with drop shadows over game world (no background), configurable visible lines via `chatIdleLines`, font size via `messageFontSize`. Full panel mode (channel selector + input bar, TWOM-style) toggled via Chat button. Replaces ChatTicker |
| EXPBar | Bottom | Full-width 14px bar, gold fill, "EXP XX.XXX%" with 9pt text shadow |
| PartyFrame | Left edge | Compact member cards (portrait, leader crown, name/level, HP+MP bars), stacked vertically |

**Menu Panels**

| Widget | Notes |
|--------|-------|
| MenuTabBar | 11-tab panel switcher (STS/INV/SKL/GLD/SOC/SET/SHP/PET/CRAFT/COL/COS) with left/right arrow cycling. Gold active tab, dark tan inactive. Direct tap or arrow navigation with wrapping. Manages panel visibility in fate_menu_panels |
| InventoryPanel | TWOM-style layout: paper doll left 45% (character sprite + armor/hat layers, **10 equip slots** — Hat/Armor/Weapon/Shield/Gloves/Boots/Ring/Neck/Belt/Cloak), 4x4 item grid right 55% with rarity-colored borders. Formatted gold/platinum with configurable positioning (platOffsetX/Y). 5 configurable font sizes + 7 colors. Inspector shows equipped item name/rarity/stats per slot. **Drag cursor**: dragged item follows cursor as floating slot (rarity border + letter + quantity badge). **Drag-to-equip** (grid→equip slot, `CmdEquip` action=0) + **drag-to-unequip** (equip→grid, action=1) + drag-to-stack/swap (`CmdMoveItem`) + drag stat enchant scrolls + **drag-to-destroy** (drop outside panel → confirmation dialog → `CmdDestroyItem`). `NetClient::sendEquip()` wired. Tooltip on tap (rarity color, enchant prefix, stat lines, level req). **Context menu** on item tap: Equip/Enchant/Repair/Extract Core/Destroy for equipment; Use/Destroy for consumables. **Drag-drop enchant** scroll onto equipment → `sendEnchant()`. **Drag-drop extraction** scroll → `sendExtractCore()`. **Drag-drop socket** scroll onto equipped accessory → `sendSocketItem()`. Callbacks: `onEnchantRequest`, `onRepairRequest`, `onExtractCoreRequest`, `onSocketRequest` |
| PetPanel | Menu tab panel for pet management. Pet list display, equip/unequip buttons, stat overview. Editable: titleFontSize, nameFontSize, statFontSize, portraitSize, buttonHeight. Full inspector + serialization |
| CraftingPanel | Menu tab panel for crafting. Recipe list, ingredient grid, craft button. Editable: titleFontSize, recipeFontSize, slotSize, resultSlotSize, ingredientColumns. Full inspector + serialization |
| CollectionPanel | Menu tab panel for collection/achievement tracking. 3 category tabs (Items/Combat/Progression), scrollable entry list with completion icons and reward badges. Editable: titleFontSize, entryFontSize, rewardFontSize, categoryTabHeight, entryHeight, completedColor, incompleteColor, rewardColor, progressColor (9 props, 4 full color editors). Full inspector + serialization |
| CostumePanel | Menu tab costume/closet panel. 4-column grid with rarity-colored borders, slot filter tabs (All + per-slot), equip/unequip buttons, show/hide toggle. Editable: titleFontSize, bodyFontSize, infoFontSize, gridCols, slotSize, slotSpacing, buttonHeight, buttonSpacing, filterTabHeight (9 props). Full inspector + serialization. Entries enriched from `costumeDefCache_` with displayName, slotType, visualIndex, rarity |
| StatusPanel | Character placeholder + class diamond + faction banner, name/level/XP bar, 3x3 stat grid (STR/INT/DEX/CON/WIS/ARM/HIT/CRI/SPD), free stat points display with "+" allocation buttons |
| SkillPanel | 5 set-page tabs, semicircular skill wheel with assigned skill names, remaining points badge, 4-column skill list with level dots. **Drag-and-drop**: drag activated skills from list onto wheel slots to assign; click occupied slots to unequip. `onAssignSkill` callback sends CmdAssignSkillSlot (assign/clear). Skill bar data (`skillBarSlots`/`skillBarNames`) synced from SkillManager per frame |
| ChatPanel | TWOM-style dual-mode: idle overlay (floating messages with drop shadows, configurable line count) + full panel (transparent message area over game world, channel selector button cycles All/Map/Glb/Trd/Pty/Gld/PM, dark input bar strip at bottom, X close button top-right). Panel expands to `fullPanelWidth`×`fullPanelHeight` on open, restores on close. 10 inspector-editable properties (all serialized). Style backgroundColor controls optional semi-transparent background. Toggled via FateStatusBar Chat button |
| TradeWindow | Two-sided 3x3 slot grids, gold display, Lock/Accept/Cancel, lock state visual feedback (green borders) |
| GuildPanel | Guild name/level/emblem placeholder, scrollable roster with rank colors and online/offline dots |

**NPC Interaction Panels** (server-authoritative, standalone popup windows)

| Widget | Notes |
|--------|-------|
| NpcDialoguePanel | Hub for NPC interaction. Greeting + conditional role buttons (Shop/Bank/Teleport/Guild/Arena/Battlefield) based on NPC components. `hasArena` + `hasBattlefield` flags, `onOpenArena` + `onOpenBattlefield` callbacks. Arena and Battlefield buttons render when NPC has the corresponding component. Quest accept/complete section. Story NPC mode with branching dialogue tree. Opens sub-panels via callbacks |
| ArenaPanel | NPC-triggered arena registration panel. Solo 1v1 / Duo 2v2 / Team 3v3 mode selection, register/unregister buttons. Editable: titleFontSize, bodyFontSize, buttonHeight, buttonSpacing. Full inspector + serialization |
| BattlefieldPanel | NPC-triggered battlefield registration panel. Register/unregister buttons. Editable: titleFontSize, bodyFontSize, buttonHeight. Full inspector + serialization |
| PlayerContextMenu | Floating popup on player tap in safe zones. Trade (faction-gated), Party Invite, Whisper options. Editable: menuFontSize, itemHeight, menuWidth. Full inspector + serialization |
| LeaderboardPanel | NPC-triggered leaderboard display panel. Full inspector + serialization |
| ShopPanel | Dual-pane (700×480): NPC shop items on left with Buy buttons, player 4×4 inventory on right. Double-click non-soulbound item to sell with quantity confirmation popup. Server-authoritative buy/sell |
| BankPanel | Dual-pane (700×480): bank stored items on left with Withdraw buttons, player 4×4 inventory on right. Click inventory item to deposit full stack. Bottom bar: gold display, deposit amount input (+1K/+10K), "Deposit (fee: 5,000)" button, "Withdraw All" button |
| TeleporterPanel | Destination list (360×350) with level/cost gating. Eligible destinations clickable, ineligible greyed with red requirement text. Server-authoritative teleport |

**Full Screens**

| Widget | Notes |
|--------|-------|
| CharacterSelectScreen | Dark atmospheric bg, paper doll sprite preview via `PaperDollCatalog` (body+hair+equipment layers resolved by style name strings), character info (name/class/level at absolute Y positions), horizontal slot bar (7 circles, class-colored/gold selected ring, "+" empty), Entry/Swap/Delete buttons, delete confirmation dialog with typed-name validation. **70+ editor properties** across 6 inspector groups. Server sends `armorStyle`/`weaponStyle`/`hatStyle` strings in `CharacterPreview` (replaced packed visual indices) |
| CharacterCreationScreen | Split layout, diamond class selectors (3 classes), faction badges (4 colors), inline name input, Next/Back |

### ImGui Legacy HUD (Fully Retired)

**ImGui is completely removed from the game client.** Zero ImGui classes remain in `game/ui/`. All data-routing call sites (~45) migrated to retained-mode widget pointers. ImGui stripped from shipping builds (`#ifndef FATE_SHIPPING`); editor build unchanged.

**Deleted classes:** LoginScreen, TouchControls, ChatUI, DeathOverlayUI, SkillBarUI, InventoryUI, HudBarsUI (~4,000 LOC total across both phases)

---

## Remaining Work

### Dungeons
- [x] Client dungeon UI (invite popup via ConfirmDialog, timer via System chat, scene loading, return handling)
- [ ] Dungeon-specific mob definitions (custom bosses per dungeon — currently using overworld mobs as placeholders)
- [ ] Dungeon scene files (editor-built .json scenes — currently no visual environment)
- [ ] Leader feedback chat message ("Waiting for party members to accept...")
- [ ] Client-side invite timeout (auto-dismiss ConfirmDialog after 30s to match server timeout)

### UI
- [x] NPC panels migrated to retained-mode (NpcDialogue, Shop, Bank, Teleporter) with server-authoritative network messages
- [x] Scissor clipping for ScrollView (pushScissorRect/popScissorRect, nested stack with rect intersection)
- [x] ImageBox widget (texture display with Stretch/Fit modes, tint, UV sourceRect)
- [x] BuffBar widget (status effect icons with color-coded backgrounds, radial duration sweep, stack badges)
- [x] BossHPBar widget (full-width boss engagement bar with name + HP fill + percentage)
- [x] ConfirmDialog widget (modal yes/no popup with Confirm/Cancel buttons)
- [x] NotificationToast widget (stacking banners with fade animation, 4 types)
- [x] FloatingTextManager (world-space combat text: damage/crit/heal/miss/block/XP/gold/level-up)
- [x] Radial cooldown overlay (smooth 32-segment sweep, leading edge highlight, seconds text)
- [x] LoginScreen migrated to retained-mode (login/register/server selector/remember-me), ImGui stripped from shipping builds
- [x] All 5 ImGui data-routing classes migrated and deleted (DeathOverlay widget, SkillArc pages, ChatPanel targetName, InventoryPanel/HudBarsUI rewired)
- [x] Nameplates + floating damage text render via SDFText::drawWorld() into Scene FBO (no ImGui game rendering at all)
- [x] Editor/game input separation: paused=editor owns keyboard, playing=game owns keyboard, no cross-contamination
- [x] Inventory panel reads style for background/border/borderWidth (UI Inspector colors take effect)
- [x] Skill VFX pipeline (TWOM-style sprite sheet + particle effects, JSON definitions, 4-phase sequencing, render graph integration)
- [x] ArenaPanel — NPC-triggered arena registration (Solo 1v1 / Duo 2v2 / Team 3v3), register/unregister
- [x] BattlefieldPanel — NPC-triggered battlefield registration, register/unregister
- [x] PetPanel — Menu tab pet management (equip/unequip, pet list, stats)
- [x] CraftingPanel — Menu tab crafting (recipe list, ingredient grid, craft button)
- [x] PlayerContextMenu — Floating popup on player tap (Trade/Party Invite/Whisper, faction+zone gated)
- [x] LeaderboardPanel — NPC-triggered leaderboard display
- [x] Inventory context menu (Equip/Enchant/Repair/Extract Core/Destroy for equipment; Use/Destroy for consumables)
- [x] Inventory drag-drop: enchant scroll→equipment, extraction scroll→equipment, socket scroll→accessory
- [x] NpcDialoguePanel Arena/Battlefield buttons (hasArena/hasBattlefield flags + onOpenArena/onOpenBattlefield callbacks)
- [x] 8 result handler wirings (onEnchantResult, onRepairResult, onExtractResult, onCraftResult, onSocketResult, onPetUpdate, onArenaUpdate, onBattlefieldUpdate)
- [x] Trade initiation via PlayerContextMenu (same-faction + safe-zone check, TradeAction::Initiate)
- [x] Town scene NPCs placed (9 NPCs: Merchant, Banker, Teleporter, Arena Master, Battlefield Herald, Dungeon Guide, Marketplace Veylan, Quest Elder, Leaderboard Keeper)
- [x] CollectionPanel — Menu tab collection/achievement tracking (Items/Combat/Progression), 9 editable properties + 4 colors
- [x] Collection system server-side (DB cache, repository, 9 event hooks, completion checks, bonus stat integration)
- [x] Stat point distribution (5 free points/level, CmdAllocateStat, StatusPanel "+" buttons, DB persistence)
- [x] Oblivion Potion consumable (StatReset — resets skill ranks, refunds points)
- [x] Recall Scroll consumable (TownRecall — teleport to Town spawn)
- [ ] Art asset pass (parchment 9-slice textures, skill icons, portraits, item icons, ornate headers)
- [ ] Create skill VFX sprite sheets (60 skills need effect art — base templates: slash, fireball, heal, arrow, ice, poison, lightning, shield, buff aura, explosion)
- [ ] 9-slice atlas sub-region support (`drawNineSlice` works but assumes full-texture UV — needs `sourceRect` param for atlas packing)
- [ ] Minimap widget
- [ ] UI animation system (fade, slide, scale transitions)
- [ ] Sound effects for UI interactions
