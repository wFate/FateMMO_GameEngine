# Loot, Ground Items, and Boss Spawn Systems — Design Spec

**Date:** 2026-03-18
**Status:** Approved
**Source:** C# prototype at `C:\Users\Caleb\FateRPG_Prototype2\Assets\Scripts`

## Overview

Port the remaining loot/drop and boss spawning systems from the Unity/C# prototype into the C++ ECS engine. This covers: item definition caching, loot table rolling, ground item entities, item pickup, boss fixed-position spawning, and mob death persistence.

Two independent tracks:
- **Track A (steps 1–8):** Loot pipeline — item defs → loot tables → ground items → pickup → combat integration
- **Track B (steps 9–10):** Boss spawning — persistence repo → boss spawn system

---

## Systems to Implement

### 1. ItemDefinitionCache

Server-side cache loaded from PostgreSQL on startup.

**File:** `server/cache/item_definition_cache.h/.cpp` (server-only, uses pqxx)

**Data structure — `CachedItemDefinition`:**
- Identity: `itemId`, `displayName`, `description`
- Classification: `itemType`, `subtype`, `rarity`
- Requirements: `levelReq`, `classReq` (DB columns: `level_req`, `class_req`)
- Combat: `damageMin`, `damageMax` (no `attackSpeed` — not in DB schema)
- Defense: `armor`
- Enchantment: `maxEnchant`, `isSocketable`
- Binding: `isSoulbound`
- Stacking: `maxStack`
- Economy: `goldValue`
- Visual: `iconPath`
- Rolled stats: `possibleStats` (parsed from JSON into `PossibleStat` vector)
- Attributes: `attributes` (parsed from JSON into `std::unordered_map<std::string, nlohmann::json>`)
- Helpers: `isWeapon()`, `isArmor()`, `isAccessory()`, `isEquipment()`, `hasPossibleStats()`
- Attribute accessors: `getIntAttribute(key)`, `getFloatAttribute(key)`, `getStringAttribute(key)`

**DB column → field mapping:**
| DB Column | Struct Field |
|-----------|-------------|
| `item_id` | `itemId` |
| `name` | `displayName` |
| `type` | `itemType` |
| `subtype` | `subtype` |
| `class_req` | `classReq` |
| `level_req` | `levelReq` |
| `damage_min` | `damageMin` |
| `damage_max` | `damageMax` |
| `armor` | `armor` |
| `attributes` | `attributes` (JSONB → parsed map) |
| `description` | `description` |
| `gold_value` | `goldValue` |
| `max_stack` | `maxStack` |
| `icon_path` | `iconPath` |
| `possible_stats` | `possibleStats` (JSONB → PossibleStat vector) |
| `is_socketable` | `isSocketable` |
| `is_soulbound` | `isSoulbound` |
| `rarity` | `rarity` |
| `max_enchant` | `maxEnchant` |

**Interface:**
```cpp
class ItemDefinitionCache {
public:
    void initialize(pqxx::connection& conn);
    const CachedItemDefinition* getDefinition(const std::string& itemId) const;
    std::vector<const CachedItemDefinition*> getItemsByType(const std::string& itemType) const;
private:
    std::unordered_map<std::string, CachedItemDefinition> definitions_;
};
```

**DB query:** `SELECT * FROM item_definitions` — loads all rows, parses `possible_stats` and `attributes` JSON via nlohmann/json.

### 2. LootTableCache

Server-side cache loaded from PostgreSQL on startup. Rolls item drops when mobs die.

**File:** `server/cache/loot_table_cache.h/.cpp` (server-only, uses pqxx)

**Data structures:**
```cpp
struct LootDropEntry {
    std::string itemId;
    float dropChance;    // 0.0–1.0
    int minQuantity;
    int maxQuantity;
};

struct LootDropResult {
    ItemInstance item;
    std::string itemName;  // for logging
};
```

**Interface:**
```cpp
class LootTableCache {
public:
    void initialize(pqxx::connection& conn, const ItemDefinitionCache& itemDefs);
    std::vector<LootDropResult> rollLoot(const std::string& lootTableId) const;
private:
    const ItemDefinitionCache* itemDefs_ = nullptr;
    std::unordered_map<std::string, std::vector<LootDropEntry>> tables_;
    int rollEnchantLevel(const std::string& subtype) const;
};
```

**Loot rolling algorithm:**
1. Look up entries for `lootTableId`
2. For each entry, roll against `dropChance`
3. On success: roll quantity in `[minQuantity, maxQuantity]`
4. Get item definition from `itemDefs_->getDefinition(entry.itemId)`
5. Roll stats via `ItemStatRoller::rollStats()` using the item's `possibleStats`
6. Roll enchant level (weighted: +0=40%, +1=25%, +2=15%, +3=10%, +4=5%, +5=3%, +6=1.5%, +7=0.5%)
7. Only enchantable subtypes get enchants: Sword, Wand, Bow, Shield, Head, Armor, Gloves, Boots, Feet
8. Return `LootDropResult` with the fully rolled `ItemInstance`

**DB query:** `SELECT * FROM loot_drops` — groups by `loot_table_id`.

**Depends on:** `ItemDefinitionCache` initialized first, passed to `initialize()`.

### 3. Protocol Changes

Before any spawning or replication work, extend the wire protocol.

**Modified file:** `engine/net/protocol.h`

**`SvEntityEnterMsg` — conditional item fields after base fields:**
```cpp
struct SvEntityEnterMsg {
    // ... existing fields ...
    // entityType: 0=player, 1=mob, 2=npc, 3=dropped_item

    // Dropped item fields (only when entityType == 3)
    std::string itemId;
    int32_t     quantity    = 0;
    uint8_t     isGold      = 0;
    int32_t     goldAmount  = 0;
    int32_t     enchantLevel = 0;
    std::string rarity;

    void write(ByteWriter& w) const {
        // ... existing writes ...
        if (entityType == 3) {
            w.writeString(itemId);
            w.writeI32(quantity);
            w.writeU8(isGold);
            w.writeI32(goldAmount);
            w.writeI32(enchantLevel);
            w.writeString(rarity);
        }
    }

    static SvEntityEnterMsg read(ByteReader& r) {
        // ... existing reads ...
        if (m.entityType == 3) {
            m.itemId       = r.readString();
            m.quantity     = r.readI32();
            m.isGold       = r.readU8();
            m.goldAmount   = r.readI32();
            m.enchantLevel = r.readI32();
            m.rarity       = r.readString();
        }
        return m;
    }
};
```

This works because `entityType` is already read before the conditional section. Clients that don't recognize type 3 will skip the trailing bytes naturally since they already consumed the base fields.

**New message — `SvLootPickupMsg`:**
```cpp
struct SvLootPickupMsg {
    std::string itemId;      // item picked up (empty if gold)
    std::string displayName; // "Iron Sword +3" or "Gold"
    int32_t     quantity = 0;
    uint8_t     isGold   = 0;
    int32_t     goldAmount = 0;
    std::string rarity;

    void write(ByteWriter& w) const { /* ... */ }
    static SvLootPickupMsg read(ByteReader& r) { /* ... */ }
};
```

**Modified file:** `engine/net/packet.h`
```cpp
constexpr uint8_t SvLootPickup = 0x98;
```

### 4. DroppedItemComponent

Ground loot entity component.

**File:** `game/components/dropped_item_component.h`

```cpp
struct DroppedItemComponent {
    FATE_COMPONENT_COLD(DroppedItemComponent)

    std::string itemId;
    int quantity = 1;
    int enchantLevel = 0;
    std::string rolledStatsJson;
    std::string rarity;

    bool isGold = false;
    int goldAmount = 0;

    uint32_t ownerEntityId = 0;  // 0 = free for all (top damager gets ownership)
    float spawnTime = 0.0f;
    float despawnAfter = 120.0f; // 2 minutes
};
```

**Registration:** `FATE_COMPONENT_COLD` tier. Traits: `Serializable | Networked` (no `Persistent` — ground loot is transient). Reflection: `FATE_REFLECT_EMPTY` (custom serialization in replication).

### 5. EntityFactory::createDroppedItem() + Replication

**New factory method added to:** `game/entity_factory.h`

```cpp
static Entity* createDroppedItem(World& world, Vec2 position, bool isGold);
```

Creates entity with:
- `Transform` at position
- `SpriteComponent` (gold coin sprite or item icon)
- `DroppedItemComponent`
- Tag: `"dropped_item"`
- No collider, no controller

**Replication integration (`engine/net/replication.cpp`):**

`buildEnterMessage()` — add entity type `3` branch:
```cpp
auto* droppedItem = entity->getComponent<DroppedItemComponent>();
if (droppedItem) {
    msg.entityType = 3; // dropped item
    msg.name = droppedItem->isGold ? "Gold" : droppedItem->itemId;
    msg.itemId = droppedItem->itemId;
    msg.quantity = droppedItem->quantity;
    msg.isGold = droppedItem->isGold ? 1 : 0;
    msg.goldAmount = droppedItem->goldAmount;
    msg.enchantLevel = droppedItem->enchantLevel;
    msg.rarity = droppedItem->rarity;
}
```

`buildCurrentState()` — dropped items are static (no position/animation changes after spawn), so updates will produce zero dirty bits and no bandwidth.

### 6. DroppedItemSpawner

Static utility for spawning ground item entities.

**File:** `game/shared/dropped_item_spawner.h`

**Interface:**
```cpp
class DroppedItemSpawner {
public:
    static Entity* spawnGold(World& world, ReplicationManager& repl,
                             Vec2 position, int amount, uint32_t ownerEntityId, float gameTime);

    static Entity* spawnItem(World& world, ReplicationManager& repl,
                             Vec2 position, const ItemInstance& item,
                             const std::string& rarity,
                             uint32_t ownerEntityId, float gameTime);

    // Determines top damager from damageByAttacker, sets as loot owner
    static void spawnMobLoot(World& world, ReplicationManager& repl,
                             Vec2 position,
                             const std::vector<LootDropResult>& drops,
                             const std::unordered_map<uint32_t, int>& damageByAttacker,
                             int goldAmount, float gameTime);

private:
    static Vec2 calculateOffset(int index, int total);
    static uint32_t getTopDamager(const std::unordered_map<uint32_t, int>& damageByAttacker);
};
```

**Loot ownership:** `getTopDamager()` iterates `damageByAttacker` and returns the entity ID with the highest damage dealt. This becomes the `ownerEntityId` on all spawned items. When `ownerEntityId != 0`, only that player (or their party members) can pick up the item.

**Offset algorithm:** Grid layout with 0.3f spacing, max 4 per row, centered around spawn position, small random jitter for natural scatter.

**Each spawn call:**
1. Creates entity via `EntityFactory::createDroppedItem()`
2. Registers with `ReplicationManager` using a new `PersistentId`
3. Sets `DroppedItemComponent` fields

### 7. LootPickupSystem

Handles command-based pickup and despawn timers.

**File:** `game/systems/loot_pickup_system.h`

**Pickup mechanism:** Uses the existing `CmdAction` with `actionType = 3` (pickup). The client sends a `CmdAction` targeting the dropped item's `PersistentId`. The server validates proximity and loot rights.

**Dependencies (public members):**
```cpp
ReplicationManager* replication = nullptr;
NetServer* netServer = nullptr;
```

**Update loop (each tick):**
1. **Despawn check:** Collect all `DroppedItemComponent` entities. For each: check `gameTime - spawnTime > despawnAfter` → destroy expired items, unregister from replication
2. **Pickup processing:** When a `CmdAction(actionType=3)` arrives:
   a. Resolve `targetId` to entity via `ReplicationManager::getEntityHandle()`
   b. Verify entity has `DroppedItemComponent`
   c. Check proximity: player must be within 1.0 tile of the item
   d. Validate loot rights: `ownerEntityId == 0` OR matches player OR player is in owner's party
   e. If gold: add to player's `Inventory.gold`, split among party members in same scene if applicable
   f. If item: reconstruct `ItemInstance` from component data, call `inventory.addItem()`
   g. Send `SvLootPickupMsg` to the player (item name, quantity, rarity)
   h. Destroy entity, unregister from replication

### 8. CombatActionSystem Integration

**Modified file:** `game/systems/combat_action_system.h`

**New dependencies (public members):**
```cpp
LootTableCache* lootTableCache = nullptr;
ReplicationManager* replication = nullptr;
```

**Updated `onMobDeath()` flow:**
1. Quest notification (existing)
2. XP award (existing)
3. Roll loot table (NEW): if `!es.lootTableId.empty()`, call `lootTableCache->rollLoot(es.lootTableId)`
4. Spawn ground loot (NEW): `DroppedItemSpawner::spawnMobLoot()` with drops, `es.damageByAttacker`, rolled gold amount, and `gameTime_`
5. Gold drops on ground (CHANGED): gold now spawns as a ground entity via the spawner instead of going directly to inventory
6. Honor award (existing — top damager only for Boss/MiniBoss/RaidBoss)
7. Floating text (existing — XP text stays, gold/item text deferred to pickup)
8. Hide sprite + clear target (existing)

### 9. ZoneMobStateRepository

Mob death state persistence to PostgreSQL. Independent from loot pipeline (Track B).

**File:** `server/db/zone_mob_state_repository.h/.cpp`

**Data structure:**
```cpp
struct DeadMobRecord {
    std::string enemyId;
    int mobIndex = 0;
    int64_t diedAtUnix = 0;
    int respawnSeconds = 0;

    bool hasRespawned() const;
    float getRemainingRespawnTime() const;
};
```

**Interface:**
```cpp
class ZoneMobStateRepository {
public:
    explicit ZoneMobStateRepository(pqxx::connection& conn);

    bool saveZoneDeaths(const std::string& sceneName, const std::string& zoneName,
                        const std::vector<DeadMobRecord>& deadMobs);
    std::vector<DeadMobRecord> loadZoneDeaths(const std::string& sceneName,
                                               const std::string& zoneName);
    bool clearZoneDeaths(const std::string& sceneName, const std::string& zoneName);
    int cleanupExpiredDeaths();

private:
    pqxx::connection& conn_;
};
```

**DB table:** `zone_mob_deaths` (already in schema) — columns: `scene_name`, `zone_name`, `enemy_id`, `mob_index`, `died_at_unix`, `respawn_seconds`.

### 10. BossSpawnPointComponent + BossSpawnSystem

**Component file:** `game/components/boss_spawn_point_component.h`

```cpp
struct BossSpawnPointComponent {
    FATE_COMPONENT_COLD(BossSpawnPointComponent)

    std::string bossDefId;
    std::vector<Vec2> spawnCoordinates;
    int levelOverride = 0;  // 0 = use DB range

    // Tracked state
    EntityId bossEntityId = INVALID_ENTITY;
    bool bossAlive = false;
    float respawnAt = 0.0f;
    int lastSpawnIndex = -1;  // avoid respawning at same coord
    bool initialized = false;
};
```

**System file:** `game/systems/boss_spawn_system.h`

**Dependencies (public members):**
```cpp
ZoneMobStateRepository* mobStateRepo = nullptr;
ReplicationManager* replication = nullptr;
```

**Tick interval:** 0.25s (matching C#).

**Lifecycle:**
1. On first tick per zone: call `mobStateRepo->loadZoneDeaths()` to restore persisted death timers
2. `spawnBoss()` — picks random coordinate from list (excluding `lastSpawnIndex`), calls `EntityFactory::createMob()` with `isBoss=true` at the fixed position, registers with `ReplicationManager`
3. `detectDeathAndScheduleRespawn()` — monitors `EnemyStats.isAlive`, on death: records `respawnAt`, saves to DB via `mobStateRepo->saveZoneDeaths()`
4. `processRespawn()` — when timer expires, respawns at different coordinate, clears DB death record

**Separate from SpawnSystem** — bosses don't use zone bounds or random positioning.

### 11. Client-Side Handling

**Modified file:** `game/game_app.cpp` (or equivalent client entity handler)

When the client receives `SvEntityEnterMsg` with `entityType == 3`:
1. Create a local-only entity with `Transform` + `SpriteComponent` at the received position
2. Set sprite to gold coin icon if `isGold`, otherwise look up item icon by `itemId`
3. Optionally add a small floating label showing the item name with rarity coloring
4. Tag as `"dropped_item"` for local rendering/interaction

When `SvEntityLeaveMsg` arrives for a dropped item (despawn or pickup by another player):
1. Destroy the local entity

When player clicks/taps a nearby dropped item:
1. Send `CmdAction(actionType=3, targetId=droppedItem.persistentId)` to server

When `SvLootPickupMsg` arrives:
1. Display floating text: "+1 Iron Sword" or "+50 Gold" with rarity coloring
2. Play pickup sound effect

### 12. Component Registration

**Modified file:** `game/register_components.h`

Register both new components:
- `DroppedItemComponent`: `Serializable | Networked` (no Persistent)
- `BossSpawnPointComponent`: `Serializable | Persistent`

---

## File Summary

| New File | Purpose |
|----------|---------|
| `game/shared/item_definition_cache.h/.cpp` | Item def DB cache |
| `game/shared/loot_table_cache.h/.cpp` | Loot table DB cache + rolling |
| `game/shared/dropped_item_spawner.h` | Static spawn utilities |
| `game/components/dropped_item_component.h` | Ground loot component |
| `game/components/boss_spawn_point_component.h` | Boss spawn point component |
| `game/systems/boss_spawn_system.h` | Fixed-position boss spawning |
| `game/systems/loot_pickup_system.h` | Pickup detection + despawn |
| `server/db/zone_mob_state_repository.h/.cpp` | Mob death persistence |

| Modified File | Change |
|---------------|--------|
| `game/entity_factory.h` | Add `createDroppedItem()` |
| `game/systems/combat_action_system.h` | Loot pipeline in `onMobDeath()`, new member pointers |
| `engine/net/replication.cpp` | Entity type 3 handling in buildEnterMessage |
| `engine/net/protocol.h` | Conditional item fields in `SvEntityEnterMsg`, new `SvLootPickupMsg` |
| `engine/net/packet.h` | New `SvLootPickup = 0x98` packet type |
| `game/register_components.h` | Register new components |
| `game/components/game_components.h` | Include new component headers |
| `game/game_app.cpp` | Client-side entity type 3 handling, pickup input |
| `server/server_app.cpp` | Initialize caches on startup, wire dependencies |
| `CMakeLists.txt` | Add new .cpp files to build |

## Implementation Order

**Track A — Loot Pipeline:**
1. `ItemDefinitionCache` (foundation — everything needs item defs)
2. `LootTableCache` (depends on ItemDefinitionCache)
3. Protocol changes (`SvEntityEnterMsg` type 3 fields, `SvLootPickupMsg`, packet type)
4. `DroppedItemComponent` + `EntityFactory::createDroppedItem()`
5. Replication integration (entity type 3 in `buildEnterMessage`)
6. `DroppedItemSpawner`
7. `LootPickupSystem` (pickup via CmdAction + despawn)
8. `CombatActionSystem` integration (ties loot pipeline together)

**Track B — Boss Spawning (independent, can be parallel):**
9. `ZoneMobStateRepository`
10. `BossSpawnPointComponent` + `BossSpawnSystem`

**Final wiring:**
11. `server_app.cpp` — initialize caches, wire dependencies to systems
12. `game_app.cpp` — client-side entity type 3 handling
13. Component registration + `CMakeLists.txt`

## C# Source Reference

| C# File | C++ Target |
|---------|-----------|
| `Scripts/Server/Database/ItemDefinitionCache.cs` | `game/shared/item_definition_cache.h/.cpp` |
| `Scripts/Server/Database/LootTableCache.cs` | `game/shared/loot_table_cache.h/.cpp` |
| `Scripts/Loot/DroppedItem.cs` | `game/components/dropped_item_component.h` |
| `Scripts/Loot/DroppedItemSpawner.cs` | `game/shared/dropped_item_spawner.h` |
| `Scripts/Networking/BossSpawnPoint.cs` | `game/components/boss_spawn_point_component.h` + `game/systems/boss_spawn_system.h` |
| `Scripts/Networking/NetworkEnemyStats.cs` (Die/Award methods) | `game/systems/combat_action_system.h` modifications |
| `Scripts/Server/Database/ZoneMobStateRepository.cs` | `server/db/zone_mob_state_repository.h/.cpp` |
| `Scripts/Inventory/ItemStatRoller.cs` | Already ported: `game/shared/item_stat_roller.h/.cpp` |
