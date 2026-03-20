# Server-Side Mob Spawning & Replication — Design Spec

**Date:** 2026-03-19
**Status:** Approved

## Overview

Move mob lifecycle from client-only to server-authoritative. Server loads spawn zones from DB, creates mob entities, ticks MobAISystem, replicates mob state to clients via existing entity replication protocol. Client removes local SpawnSystem mob creation and renders server-replicated mobs instead.

## Database

**`spawn_zones` table** (Migration 005, already created):
- `zone_id`, `scene_id`, `zone_name`, `center_x`, `center_y`, `radius`, `mob_def_id`, `target_count`, `respawn_override_seconds`
- 6 rows seeded for WhisperingWoods (squirrel, giant_rat, horned_hare, timber_wolf, grizzly_bear, timber_alpha)

**`mob_definitions` table** (73 rows, loaded by `MobDefCache` at startup):
- All stats: HP, damage, armor, crit, aggro/attack/leash range, respawn, spawn levels, loot table, etc.

## Protocol Extension

### SvEntityEnterMsg — Add mob fields (entityType == 1)

```cpp
// New fields after faction, only serialized when entityType == 1 (mob)
std::string mobDefId;    // e.g. "timber_wolf" — client uses for sprite: mob_<id>.png
uint8_t     isBoss = 0;  // boss flag for nameplate "[Boss]" prefix
```

Backward compatible: existing entityType 0 (player) and 3 (dropped_item) unchanged.

### SvEntityUpdateMsg — Unchanged

Already has position (bit 0), flipX (bit 2), currentHP (bit 3). Sufficient for mob movement and HP sync.

## Server Components

### SpawnZoneCache (new: `server/db/spawn_zone_cache.h/.cpp`)

Loads `spawn_zones` table at startup. Groups rows by `scene_id`. Provides:
```cpp
struct SpawnZoneRow {
    int zoneId;
    std::string sceneId, zoneName, mobDefId;
    float centerX, centerY, radius;
    int targetCount;
    int respawnOverrideSeconds; // -1 = use mob_definitions default
};

class SpawnZoneCache {
    bool initialize(pqxx::connection& conn);
    const std::vector<SpawnZoneRow>& getZonesForScene(const std::string& sceneId) const;
};
```

### ServerSpawnManager (new: `server/server_spawn_manager.h/.cpp`)

Manages mob lifecycle on the server. Created during `ServerApp::init()`.

**Responsibilities:**
- On init: iterate spawn zone rows for current scene, create mob entities
- Track alive mobs per zone row: `map<zoneId, vector<EntityHandle>>`
- On mob death: start respawn timer, send `SvEntityLeave`
- On respawn timer expiry: create new mob at random position within zone radius, register with replication, broadcast `SvEntityEnter`
- Ticked every server frame via `ServerApp::tick()`

**Mob entity creation:**
```
For each SpawnZoneRow:
  CachedMobDef* def = mobDefCache.get(row.mobDefId)
  For i in 0..row.targetCount:
    Entity* mob = world.createEntity(def.displayName)
    - Transform: random position within (centerX±radius, centerY±radius)
    - EnemyStatsComponent: HP, damage, armor, etc from def, scaled to random level in [min,max]
    - MobAIComponent: aggro/attack/leash range from def
    - MobNameplateComponent: name, level, boss flag
    - NO SpriteComponent (server doesn't render)
    Register with ReplicationManager
```

### MobAISystem — Re-added to server World

Already fully implemented. Needs mob entities in the World to tick. Once `ServerSpawnManager` creates them, MobAISystem works as-is.

### onMobAttackResolved callback — Already written

Wired in `ServerApp::run()`. Broadcasts `SvCombatEventMsg` and `SvDeathNotifyMsg`. Just needs MobAISystem re-enabled (which happens when mobs exist).

## Replication Changes

### buildEnterMessage (ReplicationManager)

Extend to populate new mob fields when building `SvEntityEnterMsg` for entityType 1:
```cpp
if (entity has EnemyStatsComponent) {
    msg.entityType = 1;
    msg.mobDefId = enemyStats.mobDefId;  // need to store this on EnemyStats
    msg.isBoss = enemyStats.isBoss ? 1 : 0;
}
```

**Requires:** Add `std::string mobDefId` field to `EnemyStats` (set during mob creation from `CachedMobDef.mobDefId`).

### Server tick replication

Existing `ReplicationManager::update()` already broadcasts position changes via `SvEntityUpdate`. Mob position changes from MobAISystem movement will automatically replicate.

Need to also broadcast HP changes when mobs take damage (set fieldMask bit 3).

## Client Changes

### Remove client-side mob spawning

- `SpawnSystem` mob creation disabled when connected to server (mobs come from replication)
- Keep `SpawnSystem` for offline/editor mode

### Enhance createGhostMob

```cpp
static Entity* createGhostMob(World& world, const SvEntityEnterMsg& msg) {
    Entity* entity = world.createEntity(msg.name);
    entity->setTag("mob");

    auto* t = entity->addComponent<Transform>(msg.position);

    // Sprite from mob_def_id
    auto* sprite = entity->addComponent<SpriteComponent>();
    std::string spritePath = "assets/sprites/mob_" + msg.mobDefId + ".png";
    sprite->texture = TextureCache::instance().load(spritePath);
    sprite->size = {32.0f, 32.0f};

    // EnemyStats for targeting/HP bars
    auto* es = entity->addComponent<EnemyStatsComponent>();
    es->stats.enemyName = msg.name;
    es->stats.level = msg.level;
    es->stats.currentHP = msg.currentHP;
    es->stats.maxHP = msg.maxHP;
    es->stats.isAlive = msg.currentHP > 0;

    // Nameplate
    auto* np = entity->addComponent<MobNameplateComponent>();
    np->displayName = msg.name;
    np->level = msg.level;
    np->isBoss = msg.isBoss != 0;
    np->visible = true;

    return entity;
}
```

### onEntityUpdate for mobs

Extend existing handler to update `EnemyStatsComponent.currentHP` when bit 3 is set. Also update `isAlive` flag. Handle mob death visual (hide sprite when HP reaches 0).

### Combat flow

- **Player → mob (client-side prediction):** Client shows floating text immediately from local CombatActionSystem. Sends `CmdAction`. Server validates, applies actual damage, broadcasts `SvCombatEventMsg` (which updates mob HP on all clients).
- **Mob → player (server authority):** Server broadcasts `SvCombatEventMsg`. Client shows floating text on receipt.

## Files Changed

| File | Action | Purpose |
|------|--------|---------|
| `engine/net/protocol.h` | Modify | Add mobDefId + isBoss to SvEntityEnterMsg |
| `game/shared/enemy_stats.h` | Modify | Add `std::string mobDefId` field |
| `server/db/spawn_zone_cache.h/.cpp` | Create | Load spawn_zones table |
| `server/server_spawn_manager.h/.cpp` | Create | Server-side mob lifecycle |
| `server/server_app.h/.cpp` | Modify | Init SpawnZoneCache + ServerSpawnManager, tick spawns, re-add MobAISystem |
| `engine/net/replication.cpp` | Modify | Populate mob fields in buildEnterMessage |
| `game/entity_factory.h` | Modify | Enhance createGhostMob to use SvEntityEnterMsg |
| `game/game_app.cpp` | Modify | Pass full msg to createGhostMob, update mob HP from SvEntityUpdate |
| `game/systems/mob_ai_system.h` | No change | Already works, just needs entities |

## Security

- All mob stats server-authoritative (HP, damage, position)
- Client cannot create fake mobs or modify mob HP
- Mob positions validated by server's MobAISystem movement
- Spawn zones defined in DB, not client-editable

## Testing

- Unit test: SpawnZoneCache loads rows correctly
- Unit test: SvEntityEnterMsg mob fields round-trip
- Integration: Server creates mobs, client sees them via replication
- Integration: Mob death → SvEntityLeave → respawn timer → SvEntityEnter
- Manual: Walk near mobs, see nameplates, get attacked, kill mob, see respawn
