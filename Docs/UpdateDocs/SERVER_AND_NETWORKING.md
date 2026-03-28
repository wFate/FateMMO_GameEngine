# FateEngine — Server, Networking & Database

## Overview

FateEngine runs a **headless, server-authoritative game server** (`FateServer`) ticking at **20 Hz**. The networking stack is a custom reliable UDP transport with 3 channel types, 32-bit ACK bitfields, and RTT-based retransmission. All game traffic is encrypted via **X25519 key exchange + XChaCha20-Poly1305 AEAD**. Entity replication is AOI-driven with field-level delta compression and tiered update rates. The persistent backend is **PostgreSQL via libpqxx**, with read-only caches loaded at startup and 17 repositories for runtime read/write operations.

---

## Networking

### Transport Layer

| Property | Value |
|----------|-------|
| Protocol | Custom reliable UDP (`0xFA7E`) |
| Platform sockets | Win32 (Winsock2) / POSIX (dual-platform) |
| IPv6 | Dual-stack with IPv4 fallback |
| Max packet size | 1200 bytes standard, 4096 for large payloads (inventory sync with tooltip data) |
| Tick rate | 20 Hz server, 60 Hz client sends |

### Channels

| Channel | ID | Use |
|---------|----|-----|
| `Unreliable` | 0 | Frequent updates (movement) |
| `ReliableOrdered` | 1 | Critical messages requiring guaranteed, ordered delivery |
| `ReliableUnordered` | 2 | Reliable delivery without ordering constraint |

### Reliability

- **Sequence numbers** — monotonically increasing `uint16_t` with safe wrap
- **ACK bitfield** — 32-bit field tracking the 32 most recent prior packets
- **Duplicate rejection** — remote sequence + received bitfield; first packet always accepted
- **RTT estimation** — exponential moving average: `rtt = rtt * 0.875 + sample * 0.125`
- **Retransmission** — delay = `max(0.2s, 2.0 * RTT)`, pending queue capped at 256

### Encryption & Key Exchange

| Layer | Algorithm |
|-------|-----------|
| Key exchange | X25519 (libsodium `crypto_kx`) |
| AEAD cipher | XChaCha20-Poly1305 (24-byte nonce, 16-byte tag) |
| Nonce derivation | 8-byte packet sequence, zero-padded to 24 bytes |
| Session keys | Separate `txKey` / `rxKey` per direction |

System packets (Connect, Disconnect, Heartbeat, KeyExchange, ConnectAccept, ConnectReject) are **not** encrypted. Plaintext fallback available when `FATE_HAS_SODIUM` is not defined (dev builds only).

### Connection Lifecycle

- `disconnect()` clears all connection state including `authToken_`, crypto keys, reliability buffers, heartbeat/reconnect timers. Fires `onDisconnected` callback.
- `ConnectReject` handler clears `authToken_`, stops reconnect (`ReconnectPhase::Failed`), fires `onConnectRejected`. Stale tokens from prior sessions no longer cause infinite reconnect loops.
- Client skips ghost creation for own player entity (`SvEntityEnter` with `entityType==0` matching local character name). Stores `localPlayerPid_` for self-identification.

### Rate Limiting

Per-client, per-packet-type **token bucket** system (`server/rate_limiter.h`).

| Packet Type | Burst | Refill/sec |
|-------------|-------|------------|
| `CmdMove` | 65 | 60 |
| `CmdAction` | 5 | 2 |
| `CmdUseSkill` | 3 | 1 |
| `CmdChat` | 3 | 0.33 |
| `CmdMarket` | 3 | 0.5 |
| `CmdTrade` | 5 | 2 |
| `CmdShopBuy` | 3 | 1 |
| `CmdShopSell` | 3 | 1 |
| `CmdBankDeposit/WithdrawItem` | 3 | 1 |
| `CmdBankDeposit/WithdrawGold` | 3 | 1 |
| `CmdTeleport` | 2 | 0.5 |
| `CmdUseConsumable` | 30 | 3 |
| `CmdEnchant` | 3 | 1 |
| `CmdRepair` | 3 | 1 |
| `CmdExtractCore` | 3 | 1 |
| `CmdCraft` | 3 | 1 |
| `CmdSocketItem` | 3 | 1 |
| `CmdArena` | 3 | 1 |
| `CmdBattlefield` | 3 | 1 |
| `CmdPetCommand` | 3 | 1 |
| `CmdAllocateStat` | 5 | 2 | (disabled) |

**New NetClient send methods (10 total):** `sendEnchant(inventorySlot, useProtectionStone)`, `sendRepair(inventorySlot)`, `sendExtractCore(itemSlot, scrollSlot)`, `sendCraft(recipeId)`, `sendSocketItem(equipSlot, scrollItemId)`, `sendArena(action, mode)`, `sendBattlefield(action)`, `sendPetCommand(action, petDbId)`, `sendAllocateStat(statType, amount)`, `sendUseConsumableWithTarget(slot, targetEntityId)`.

**Result handlers (10 new server→client):** `onEnchantResult`, `onRepairResult`, `onExtractResult`, `onCraftResult`, `onSocketResult` (chat messages), `onPetUpdate` (PetPanel state + chat), `onArenaUpdate` (ArenaPanel state + chat), `onBattlefieldUpdate` (BattlefieldPanel state + chat), `onCollectionSync` (CollectionPanel completed IDs + bonus totals), `onCollectionDefs` (collection definitions sent once on login).

`SvCollectionSync` (0xBC): server→client collection completion state. Contains completed collection IDs + accumulated bonus totals (STR/INT/DEX/CON/WIS/MaxHP/MaxMP/Damage/Armor/CritRate/MoveSpeed). Sent on login and on each new completion.

`SvCollectionDefs` (0xBD): server→client collection definitions. Sends all collection definitions from `collection_definitions` DB table (name, description, category, condition, reward) once on login. Client populates CollectionPanel entries from this data.

`SvCostumeSync` (0xBE): server→client full costume state on login (showCostumes toggle, owned costume IDs, equipped slot→costumeDefId pairs). Sent from `loadPlayerCostumes()` after DB load.

`SvCostumeUpdate` (0xBF): server→client incremental costume update (type: 0=obtained, 1=equipped, 2=unequipped, 3=toggleChanged). Sent on equip/unequip/toggle/grant.

`SvCostumeDefs` (0xC0): server→client costume definition catalog (costumeDefId, displayName, slotType, visualIndex, rarity, source). Sent once on login before `SvCostumeSync`. Client stores in `costumeDefCache_` and enriches CostumePanel entries with display names, rarity, slot types.

`CmdEquipCostume`, `CmdUnequipCostume`, `CmdToggleCostumes`: client→server costume commands. Server validates ownership and slot type before persisting.

`SvAuroraStatus` (0xB9): server→client Aurora rotation status (favored faction + seconds remaining). Sent on zone entry, login, and hourly rotation change.

`CmdUseConsumable`: client→server consumable use. Extended with `uint32_t targetEntityId` (default 0) for Beacon of Calling. Zero means normal consumable (no target).

`SvConsumeResult`: server→client consumable use feedback (success flag + message string). Client wires `onConsumeResult` in `game_app.cpp` to display `[Item]` messages in ChatPanel. Covers skill book learn/error, potion use, and all consumable feedback.

Violations decay after 60 s of clean traffic. Excessive violations trigger disconnect.

### Replication & AOI

**Area of Interest** — spatial-hash culling (`SpatialHashEngine`, 128 px cells):
- Activation radius: **640 px** (20 tiles)
- Deactivation radius: **768 px** (hysteresis buffer)
- Scene-filtered: only entities sharing the client's `currentScene` are replicated
- **Visibility filter** — optional `visibilityFilter` callback on `ReplicationManager`, checked per-entity in `buildVisibility()`. Used for GM invisibility (hide entity from non-staff clients)

**Delta compression** — 32-bit field mask (expanded from 16-bit for costume support), only dirty fields serialized:

| Bit | Field | Bytes |
|-----|-------|-------|
| 0 | position | 8 |
| 1 | animFrame | 1 |
| 2 | flipX | 1 |
| 3 | currentHP | 4 |
| 4 | maxHP | 4 |
| 5 | moveState | 1 |
| 6 | animId | 2 |
| 7 | statusEffectMask | 4 |
| 8 | deathState | 1 |
| 9 | castingSkillId + progress | 3 |
| 10 | targetEntityId | 2 |
| 11 | level | 1 |
| 12 | faction | 1 |
| 13 | equipVisuals | 4 |
| 14 | pkStatus | 1 |
| 15 | honorRank | 1 |
| 16 | costumeVisuals | variable |

**Packet batching** — Multiple entity delta updates are packed into a single `SvEntityUpdateBatch` (0xBA) UDP packet (leading count byte + concatenated deltas, up to MAX_PAYLOAD_SIZE / 1182 bytes). Typically reduces 50 per-entity packets to 2-3 batched packets per tick per client (~90% header overhead reduction). Client unpacks with count-bounded loop. Legacy single-entity `SvEntityUpdate` (0x92) preserved for compatibility. Spatial index rebuild disabled (scene-based filtering sufficient at current scale).

**Tiered update frequency** — entities farther from the client update less often. HP changes always sent regardless of tier.

**Entity unregister/leave pitfall** — `unregisterEntity()` must NOT erase the handle→PID mapping before `sendDiffs()` runs, or `SvEntityLeave` is silently skipped (PID lookup returns null). The `recentlyUnregistered_` map preserves PIDs for one tick to bridge this gap. Any new code that removes entities from replication must go through `unregisterEntity()` — never erase from `handleToPid_` directly.

**Client ghost lifecycle** — Ghost entities are created on `SvEntityEnter` and destroyed on `SvEntityLeave`. Three invariants must hold:
1. Every entity in `ghostEntities_` must have a corresponding interpolation state (otherwise the interpolation loop writes `Vec2{0,0}` to its Transform)
2. `processDestroyQueue()` must be called after `netClient_.poll()` to actually free entities queued by `onEntityLeave`
3. On disconnect/reconnect, all ghost entities must be destroyed from the world (not just cleared from tracking maps) — otherwise zombie entities render forever

---

## Database

### Connection Model

| Connection | Owner | Purpose |
|------------|-------|---------|
| `gameDbConn_` | ServerApp (game thread) | All game repositories |
| `dbConn_` | AuthServer (auth thread) | Account registration & login |
| `dbPool_` | ServerApp | Thread-safe pool (min 5, max 50, +10 overflow) |

Fiber-based async dispatch via `DbDispatcher` (header-only) for non-blocking DB operations.

### Startup Caches (loaded once, read-only)

| Cache | Source Tables | Key Lookup |
|-------|-------------|------------|
| `ItemDefinitionCache` | `item_definitions` | `getDefinition(itemId)`, `getStatTypeForScroll(itemId)`, `getVisualIndex(itemId)`. **Used by both ServerApp and AuthServer** (auth server resolves equipped weapon/armor/hat visual indices for character preview) |
| `LootTableCache` | `loot_drops` + `loot_tables` | `rollLoot(lootTableId)` |
| `MobDefCache` | `mob_definitions` | by `mob_def_id` |
| `SkillDefCache` | `skill_definitions` + `skill_ranks` | by skill ID, by class |
| `SceneCache` | `scenes` | PvP status queries, `isAurora` flag for Aurora zone identification |
| `RecipeCache` | `crafting_recipes` + `recipe_ingredients` | tier-based lookup (Novice / Book I / II / III) |
| `PetDefinitionCache` | `pet_definitions` | `getDefinition(petDefId)` |
| `SpawnZoneCache` | `spawn_zones` | `getZonesForScene(sceneId)` |
| `CostumeCache` | `costume_definitions`, `mob_costume_drops` | `get(costumeDefId)`, `getBySlot()`, `getByRarity()`, `all()`, `getMobDrops(mobDefId)` |
| `CollectionCache` | `collection_definitions` | `getByConditionType()`, `all()` |

`ItemDefinitionCache` and `LootTableCache` live under `server/cache/`. `MobDefCache`, `SkillDefCache`, and `SceneCache` are defined in `server/db/definition_caches.h/.cpp`. `SpawnZoneCache` is in `server/db/spawn_zone_cache.h/.cpp`. `CostumeCache` and `CollectionCache` are in `server/cache/`.

### Repositories (read/write)

| Repository | Tables | Operations |
|-----------|--------|-----------|
| `AccountRepository` | `accounts` | create, findByUsername, updateLastLogin, setBan, clearBan, clearBanByUsername, setAdminRole |
| `CharacterRepository` | `characters` | createDefault, load, save. Includes `recall_scene` (default "Town") |
| `InventoryRepository` | `character_inventory` | load, save |
| `SkillRepository` | `character_skills`, `character_skill_bar`, `character_skill_points` | learned skills, 20-slot skill bar, skill points |
| `GuildRepository` | `guilds`, `guild_members`, `guild_invites` | create, disband, members, rank, XP, ownership transfer |
| `SocialRepository` | `friends`, `blocked_players` | friend lifecycle, block/unblock, online status |
| `MarketRepository` | `market_listings`, `market_transactions`, `jackpot_pool` | listings, transactions, jackpot |
| `TradeRepository` | `trade_sessions`, `trade_offers`, `trade_history` | session lifecycle, item/gold transfer, history |
| `BountyRepository` | `bounties`, `bounty_contributions`, `bounty_history` | place/cancel/claim, expiry, guild cooldown |
| `QuestRepository` | `character_quests`, `character_completed_quests` | active progress, completed IDs |
| `BankRepository` | `character_bank`, `character_bank_gold` | deposit/withdraw items and gold |
| `PetRepository` | `character_pets` | load, equip/unequip, save state, add XP |
| `PvpKillLogRepository` | `pvp_kill_log` | PK kill tracking |
| `ZoneMobStateRepository` | `zone_mob_deaths` | boss death persistence, respawn tracking |
| `CostumeRepository` | `player_costumes`, `player_equipped_costumes`, `characters` | grant, loadOwned, loadEquipped, equip/unequip, toggle state (pool-based) |
| `CollectionRepository` | `player_collections` | save completed, load completed IDs |

### Key Tables — Loot Pipeline

- **`item_definitions`** — `possible_stats` JSONB (two formats: `{"stat":"hp","weighted":true}` and legacy `{"name":"int","weight":1.0}`). `attributes` JSONB for bonus stats (mp_bonus, lifesteal, move_speed_pct, etc.).
- **`loot_drops`** — FK to `loot_tables.loot_table_id` and `item_definitions.item_id`. Columns: `drop_chance` (0.0-1.0), `min_quantity`, `max_quantity`.
- **`loot_tables`** — Referenced by `mob_definitions.loot_table_id`.
- **`character_inventory`** — UUID `instance_id`, `rolled_stats` JSONB, `socket_stat`/`socket_value`, `enchant_level`, `is_equipped`/`equipped_slot`. Starter equipment inserted on registration.
- **`zone_mob_deaths`** — `scene_name`, `zone_name`, `enemy_id`, `died_at_unix`, `respawn_seconds`. Loaded on server start, cleared after respawn.
- **`costume_definitions`** — `costume_def_id` PK, `costume_name`, `display_name`, `slot_type` (equipment slot), `visual_index`, `rarity` (0–4), `source` (drop/shop/collection/craft/event).
- **`player_costumes`** — player costume ownership. PK `(character_id, costume_def_id)`. `ON CONFLICT DO NOTHING` for idempotent grants.
- **`player_equipped_costumes`** — equipped costumes per slot. PK `(character_id, slot_type)`. `ON CONFLICT DO UPDATE` for slot replacement.
- **`mob_costume_drops`** — maps mob definitions to possible costume drops. `mob_def_id`, `costume_def_id` FK, `drop_chance` (REAL). Loaded into `CostumeCache::mobDrops_` at startup.

### Starter Equipment

| Class | Weapon | Body | Boots | Gloves |
|-------|--------|------|-------|--------|
| Warrior | `item_rusty_dagger` | `item_quilted_vest` | `item_worn_sandals` | `item_tattered_gloves` |
| Mage | `item_gnarled_stick` | `item_quilted_vest` | `item_worn_sandals` | `item_tattered_gloves` |
| Archer | `item_makeshift_bow` | `item_quilted_vest` | `item_worn_sandals` | `item_tattered_gloves` |

---

## Server Architecture

### Directory Layout

```
server/
├── server_app.h/.cpp              # ServerApp: main game loop, tick dispatch
├── server_main.cpp                # Entry point
├── server_spawn_manager.h/.cpp    # Zone-based mob spawning
├── dungeon_manager.h/.cpp         # Per-party instanced dungeon worlds (lastMinuteBroadcast, decline cooldowns)
├── rate_limiter.h                 # Per-client token-bucket rate limiting
├── player_lock.h                  # Concurrent player-action locking
├── nonce_manager.h                # Replay-attack prevention
├── gm_commands.h                  # GM / admin command framework (AdminRole enum, GMCommand with metadata, GMCommandRegistry)
├── target_validator.h             # Server-side target validation
│
├── auth/
│   └── auth_server.h/.cpp         # TLS auth (bcrypt, register + login, starter equipment, ItemDefinitionCache for equip preview)
│
├── handlers/                      # Message handlers (split from server_app)
│   ├── aurora_handler.cpp         # Aurora rotation tick, source-tagged buff apply/remove, death/recall override
│   ├── bank_handler.cpp           # 4 split methods (deposit/withdraw item/gold) with NPC proximity validation
│   ├── combat_handler.cpp         # Skill execution + Life Tap (mage_life_tap HP→MP conversion)
│   ├── consumable_handler.cpp     # 8 subtypes: HpPotion, MpPotion, SkillBook, StatReset, TownRecall, fate_coin, exp_boost, beacon_of_calling
│   ├── crafting_handler.cpp
│   ├── dungeon_handler.cpp        # Dungeon lifecycle + per-minute chat timer + 10s decline cooldown, post-transfer event locks
│   ├── equipment_handler.cpp
│   ├── destroy_item_handler.cpp    # CmdDestroyItem (0x34): discard inventory item, trade-blocked, resyncs
│   ├── inventory_move_handler.cpp  # CmdMoveItem (0x33): slot swap/stack via inventory.moveItem(), trade-blocked
│   ├── gauntlet_handler.cpp
│   ├── gm_handler.cpp
│   ├── maintenance_handler.cpp
│   ├── persistence_handler.cpp
│   ├── pet_handler.cpp
│   ├── pvp_event_handler.cpp
│   ├── ranking_handler.cpp
│   ├── shop_handler.cpp           # Buy/sell with NPC proximity validation + ShopComponent checks + quantity cap (MAX_STACK_SIZE). Costume purchases (itemType=="Costume") grant via costumeRepo instead of inventory
│   ├── sync_handler.cpp
│   ├── teleport_handler.cpp       # NPC teleport with proximity/level/gold/item validation + multi-stack item consumption
│   ├── costume_handler.cpp        # Costume equip/unequip/toggle/sync/load/sendDefs — ownership + slot validation
│   ├── collection_handler.cpp     # Collection condition checking, reward granting (stat bonuses + costume rewards), sync/defs
│   └── mob_death_handler.cpp      # XP/loot/gold/costume drop processing, party XP bonus, gauntlet/dungeon hooks
│
├── cache/
│   ├── item_definition_cache.h/.cpp    # Items (possible_stats, attributes)
│   ├── loot_table_cache.h/.cpp         # Loot tables (rollLoot, enchant rolling)
│   ├── recipe_cache.h/.cpp             # Crafting recipes (tier-based lookup)
│   ├── pet_definition_cache.h/.cpp     # Pet definitions
│   ├── costume_cache.h                 # Costume definitions + mob costume drops (CostumeCache)
│   └── collection_cache.h             # Collection definitions (CollectionCache)
│
├── db/
│   ├── db_connection.h/.cpp            # pqxx wrapper with reconnect
│   ├── db_pool.h/.cpp                  # Thread-safe connection pool (5-50)
│   ├── db_dispatcher.h                 # Fiber-based async DB dispatch (header-only)
│   ├── definition_caches.h/.cpp        # MobDefCache, SkillDefCache, SceneCache
│   ├── spawn_zone_cache.h/.cpp         # Spawn zone definitions per scene
│   ├── persistence_priority.h/.cpp     # Priority-based flush ordering
│   ├── player_dirty_flags.h            # Dirty tracking for selective persistence
│   ├── circuit_breaker.h               # DB failure circuit breaker
│   ├── tracked.h                       # Change-tracking wrapper
│   ├── account_repository.h/.cpp
│   ├── character_repository.h/.cpp
│   ├── inventory_repository.h/.cpp
│   ├── skill_repository.h/.cpp
│   ├── guild_repository.h/.cpp
│   ├── social_repository.h/.cpp
│   ├── market_repository.h/.cpp
│   ├── trade_repository.h/.cpp
│   ├── bounty_repository.h/.cpp
│   ├── quest_repository.h/.cpp
│   ├── bank_repository.h/.cpp
│   ├── pet_repository.h/.cpp
│   ├── pvp_kill_log_repository.h/.cpp
│   ├── zone_mob_state_repository.h/.cpp
│   ├── costume_repository.h/.cpp       # Costume persistence (grant, equip, unequip, toggle)
│   └── collection_repository.h/.cpp    # Collection completion persistence
│
└── wal/
    └── write_ahead_log.h/.cpp          # Write-ahead log for crash recovery
```

### Networking Layer

```
engine/net/
├── protocol.h                # Protocol ID, channel enum, constants
├── packet.h                  # Packet header layout (18 bytes)
├── packet_crypto.h/.cpp      # X25519 key exchange + XChaCha20-Poly1305 AEAD
├── reliability.h/.cpp        # ACK bitfields, RTT estimation, retransmission
├── replication.h/.cpp        # Delta-compressed entity replication
├── connection.h/.cpp         # Connection state machine
├── connection_cookie.h       # Stateless cookie for connect verification
├── reconnect_state.h         # Client reconnection state
├── aoi.h                     # Area-of-Interest spatial culling
├── update_frequency.h        # Tiered update rates by distance
├── ghost.h                   # Ghost (replicated entity proxy) tracking
├── byte_stream.h             # Serialization stream
├── game_messages.h           # Game-specific message definitions
├── interpolation.h           # Client-side entity interpolation
├── socket.h                  # Platform socket abstraction
├── socket_win32.cpp          # Win32 socket implementation
├── socket_posix.cpp          # POSIX socket implementation
├── net_client.h/.cpp         # Client networking (connect, send, receive)
├── net_server.h/.cpp         # Server networking (accept, broadcast, kick)
├── auth_protocol.h           # Auth handshake protocol definitions (CharacterPreview with equip visual indices)
└── auth_client.h/.cpp        # Client-side auth connection (TLS over TCP)
```

### Integration Pattern

- **ECS architecture** — `game/shared/` classes are standalone logic (pure C++, no engine deps). `game/components/game_components.h` wraps each in an ECS Component. `game/systems/` iterate entities and tick the wrapped logic.
- **Entity creation** — `EntityFactory` creates entities with all components attached (mirrors Unity prefab structure).
- **Callbacks** — game logic uses `std::function` callbacks; ECS systems wire these to engine actions.
- **RNG** — `thread_local std::mt19937` seeded from `std::random_device`.
- **Handler split** — packet handlers are in `server/handlers/` (35 files), dispatched from `ServerApp`. Includes `skill_handler.cpp` for `CmdActivateSkillRank` (0x35) and `CmdAssignSkillSlot` (0x36), `costume_handler.cpp` for equip/unequip/toggle/sync/defs.
- **GM command system** — `GMCommandRegistry` with `AdminRole` enum (Player=0, GM=1, Admin=2). Commands parsed from `CmdChat` messages starting with `/`. Each command has `category`, `usage`, `description` metadata for auto-generated `/admin` help. 17 commands across 6 categories: Player Management (kick/ban/permaban/unban/mute/unmute/whois/setrole), Teleportation (tp/tphere/goto), Spawning (spawnmob), Economy (additem/addgold/setlevel/addskillpoints), GM Tools (announce/dungeon/invisible/god), Help (admin). Ban/unban fully DB-wired with timed expiry. Mute is in-memory (timed, per-client). Invisibility uses replication visibility filter. God mode blocks damage at all 3 paths (PvP, skills, mob AI).
- **Loot pipeline** — server rolls loot on kill, spawns ground entities, replicates to clients, pickup via `CmdAction`, despawn after 120 s.
- **Persistence** — priority-based dirty-flag system with WAL for crash recovery. WAL truncation deferred until all async auto-saves commit (prevents data loss on crash between truncate and DB commit). Inventory items (equipment, bags, slots) saved via `saveInventoryForClient` on disconnect and when dirty. Auto-save every 5 min per-player (staggered by clientId). `PlayerLockMap` preserves mutexes while worker fibers hold references (prevents reconnect serialization break).
- **Reconnect grace period** — battlefield and dungeon players get 180s to rejoin after disconnect. `markDisconnected` preserves state in grace map; session setup checks grace entries and restores player. Arena disconnect = forfeit (no grace).
- **Event locks** — `playerEventLocks_` prevents double-enrollment. Dungeon locks set AFTER `transferPlayerToWorld` so the key matches the post-transfer entity ID. Cleared BEFORE exit transfer. Arena/battlefield locks use current entity ID directly.
- **Tick profiling** — slow tick warnings (>50ms) log per-section breakdown: `net` (poll/sessions), `world` (ECS update/spawn), `ecs` (combat leash/status effects/CC/timers/regen/casts), `despawn` (pet/death/items/boss), `repl` (entity replication), `events` (retransmit/DB drain/gauntlet/battlefield/arena), `save` (auto-save/persist queue/WAL flush).
- **Combat leash** — boss/mini-boss mobs reset to full HP and clear threat table after 15s with no incoming damage. Ticked in both main world and dungeon instances. Regular mobs unaffected.
