# Server-Side Mob Spawning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Server creates mob entities from DB spawn zones, ticks MobAISystem, replicates mob state to clients. Client renders server-replicated mobs instead of locally-spawned ones.

**Architecture:** New `SpawnZoneCache` loads spawn_zones table. `ServerSpawnManager` creates mob entities in server World using `MobDefCache` data. `MobAISystem` ticks them. `ReplicationManager` broadcasts enter/update/leave to clients. Client `createGhostMob` enhanced to render full mob visuals from server data.

**Tech Stack:** C++ 20, custom ECS, PostgreSQL via pqxx, UDP replication

**Spec:** `Docs/superpowers/specs/2026-03-19-server-mob-spawning-design.md`

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" --build out/build/x64-Debug --config Debug 2>&1 | grep -E "error C|FAILED|Linking|ninja: build"
```

**CRITICAL:** `touch` ALL edited .cpp files before building. Do NOT add `Co-Authored-By` lines.

**Test command:** `./out/build/x64-Debug/fate_tests.exe`

**Codebase facts:**
- `ByteWriter` requires buffer+capacity: `uint8_t buf[256]; ByteWriter w(buf, sizeof(buf));`
- `World::forEach` supports 1 or 2 template component types only
- `EnemyStats::enemyId` is the mob definition ID (same as `CachedMobDef::mobDefId`)
- `MobDefCache::get(mobDefId)` returns `const CachedMobDef*`
- `replication_.registerEntity(handle, pid)` / `unregisterEntity(handle)`
- `PersistentId::generate(seed)` creates unique persistent IDs
- Server's `world_.update(dt)` ticks all registered systems

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `engine/net/protocol.h` | Modify | Add mobDefId + isBoss to SvEntityEnterMsg |
| `game/shared/enemy_stats.h` | Modify | Add `bool isBoss` field |
| `server/db/spawn_zone_cache.h` | Create | SpawnZoneCache: load spawn_zones table |
| `server/db/spawn_zone_cache.cpp` | Create | SpawnZoneCache implementation |
| `server/server_spawn_manager.h` | Create | ServerSpawnManager: mob lifecycle |
| `server/server_spawn_manager.cpp` | Create | ServerSpawnManager implementation |
| `server/server_app.h` | Modify | Add SpawnZoneCache + ServerSpawnManager members |
| `server/server_app.cpp` | Modify | Init caches, add MobAISystem, tick spawns, wire combat callback |
| `engine/net/replication.cpp` | Modify | Populate mobDefId + isBoss in buildEnterMessage |
| `game/entity_factory.h` | Modify | Enhance createGhostMob to accept full mob data |
| `game/game_app.cpp` | Modify | Pass full msg to createGhostMob, handle mob HP updates, disable local SpawnSystem |
| `tests/test_spawn_zone_cache.cpp` | Create | SpawnZoneCache + protocol tests |

---

## Task 1: Protocol Extension — mobDefId + isBoss on SvEntityEnterMsg

**Files:**
- Modify: `engine/net/protocol.h:101-157`
- Modify: `game/shared/enemy_stats.h`
- Test: `tests/test_spawn_zone_cache.cpp`

- [ ] **Step 1: Add test for mob enter message round-trip**

Create `tests/test_spawn_zone_cache.cpp`:
```cpp
#include <doctest/doctest.h>
#include "engine/net/protocol.h"
using namespace fate;

TEST_CASE("SvEntityEnterMsg mob fields round-trip") {
    SvEntityEnterMsg src;
    src.persistentId = 12345;
    src.entityType = 1; // mob
    src.position = {100.0f, 200.0f};
    src.name = "Timber Wolf";
    src.level = 5;
    src.currentHP = 80;
    src.maxHP = 100;
    src.faction = 0;
    src.mobDefId = "timber_wolf";
    src.isBoss = 0;

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvEntityEnterMsg::read(r);

    CHECK(dst.entityType == 1);
    CHECK(dst.name == "Timber Wolf");
    CHECK(dst.mobDefId == "timber_wolf");
    CHECK(dst.isBoss == 0);
}

TEST_CASE("SvEntityEnterMsg player has no mob fields") {
    SvEntityEnterMsg src;
    src.entityType = 0; // player
    src.name = "TestPlayer";

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvEntityEnterMsg::read(r);

    CHECK(dst.entityType == 0);
    CHECK(dst.mobDefId.empty());
}
```

- [ ] **Step 2: Add mob fields to SvEntityEnterMsg**

In `engine/net/protocol.h`, add fields after `rarity` (line 117):
```cpp
    // Mob fields (only serialized when entityType == 1)
    std::string mobDefId;    // e.g. "timber_wolf" for sprite lookup
    uint8_t     isBoss = 0;
```

In `write()`, after the entityType==3 block (line 135), add:
```cpp
        if (entityType == 1) {
            w.writeString(mobDefId);
            w.writeU8(isBoss);
        }
```

In `read()`, after the entityType==3 block (line 155), add:
```cpp
        if (m.entityType == 1) {
            m.mobDefId = r.readString();
            m.isBoss   = r.readU8();
        }
```

- [ ] **Step 3: Add isBoss to EnemyStats**

In `game/shared/enemy_stats.h`, add after `isGauntletMob` (around line 47):
```cpp
    bool isBoss = false;
```

- [ ] **Step 4: Build and run tests**

- [ ] **Step 5: Commit**
```bash
git add engine/net/protocol.h game/shared/enemy_stats.h tests/test_spawn_zone_cache.cpp
git commit -m "feat: add mobDefId + isBoss to SvEntityEnterMsg and EnemyStats"
```

---

## Task 2: SpawnZoneCache — Load spawn_zones table

**Files:**
- Create: `server/db/spawn_zone_cache.h`
- Create: `server/db/spawn_zone_cache.cpp`
- Test: `tests/test_spawn_zone_cache.cpp` (append)

- [ ] **Step 1: Create SpawnZoneCache header**

Create `server/db/spawn_zone_cache.h`:
```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <pqxx/pqxx>

namespace fate {

struct SpawnZoneRow {
    int zoneId = 0;
    std::string sceneId;
    std::string zoneName;
    std::string mobDefId;
    float centerX = 0, centerY = 0;
    float radius = 100;
    int targetCount = 3;
    int respawnOverrideSeconds = -1; // -1 = use mob_definitions default
};

class SpawnZoneCache {
public:
    bool initialize(pqxx::connection& conn);
    [[nodiscard]] const std::vector<SpawnZoneRow>& getZonesForScene(const std::string& sceneId) const;
    [[nodiscard]] int count() const;

private:
    std::unordered_map<std::string, std::vector<SpawnZoneRow>> zonesByScene_;
    static const std::vector<SpawnZoneRow> empty_;
};

} // namespace fate
```

- [ ] **Step 2: Create SpawnZoneCache implementation**

Create `server/db/spawn_zone_cache.cpp`:
```cpp
#include "server/db/spawn_zone_cache.h"
#include "engine/core/logger.h"

namespace fate {

const std::vector<SpawnZoneRow> SpawnZoneCache::empty_;

bool SpawnZoneCache::initialize(pqxx::connection& conn) {
    try {
        pqxx::work txn(conn);
        auto result = txn.exec(
            "SELECT zone_id, scene_id, zone_name, mob_def_id, "
            "center_x, center_y, radius, target_count, respawn_override_seconds "
            "FROM spawn_zones ORDER BY scene_id, zone_id");
        txn.commit();

        zonesByScene_.clear();
        for (const auto& row : result) {
            SpawnZoneRow z;
            z.zoneId     = row["zone_id"].as<int>();
            z.sceneId    = row["scene_id"].as<std::string>();
            z.zoneName   = row["zone_name"].is_null() ? "" : row["zone_name"].as<std::string>();
            z.mobDefId   = row["mob_def_id"].as<std::string>();
            z.centerX    = row["center_x"].as<float>();
            z.centerY    = row["center_y"].as<float>();
            z.radius     = row["radius"].as<float>();
            z.targetCount = row["target_count"].is_null() ? 3 : row["target_count"].as<int>();
            z.respawnOverrideSeconds = row["respawn_override_seconds"].is_null()
                ? -1 : row["respawn_override_seconds"].as<int>();
            zonesByScene_[z.sceneId].push_back(std::move(z));
        }

        int total = 0;
        for (auto& [scene, zones] : zonesByScene_) total += (int)zones.size();
        LOG_INFO("SpawnZoneCache", "Loaded %d spawn zone rules across %d scenes",
                 total, (int)zonesByScene_.size());
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SpawnZoneCache", "Failed to load: %s", e.what());
        return false;
    }
}

const std::vector<SpawnZoneRow>& SpawnZoneCache::getZonesForScene(const std::string& sceneId) const {
    auto it = zonesByScene_.find(sceneId);
    return it != zonesByScene_.end() ? it->second : empty_;
}

int SpawnZoneCache::count() const {
    int total = 0;
    for (auto& [s, zones] : zonesByScene_) total += (int)zones.size();
    return total;
}

} // namespace fate
```

- [ ] **Step 3: Build and test**

- [ ] **Step 4: Commit**
```bash
git add server/db/spawn_zone_cache.h server/db/spawn_zone_cache.cpp
git commit -m "feat: add SpawnZoneCache to load spawn_zones from DB"
```

---

## Task 3: ServerSpawnManager — Mob lifecycle

**Files:**
- Create: `server/server_spawn_manager.h`
- Create: `server/server_spawn_manager.cpp`

- [ ] **Step 1: Create ServerSpawnManager header**

Create `server/server_spawn_manager.h`:
```cpp
#pragma once
#include "engine/ecs/world.h"
#include "engine/ecs/entity_handle.h"
#include "engine/net/replication.h"
#include "server/db/spawn_zone_cache.h"
#include "server/db/definition_caches.h"
#include <vector>
#include <unordered_map>

namespace fate {

class ServerSpawnManager {
public:
    // Initialize: create mob entities for the given scene
    void initialize(const std::string& sceneId,
                    World& world,
                    ReplicationManager& replication,
                    const SpawnZoneCache& spawnZoneCache,
                    const MobDefCache& mobDefCache);

    // Tick: check for dead mobs, process respawn timers
    void tick(float dt, float gameTime, World& world, ReplicationManager& replication);

    // Called when a mob entity dies (from MobAISystem combat callback)
    void onMobDeath(EntityHandle handle, float gameTime);

    int totalMobs() const { return totalMobs_; }

private:
    struct TrackedMob {
        EntityHandle handle;
        int zoneRowIndex = -1;   // index into zoneRows_
        bool alive = true;
        float respawnAt = 0.0f;  // game time when respawn should occur
    };

    struct ZoneRowState {
        SpawnZoneRow config;
        const CachedMobDef* def = nullptr;
        int respawnSeconds = 30;
    };

    std::vector<ZoneRowState> zoneRows_;
    std::vector<TrackedMob> mobs_;
    int totalMobs_ = 0;

    EntityHandle createMob(World& world, ReplicationManager& replication,
                           int zoneRowIndex, float gameTime);
    Vec2 randomPositionInZone(const SpawnZoneRow& zone);
};

} // namespace fate
```

- [ ] **Step 2: Create ServerSpawnManager implementation**

Create `server/server_spawn_manager.cpp`:
```cpp
#include "server/server_spawn_manager.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"
#include "engine/ecs/persistent_id.h"
#include "engine/core/logger.h"
#include "game/shared/mob_ai.h"
#include <random>

namespace fate {

static thread_local std::mt19937 s_rng{std::random_device{}()};

void ServerSpawnManager::initialize(
    const std::string& sceneId,
    World& world,
    ReplicationManager& replication,
    const SpawnZoneCache& spawnZoneCache,
    const MobDefCache& mobDefCache)
{
    zoneRows_.clear();
    mobs_.clear();
    totalMobs_ = 0;

    const auto& zones = spawnZoneCache.getZonesForScene(sceneId);
    for (const auto& zone : zones) {
        const CachedMobDef* def = mobDefCache.get(zone.mobDefId);
        if (!def) {
            LOG_WARN("SpawnManager", "Unknown mob_def_id '%s' in zone '%s'",
                     zone.mobDefId.c_str(), zone.zoneName.c_str());
            continue;
        }

        int idx = (int)zoneRows_.size();
        ZoneRowState state;
        state.config = zone;
        state.def = def;
        state.respawnSeconds = (zone.respawnOverrideSeconds >= 0)
            ? zone.respawnOverrideSeconds : def->respawnSeconds;
        zoneRows_.push_back(std::move(state));

        // Spawn target_count mobs for this row
        for (int i = 0; i < zone.targetCount; ++i) {
            createMob(world, replication, idx, 0.0f);
        }
    }

    LOG_INFO("SpawnManager", "Spawned %d mobs across %d zone rules for scene '%s'",
             totalMobs_, (int)zoneRows_.size(), sceneId.c_str());
}

EntityHandle ServerSpawnManager::createMob(
    World& world, ReplicationManager& replication,
    int zoneRowIndex, float gameTime)
{
    auto& row = zoneRows_[zoneRowIndex];
    const auto* def = row.def;

    // Random level within spawn range
    std::uniform_int_distribution<int> levelDist(def->minSpawnLevel, def->maxSpawnLevel);
    int level = levelDist(s_rng);

    // Random position within zone
    Vec2 pos = randomPositionInZone(row.config);

    Entity* mob = world.createEntity(def->displayName);
    mob->setTag("mob");

    auto* t = mob->addComponent<Transform>(pos);
    t->depth = 1.0f;

    // Enemy stats
    auto* esComp = mob->addComponent<EnemyStatsComponent>();
    auto& es = esComp->stats;
    es.enemyId       = def->mobDefId;
    es.enemyName     = def->displayName;
    es.level         = level;
    es.baseDamage    = def->getDamageForLevel(level);
    es.maxHP         = def->getHPForLevel(level);
    es.currentHP     = es.maxHP;
    es.armor         = def->getArmorForLevel(level);
    es.magicResist   = def->magicResist;
    es.critRate      = def->critRate;
    es.attackSpeed   = def->attackSpeed;
    es.moveSpeed     = def->moveSpeed;
    es.mobHitRate     = def->mobHitRate;
    es.xpReward      = def->getXPRewardForLevel(level);
    es.dealsMagicDamage = def->dealsMagicDamage;
    es.isAggressive  = def->isAggressive;
    es.isBoss        = def->isBoss;
    es.monsterType   = def->monsterType;
    es.lootTableId   = def->lootTableId;
    es.minGoldDrop   = def->minGoldDrop;
    es.maxGoldDrop   = def->maxGoldDrop;
    es.goldDropChance = def->goldDropChance;
    es.honorReward   = def->honorReward;
    es.isAlive       = true;

    // Mob AI
    auto* aiComp = mob->addComponent<MobAIComponent>();
    aiComp->ai.acquireRadius = def->aggroRange;
    aiComp->ai.attackRange   = def->attackRange;
    aiComp->ai.contactRadius = def->leashRadius;
    aiComp->ai.homePosition  = pos;
    aiComp->ai.moveSpeed     = def->moveSpeed * 32.0f; // tiles/sec to px/sec
    aiComp->ai.attackCooldown = def->attackSpeed;
    aiComp->ai.isPassive     = !def->isAggressive;

    // Mob nameplate (for replication buildEnterMessage)
    auto* np = mob->addComponent<MobNameplateComponent>();
    np->displayName = def->displayName;
    np->level       = level;
    np->isBoss      = def->isBoss;
    np->visible     = true;

    // Register with replication
    PersistentId pid = PersistentId::generate(1);
    replication.registerEntity(mob->handle(), pid);

    // Track
    TrackedMob tracked;
    tracked.handle = mob->handle();
    tracked.zoneRowIndex = zoneRowIndex;
    tracked.alive = true;
    mobs_.push_back(tracked);
    totalMobs_++;

    return mob->handle();
}

void ServerSpawnManager::tick(float dt, float gameTime, World& world, ReplicationManager& replication) {
    // Check for dead mobs and process respawn timers
    for (auto& mob : mobs_) {
        if (mob.alive) {
            // Check if mob died (HP <= 0)
            Entity* e = world.getEntity(mob.handle);
            if (!e) { mob.alive = false; continue; }
            auto* es = e->getComponent<EnemyStatsComponent>();
            if (es && !es->stats.isAlive && mob.alive) {
                mob.alive = false;
                mob.respawnAt = gameTime + (float)zoneRows_[mob.zoneRowIndex].respawnSeconds;
                // Entity will be cleaned up by replication (SvEntityLeave)
                replication.unregisterEntity(mob.handle);
                world.destroyEntity(mob.handle);
            }
        } else {
            // Dead — check respawn timer
            if (gameTime >= mob.respawnAt) {
                mob.handle = createMob(world, replication, mob.zoneRowIndex, gameTime);
                mob.alive = true;
                // Note: createMob already pushed a new TrackedMob — remove the duplicate
                // Actually, we're reusing this slot, so remove the duplicate:
                mobs_.pop_back(); // remove the one createMob pushed
                totalMobs_--; // createMob incremented, we don't want double count
            }
        }
    }

    world.processDestroyQueue();
}

Vec2 ServerSpawnManager::randomPositionInZone(const SpawnZoneRow& zone) {
    std::uniform_real_distribution<float> xDist(zone.centerX - zone.radius, zone.centerX + zone.radius);
    std::uniform_real_distribution<float> yDist(zone.centerY - zone.radius, zone.centerY + zone.radius);
    return {xDist(s_rng), yDist(s_rng)};
}

} // namespace fate
```

- [ ] **Step 3: Build**

- [ ] **Step 4: Commit**
```bash
git add server/server_spawn_manager.h server/server_spawn_manager.cpp
git commit -m "feat: add ServerSpawnManager for server-side mob lifecycle"
```

---

## Task 4: Wire ServerApp — Init caches, spawn mobs, add MobAISystem

**Files:**
- Modify: `server/server_app.h`
- Modify: `server/server_app.cpp`

- [ ] **Step 1: Add members to ServerApp**

In `server/server_app.h`, add includes:
```cpp
#include "server/db/spawn_zone_cache.h"
#include "server/server_spawn_manager.h"
```

Add members after `GauntletManager gauntletManager_` (around line 99):
```cpp
    SpawnZoneCache spawnZoneCache_;
    ServerSpawnManager spawnManager_;
```

- [ ] **Step 2: Initialize in ServerApp::init()**

In `server/server_app.cpp`, after `sceneCache_` initialization (around line 109), add:
```cpp
    if (!spawnZoneCache_.initialize(gameDbConn_.connection())) {
        LOG_WARN("Server", "Failed to load spawn zones — no server-side mobs");
    }
```

Replace the TODO comment about MobAISystem (around line 51) with:
```cpp
    world_.addSystem<MobAISystem>();
```

- [ ] **Step 3: Spawn mobs in ServerApp::run() before tick loop**

In `run()`, after the `onMobAttackResolved` callback wiring, before the tick loop, add:
```cpp
    // Spawn server-side mobs from DB spawn zones
    // Use "WhisperingWoods" as default scene (TODO: multi-scene support)
    spawnManager_.initialize("WhisperingWoods", world_, replication_, spawnZoneCache_, mobDefCache_);
```

- [ ] **Step 4: Tick spawns in ServerApp::tick()**

In `tick()`, after `world_.update(dt)` (around line 238), add:
```cpp
    spawnManager_.tick(dt, gameTime_, world_, replication_);
```

- [ ] **Step 5: Build and test**

- [ ] **Step 6: Commit**
```bash
git add server/server_app.h server/server_app.cpp
git commit -m "feat: wire SpawnZoneCache + ServerSpawnManager + MobAISystem in ServerApp"
```

---

## Task 5: Replication — Populate mob fields in buildEnterMessage

**Files:**
- Modify: `engine/net/replication.cpp:239-248`

- [ ] **Step 1: Enhance mob section of buildEnterMessage**

In `engine/net/replication.cpp`, find the EnemyStatsComponent block in `buildEnterMessage()` (around lines 239-248). Add mobDefId and isBoss population:

After setting entityType, name, level, HP from EnemyStatsComponent, add:
```cpp
    msg.mobDefId = es->stats.enemyId;  // enemyId IS the mob def ID
    msg.isBoss   = es->stats.isBoss ? 1 : 0;
```

- [ ] **Step 2: Build**

- [ ] **Step 3: Commit**
```bash
git add engine/net/replication.cpp
git commit -m "feat: populate mobDefId + isBoss in replication buildEnterMessage"
```

---

## Task 6: Client — Enhanced createGhostMob + entity handlers

**Files:**
- Modify: `game/entity_factory.h:763-775`
- Modify: `game/game_app.cpp` (onEntityEnter, onEntityUpdate)

- [ ] **Step 1: Enhance createGhostMob**

In `game/entity_factory.h`, replace the existing `createGhostMob` (around line 763) with a version that accepts the full message:

```cpp
    static Entity* createGhostMob(World& world, const std::string& name, Vec2 position,
                                   const std::string& mobDefId = "", int level = 1,
                                   int currentHP = 100, int maxHP = 100,
                                   bool isBoss = false) {
        Entity* entity = world.createEntity(name);
        entity->setTag("mob");
        auto* t = entity->addComponent<Transform>(position);
        t->depth = 1.0f;

        auto* sprite = entity->addComponent<SpriteComponent>();
        if (!mobDefId.empty()) {
            std::string path = "assets/sprites/mob_" + mobDefId + ".png";
            sprite->texture = TextureCache::instance().load(path);
        }
        sprite->size = {32.0f, 32.0f};

        auto* es = entity->addComponent<EnemyStatsComponent>();
        es->stats.enemyName = name;
        es->stats.level = level;
        es->stats.currentHP = currentHP;
        es->stats.maxHP = maxHP;
        es->stats.isAlive = currentHP > 0;
        es->stats.isBoss = isBoss;

        auto* np = entity->addComponent<MobNameplateComponent>();
        np->displayName = name;
        np->level = level;
        np->isBoss = isBoss;
        np->visible = true;

        return entity;
    }
```

- [ ] **Step 2: Update onEntityEnter handler**

In `game/game_app.cpp`, update the mob branch of `onEntityEnter` (around line 804-805):

```cpp
        } else if (msg.entityType == 1) { // mob
            ghost = EntityFactory::createGhostMob(world, msg.name, msg.position,
                msg.mobDefId, msg.level, msg.currentHP, msg.maxHP, msg.isBoss != 0);
        } else if (msg.entityType == 2) { // npc
            ghost = EntityFactory::createGhostMob(world, msg.name, msg.position);
        }
```

- [ ] **Step 3: Update onEntityUpdate to sync mob HP**

In the `onEntityUpdate` handler, after position interpolation, add HP sync:

```cpp
        // Update mob HP from server
        if (msg.fieldMask & 0x08) { // bit 3 = currentHP
            auto* sc = SceneManager::instance().currentScene();
            if (sc) {
                Entity* ghost = sc->world().getEntity(it->second);
                if (ghost) {
                    auto* es = ghost->getComponent<EnemyStatsComponent>();
                    if (es) {
                        es->stats.currentHP = msg.currentHP;
                        es->stats.isAlive = msg.currentHP > 0;
                    }
                }
            }
        }
```

- [ ] **Step 4: Disable client SpawnSystem when connected**

In `game_app.cpp`, where the connection state transitions to `InGame`, disable the local SpawnSystem:
```cpp
    // Disable local mob spawning — server replicates mobs
    auto* spawnSys = scene->world().getSystem<SpawnSystem>();
    if (spawnSys) spawnSys->enabled = false;
```

- [ ] **Step 5: Build and test**

- [ ] **Step 6: Commit**
```bash
git add game/entity_factory.h game/game_app.cpp
git commit -m "feat: client renders server-replicated mobs with full visuals"
```

---

## Task 7: Final Integration + Test

- [ ] **Step 1: Run full test suite**
Expected: All existing tests pass + new protocol tests.

- [ ] **Step 2: Manual test**
1. Start server (loads SpawnZoneCache, creates 12 mobs)
2. Start client, connect
3. Walk around — see server-replicated mobs with sprites, nameplates, HP bars
4. Attack mob — floating text, mob HP decreases
5. Kill mob — mob disappears (SvEntityLeave), respawns after timer
6. Mob attacks player — floating text on player, HP decreases
7. Player dies — death overlay with respawn options

- [ ] **Step 3: Commit integration**
```bash
git add -A
git commit -m "feat: server-side mob spawning complete — DB spawn zones, replication, combat"
```

---

## Summary

| Task | What | Key Files |
|------|------|-----------|
| 1 | Protocol + EnemyStats extension | protocol.h, enemy_stats.h |
| 2 | SpawnZoneCache (DB loader) | spawn_zone_cache.h/.cpp |
| 3 | ServerSpawnManager (mob lifecycle) | server_spawn_manager.h/.cpp |
| 4 | ServerApp wiring | server_app.h/.cpp |
| 5 | Replication mob fields | replication.cpp |
| 6 | Client mob rendering | entity_factory.h, game_app.cpp |
| 7 | Integration test | Manual verification |
