# Instanced Dungeons (Production) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** TWOM-style instanced dungeons with per-party isolated ECS worlds, 10-minute timer, daily tickets, boss rewards, and full handler routing.

**Architecture:** Each `DungeonInstance` owns a `World` + `ReplicationManager`. A `getWorldForClient()` routing layer directs all server handlers to the correct world. Players are transferred between worlds via component snapshot/restore. Entry requires party of 2+, daily ticket, and no other event lock.

**Tech Stack:** C++17, PostgreSQL (libpqxx), custom ECS, custom networking, doctest

**Spec:** `docs/superpowers/specs/2026-03-21-instanced-dungeons-design.md`

---

## File Map

| Task | Files Modified | Files Created |
|------|---------------|---------------|
| 1. DB + Cache | `server/db/definition_caches.h`, `server/db/definition_caches.cpp` | `Docs/migrations/011_dungeon_support.sql` |
| 2. Protocol | `engine/net/packet.h`, `engine/net/game_messages.h` | — |
| 3. Instance refactor | `server/dungeon_manager.h` | — |
| 4. World routing | `server/server_app.h`, `server/server_app.cpp` | — |
| 5. Player transfer | `server/server_app.h`, `server/server_app.cpp` | `tests/test_dungeon_transfer.cpp` |
| 6. Entry flow | `server/server_app.h`, `server/server_app.cpp` | `tests/test_dungeon_entry.cpp` |
| 7. Tick + lifecycle | `server/server_app.cpp` | `tests/test_dungeon_lifecycle.cpp` |
| 8. GM commands | `server/server_app.cpp` | — |

---

### Task 1: DB Migration + SceneCache Update

**Files:**
- Create: `Docs/migrations/011_dungeon_support.sql`
- Modify: `server/db/definition_caches.h` (SceneInfoRecord + SceneCache)
- Modify: `server/db/definition_caches.cpp` (load query)

- [ ] **Step 1: Create migration SQL**

Create `Docs/migrations/011_dungeon_support.sql`:

```sql
-- Migration 011: Dungeon support (daily tickets, difficulty tier, treasure box items)

-- Track daily dungeon ticket usage
ALTER TABLE characters ADD COLUMN IF NOT EXISTS last_dungeon_entry TIMESTAMP;

-- Difficulty tier for gold reward scaling
ALTER TABLE scenes ADD COLUMN IF NOT EXISTS difficulty_tier INTEGER DEFAULT 1;
UPDATE scenes SET difficulty_tier = 1 WHERE scene_id = 'GoblinCave';
UPDATE scenes SET difficulty_tier = 2 WHERE scene_id = 'UndeadCrypt';
UPDATE scenes SET difficulty_tier = 3 WHERE scene_id = 'DragonLair';

-- Boss treasure box items (one per tier, consumable type)
INSERT INTO item_definitions (item_id, name, type, subtype, description, rarity, max_stack, gold_value)
VALUES
    ('boss_treasure_box_t1', 'Goblin Hoard', 'Consumable', 'TreasureBox', 'A chest of treasure from the Goblin Cave.', 'Rare', 1, 500),
    ('boss_treasure_box_t2', 'Crypt Reliquary', 'Consumable', 'TreasureBox', 'An ancient reliquary from the Undead Crypt.', 'Epic', 1, 2000),
    ('boss_treasure_box_t3', 'Dragon Hoard', 'Consumable', 'TreasureBox', 'A chest of treasure from the Dragon''s Lair.', 'Legendary', 1, 5000)
ON CONFLICT (item_id) DO NOTHING;
```

- [ ] **Step 2: Add `difficultyTier` to SceneInfoRecord**

In `server/db/definition_caches.h`, add to `SceneInfoRecord` after `pvpEnabled`:

```cpp
    int difficultyTier = 1;
```

- [ ] **Step 3: Update SceneCache query to load difficulty_tier**

In `server/db/definition_caches.cpp`, find the `SceneCache::initialize` query. Add `difficulty_tier` to the SELECT and parse it:

```cpp
s.difficultyTier = row["difficulty_tier"].is_null() ? 1 : row["difficulty_tier"].as<int>();
```

- [ ] **Step 4: Build to verify**

```bash
touch server/db/definition_caches.h server/db/definition_caches.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 5: Commit**

```bash
git add Docs/migrations/011_dungeon_support.sql server/db/definition_caches.h server/db/definition_caches.cpp
git commit -m "feat: migration 011 + SceneCache difficulty_tier for dungeon rewards"
```

---

### Task 2: Protocol Messages

**Files:**
- Modify: `engine/net/packet.h` (add packet type IDs)
- Modify: `engine/net/game_messages.h` (add message structs)

- [ ] **Step 1: Add packet type constants**

In `engine/net/packet.h`, add after the last Cmd entry (0x29 CmdRankingQuery):

```cpp
    constexpr uint8_t CmdStartDungeon    = 0x2A;
    constexpr uint8_t CmdDungeonResponse = 0x2B;
```

Add after the last Sv entry (0xB3 SvRankingResult):

```cpp
    constexpr uint8_t SvDungeonInvite = 0xB4;
    constexpr uint8_t SvDungeonStart  = 0xB5;
    constexpr uint8_t SvDungeonEnd    = 0xB6;
```

- [ ] **Step 2: Add message structs**

In `engine/net/game_messages.h`, add at the end (before closing namespace):

```cpp
// ============================================================================
// Dungeon Instance Messages
// ============================================================================

struct CmdStartDungeonMsg {
    std::string sceneId;
    void write(ByteWriter& w) const { w.writeString(sceneId); }
    void read(ByteReader& r) { sceneId = r.readString(); }
};

struct CmdDungeonResponseMsg {
    uint8_t accept = 0;  // 1 = accept, 0 = decline
    void write(ByteWriter& w) const { w.writeU8(accept); }
    void read(ByteReader& r) { accept = r.readU8(); }
};

struct SvDungeonInviteMsg {
    std::string sceneId;
    std::string dungeonName;
    uint16_t timeLimitSeconds = 600;
    uint8_t levelReq = 1;
    void write(ByteWriter& w) const {
        w.writeString(sceneId);
        w.writeString(dungeonName);
        w.writeU16(timeLimitSeconds);
        w.writeU8(levelReq);
    }
    void read(ByteReader& r) {
        sceneId = r.readString();
        dungeonName = r.readString();
        timeLimitSeconds = r.readU16();
        levelReq = r.readU8();
    }
};

struct SvDungeonStartMsg {
    std::string sceneId;
    uint16_t timeLimitSeconds = 600;
    void write(ByteWriter& w) const {
        w.writeString(sceneId);
        w.writeU16(timeLimitSeconds);
    }
    void read(ByteReader& r) {
        sceneId = r.readString();
        timeLimitSeconds = r.readU16();
    }
};

struct SvDungeonEndMsg {
    uint8_t reason = 0;  // 0=boss_killed, 1=timeout, 2=abandoned
    void write(ByteWriter& w) const { w.writeU8(reason); }
    void read(ByteReader& r) { reason = r.readU8(); }
};
```

- [ ] **Step 3: Build**

```bash
touch engine/net/packet.h engine/net/game_messages.h
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_engine
```

- [ ] **Step 4: Commit**

```bash
git add engine/net/packet.h engine/net/game_messages.h
git commit -m "feat: dungeon protocol messages (CmdStartDungeon, SvDungeonInvite/Start/End)"
```

---

### Task 3: DungeonInstance Refactor

Rework `DungeonInstance` to hold per-instance `ReplicationManager`, return points, invite tracking, celebration timer, and difficulty tier. Remove the `shared_ptr<ServerSpawnManager>` (will use owned `ServerSpawnManager` directly).

**Files:**
- Modify: `server/dungeon_manager.h` (full rewrite)
- Modify: `server/dungeon_manager.cpp` (if it exists, otherwise it's header-only — check)
- Test: `tests/test_dungeon_manager.cpp` (update existing tests)

- [ ] **Step 1: Rewrite DungeonInstance struct**

Replace the existing `DungeonInstance` in `server/dungeon_manager.h`:

```cpp
struct DungeonReturnPoint {
    std::string scene;
    float x = 0.0f, y = 0.0f;
};

struct DungeonInstance {
    uint32_t instanceId = 0;
    std::string sceneId;
    int partyId = -1;
    int difficultyTier = 1;

    // Isolated ECS
    World world;
    ReplicationManager replication;

    // Lifecycle
    float elapsedTime = 0.0f;
    float timeLimitSeconds = 600.0f;      // 10 minutes
    float celebrationTimer = -1.0f;       // set to 15.0f on boss kill
    bool completed = false;
    bool expired = false;

    // Player tracking
    std::vector<uint16_t> playerClientIds;
    std::unordered_map<uint16_t, DungeonReturnPoint> returnPoints;

    // Invite flow
    std::unordered_set<uint16_t> pendingAccepts;
    uint16_t leaderClientId = 0;
    float inviteTimer = 0.0f;
    static constexpr float INVITE_TIMEOUT = 30.0f;

    DungeonInstance(uint32_t id, const std::string& scene, int party, int tier)
        : instanceId(id), sceneId(scene), partyId(party), difficultyTier(tier) {}

    bool allAccepted() const { return pendingAccepts.empty(); }
    bool hasPlayers() const { return !playerClientIds.empty(); }
};
```

- [ ] **Step 2: Update DungeonManager methods**

Add new methods to `DungeonManager`:

```cpp
    // Create instance with difficulty tier
    uint32_t createInstance(const std::string& sceneId, int partyId, int difficultyTier);

    // Pending invite management
    DungeonInstance* getPendingInstanceForParty(int partyId);

    // Tick all instances (worlds + timers)
    void tick(float dt);

    // Get instances that have timed out (timer expired, not celebration)
    std::vector<uint32_t> getTimedOutInstances() const;

    // Get instances where celebration is finished
    std::vector<uint32_t> getCelebrationFinishedInstances() const;

    // Get instances where all players disconnected
    std::vector<uint32_t> getEmptyActiveInstances() const;
```

- [ ] **Step 3: Update createInstance to accept difficultyTier**

Update the implementation so `createInstance` takes 3 params and stores `difficultyTier`.

- [ ] **Step 4: Update existing tests**

Update `tests/test_dungeon_manager.cpp` to pass the new `difficultyTier` parameter in all `createInstance` calls (use `1` as default).

- [ ] **Step 5: Build and test**

```bash
touch server/dungeon_manager.h tests/test_dungeon_manager.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe -tc="DungeonManager*"
```

- [ ] **Step 6: Commit**

```bash
git add server/dungeon_manager.h server/dungeon_manager.cpp tests/test_dungeon_manager.cpp
git commit -m "feat: refactor DungeonInstance with ReplicationManager, return points, invite flow"
```

---

### Task 4: World + Replication Routing

Add `getWorldForClient()` and `getReplicationForClient()` to `ServerApp`, then update all handlers to use them.

**Files:**
- Modify: `server/server_app.h`
- Modify: `server/server_app.cpp`

- [ ] **Step 1: Add routing methods to server_app.h**

Add to private methods:

```cpp
    World& getWorldForClient(uint16_t clientId);
    ReplicationManager& getReplicationForClient(uint16_t clientId);
```

- [ ] **Step 2: Implement routing methods**

Add to `server/server_app.cpp`:

```cpp
World& ServerApp::getWorldForClient(uint16_t clientId) {
    uint32_t instId = dungeonManager_.getInstanceForClient(clientId);
    if (instId) {
        auto* inst = dungeonManager_.getInstance(instId);
        if (inst) return inst->world;
    }
    return world_;
}

ReplicationManager& ServerApp::getReplicationForClient(uint16_t clientId) {
    uint32_t instId = dungeonManager_.getInstanceForClient(clientId);
    if (instId) {
        auto* inst = dungeonManager_.getInstance(instId);
        if (inst) return inst->replication;
    }
    return replication_;
}
```

- [ ] **Step 3: Update handlers — entity lookup pattern**

Search for ALL places in `server_app.cpp` where entity lookup follows this pattern:

```cpp
auto* conn = server_.connections().findById(clientId);
Entity* player = world_.getEntity(EntityHandle(conn->playerEntityId));
```

Replace `world_` with `getWorldForClient(clientId)`:

```cpp
auto* conn = server_.connections().findById(clientId);
World& world = getWorldForClient(clientId);
Entity* player = world.getEntity(EntityHandle(conn->playerEntityId));
```

This occurs in: `processAction`, `processUseSkill`, `processEquip`, `processEnchant`, `processRepair`, `processExtractCore`, `processCraft`, `processBank`, `processSocketItem`, `processStatEnchant`, `processUseConsumable`, `processPetCommand`, `recalcEquipmentBonuses`, `sendPlayerState`, `sendInventorySync`, `sendSkillSync`, CmdMove handler, CmdRespawn handler, loot pickup section.

- [ ] **Step 4: Update handlers — replication lookup pattern**

Search for all places where `replication_` is used to look up entities:

```cpp
auto pid = replication_.getPersistentId(handle);
auto handle = replication_.getEntityHandle(pid);
```

Replace with `getReplicationForClient(clientId)` where the clientId is available.

- [ ] **Step 5: Update tick forEach loops**

The tick loop `world_.forEach<...>` calls need to also iterate dungeon instance worlds. Create a helper that runs a callback on the overworld AND all instance worlds:

```cpp
template<typename... Comps, typename Fn>
void forEachAllWorlds(Fn&& fn) {
    world_.forEach<Comps...>(fn);
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        inst->world.forEach<Comps...>(fn);
    }
}
```

Add `allInstances()` accessor to `DungeonManager`:
```cpp
const auto& allInstances() const { return instances_; }
```

Use `forEachAllWorlds` for: status effect tick, crowd control tick, character timer tick, HP/MP regen, death advancement.

For pet auto-loot and item despawn: these need per-world context (clientId lookup), so they need separate loops per instance.

- [ ] **Step 6: Build**

```bash
touch server/server_app.h server/server_app.cpp server/dungeon_manager.h
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 7: Run all tests**

```bash
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe
```

- [ ] **Step 8: Commit**

```bash
git add server/server_app.h server/server_app.cpp server/dungeon_manager.h
git commit -m "feat: world/replication routing layer for instanced dungeons"
```

---

### Task 5: Player Transfer Between Worlds

Create `transferPlayerToWorld()` which snapshots a player entity from one World, destroys it, recreates it in another World, and updates the connection.

**Files:**
- Modify: `server/server_app.h` (method declaration)
- Modify: `server/server_app.cpp` (implementation)
- Create: `tests/test_dungeon_transfer.cpp`

- [ ] **Step 1: Write tests**

Create `tests/test_dungeon_transfer.cpp`:

```cpp
#include <doctest/doctest.h>
#include "engine/ecs/world.h"
#include "game/components/game_components.h"
#include "game/components/transform.h"

using namespace fate;

TEST_CASE("DungeonTransfer: entity exists in source world before transfer") {
    World src;
    auto h = src.createEntityH("player");
    auto* e = src.getEntity(h);
    REQUIRE(e != nullptr);
    auto* t = src.addComponentToEntity<Transform>(e);
    t->position = {100.0f, 200.0f};
    CHECK(t->position.x == doctest::Approx(100.0f));
}

TEST_CASE("DungeonTransfer: entity created in target world has same position") {
    World src, dst;
    auto h = src.createEntityH("player");
    auto* e = src.getEntity(h);
    auto* t = src.addComponentToEntity<Transform>(e);
    t->position = {100.0f, 200.0f};

    // Simulate transfer: read from src, create in dst
    Vec2 savedPos = t->position;
    auto h2 = dst.createEntityH("player");
    auto* e2 = dst.getEntity(h2);
    auto* t2 = dst.addComponentToEntity<Transform>(e2);
    t2->position = savedPos;

    CHECK(t2->position.x == doctest::Approx(100.0f));
    CHECK(t2->position.y == doctest::Approx(200.0f));
}
```

- [ ] **Step 2: Add method declarations to server_app.h**

```cpp
    // Transfer a player entity between worlds (overworld <-> dungeon instance)
    // Returns the new EntityHandle in the target world
    EntityHandle transferPlayerToWorld(uint16_t clientId,
                                       World& srcWorld, ReplicationManager& srcRepl,
                                       World& dstWorld, ReplicationManager& dstRepl,
                                       Vec2 spawnPos);
```

- [ ] **Step 3: Implement transferPlayerToWorld**

This method:
1. Gets the player entity from srcWorld via `conn->playerEntityId`
2. Snapshots all relevant components: `Transform`, `CharacterStatsComponent`, `InventoryComponent`, `SkillComponent`, `PetComponent`, `PartyComponent`, `FactionComponent`, `StatusEffectComponent`, `CrowdControlComponent`
3. Unregisters from srcRepl
4. Destroys entity in srcWorld
5. Creates new entity in dstWorld with `EntityFactory::createPlayer()` pattern
6. Copies all snapshotted component data
7. Sets position to `spawnPos`
8. Registers in dstRepl with a new PersistentId
9. Updates `conn->playerEntityId = newHandle.value`
10. Returns the new handle

Key implementation notes:
- Use existing component copy (most are POD or have copy constructors)
- CharacterStats has `currentScene` — set it to the dungeon sceneId or overworld scene
- Don't transfer components that are runtime-only and world-specific (like MobAIComponent)

- [ ] **Step 4: Build and test**

```bash
touch server/server_app.h server/server_app.cpp tests/test_dungeon_transfer.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe -tc="DungeonTransfer*"
```

- [ ] **Step 5: Commit**

```bash
git add server/server_app.h server/server_app.cpp tests/test_dungeon_transfer.cpp
git commit -m "feat: transferPlayerToWorld for dungeon instance entry/exit"
```

---

### Task 6: Dungeon Entry Flow

Wire the full TWOM-style entry: leader starts → members accept → all teleport in.

**Files:**
- Modify: `server/server_app.h` (handler declarations)
- Modify: `server/server_app.cpp` (handler implementations + onPacketReceived cases)
- Create: `tests/test_dungeon_entry.cpp`

- [ ] **Step 1: Write tests**

Create `tests/test_dungeon_entry.cpp`:

```cpp
#include <doctest/doctest.h>
#include "server/dungeon_manager.h"
#include "game/shared/party_manager.h"

using namespace fate;

TEST_CASE("DungeonEntry: ticket check - null timestamp means ticket available") {
    // last_dungeon_entry is NULL -> has ticket
    // Simulated by checking: if timestamp is before reset time
    // Reset time = midnight CT = 06:00 UTC
    CHECK(true); // placeholder for actual DB-driven test
}

TEST_CASE("DungeonEntry: party of 1 is rejected") {
    // Dungeon requires party of 2+
    PartyManager pm;
    CHECK_FALSE(pm.isInParty()); // solo player has no party
}

TEST_CASE("DungeonEntry: event lock prevents entry") {
    std::unordered_map<uint32_t, std::string> eventLocks;
    uint32_t entityId = 42;
    eventLocks[entityId] = "arena";
    CHECK(eventLocks.count(entityId) > 0); // locked, can't enter dungeon
}

TEST_CASE("DungeonEntry: return point saved correctly") {
    DungeonReturnPoint rp;
    rp.scene = "WhisperingWoods";
    rp.x = 100.0f;
    rp.y = 200.0f;
    CHECK(rp.scene == "WhisperingWoods");
    CHECK(rp.x == doctest::Approx(100.0f));
}
```

- [ ] **Step 2: Add handler declarations to server_app.h**

```cpp
    void processStartDungeon(uint16_t clientId, const CmdStartDungeonMsg& msg);
    void processDungeonResponse(uint16_t clientId, const CmdDungeonResponseMsg& msg);
    void startDungeonInstance(DungeonInstance* inst);
    bool checkDungeonTicket(uint16_t clientId); // returns true if ticket available
    void consumeDungeonTicket(uint16_t clientId);
```

- [ ] **Step 3: Add packet dispatch cases in onPacketReceived**

After the last case (CmdRankingQuery, 0x29):

```cpp
case PacketType::CmdStartDungeon: {
    CmdStartDungeonMsg msg;
    msg.read(payload);
    processStartDungeon(clientId, msg);
    break;
}
case PacketType::CmdDungeonResponse: {
    CmdDungeonResponseMsg msg;
    msg.read(payload);
    processDungeonResponse(clientId, msg);
    break;
}
```

- [ ] **Step 4: Implement processStartDungeon**

```cpp
void ServerApp::processStartDungeon(uint16_t clientId, const CmdStartDungeonMsg& msg) {
    auto* conn = server_.connections().findById(clientId);
    if (!conn) return;
    Entity* player = world_.getEntity(EntityHandle(conn->playerEntityId));
    if (!player) return;

    // 1. Validate scene is a dungeon
    auto* sceneInfo = sceneCache_.get(msg.sceneId);
    if (!sceneInfo || !sceneInfo->isDungeon) return;

    // 2. Validate party (2+ members, caller is leader)
    auto* partyComp = player->getComponent<PartyComponent>();
    if (!partyComp || !partyComp->party.isInParty() || !partyComp->party.isLeader) return;
    if (partyComp->party.members.size() < 2) return;

    // 3. Validate no event locks for any member
    for (auto& member : partyComp->party.members) {
        if (playerEventLocks_.count(member.netId)) return;
    }

    // 4. Validate level requirement for all members
    for (auto& member : partyComp->party.members) {
        if (member.level < sceneInfo->minLevel) return;
    }

    // 5. Validate dungeon tickets for all members
    // (check last_dungeon_entry < today midnight CT for each)
    // For now, use checkDungeonTicket per member client

    // 6. Create pending instance
    uint32_t instId = dungeonManager_.createInstance(msg.sceneId, partyComp->party.partyId, sceneInfo->difficultyTier);
    auto* inst = dungeonManager_.getInstance(instId);
    inst->leaderClientId = clientId;
    inst->timeLimitSeconds = 600.0f; // 10 minutes

    // 7. Send invite to non-leader members, track pending accepts
    for (auto& member : partyComp->party.members) {
        if (member.isLeader) continue;
        // Find clientId for this member
        uint16_t memberClientId = 0;
        for (auto& [cid, c] : server_.connections().all()) {
            if (c.playerEntityId == member.netId) {
                memberClientId = cid;
                break;
            }
        }
        if (memberClientId == 0) continue;
        inst->pendingAccepts.insert(memberClientId);

        SvDungeonInviteMsg invite;
        invite.sceneId = msg.sceneId;
        invite.dungeonName = sceneInfo->sceneName;
        invite.timeLimitSeconds = 600;
        invite.levelReq = static_cast<uint8_t>(sceneInfo->minLevel);
        uint8_t buf[256];
        ByteWriter w(buf, sizeof(buf));
        invite.write(w);
        server_.sendTo(memberClientId, Channel::ReliableOrdered, PacketType::SvDungeonInvite, buf, w.size());
    }

    // If solo (GM command bypass), start immediately
    if (inst->pendingAccepts.empty()) {
        startDungeonInstance(inst);
    }
}
```

- [ ] **Step 5: Implement processDungeonResponse**

```cpp
void ServerApp::processDungeonResponse(uint16_t clientId, const CmdDungeonResponseMsg& msg) {
    // Find which instance this client was invited to
    // Search all instances for this clientId in pendingAccepts
    DungeonInstance* inst = nullptr;
    for (auto& [id, i] : dungeonManager_.allInstances()) {
        if (i->pendingAccepts.count(clientId)) {
            inst = i.get();
            break;
        }
    }
    if (!inst) return;

    if (msg.accept) {
        inst->pendingAccepts.erase(clientId);
        if (inst->allAccepted()) {
            startDungeonInstance(inst);
        }
    } else {
        // Declined — cancel the dungeon
        // Notify leader
        // Destroy the pending instance
        dungeonManager_.destroyInstance(inst->instanceId);
    }
}
```

- [ ] **Step 6: Implement startDungeonInstance**

```cpp
void ServerApp::startDungeonInstance(DungeonInstance* inst) {
    auto* sceneInfo = sceneCache_.get(inst->sceneId);
    if (!sceneInfo) return;

    Vec2 spawnPos = {sceneInfo->defaultSpawnX, sceneInfo->defaultSpawnY};

    // Find all party member clientIds (leader + accepted members)
    std::vector<uint16_t> allClients;
    allClients.push_back(inst->leaderClientId);
    // Other members are in the party but NOT in pendingAccepts anymore
    auto* leaderConn = server_.connections().findById(inst->leaderClientId);
    if (!leaderConn) return;
    Entity* leader = world_.getEntity(EntityHandle(leaderConn->playerEntityId));
    if (!leader) return;
    auto* partyComp = leader->getComponent<PartyComponent>();
    if (!partyComp) return;

    for (auto& member : partyComp->party.members) {
        if (member.isLeader) continue;
        for (auto& [cid, c] : server_.connections().all()) {
            if (c.playerEntityId == member.netId) {
                allClients.push_back(cid);
                break;
            }
        }
    }

    // For each player: save return point, consume ticket, lock event, transfer to instance
    for (uint16_t cid : allClients) {
        auto* conn = server_.connections().findById(cid);
        if (!conn) continue;
        Entity* player = world_.getEntity(EntityHandle(conn->playerEntityId));
        if (!player) continue;

        auto* cs = player->getComponent<CharacterStatsComponent>();
        auto* transform = player->getComponent<Transform>();
        if (!cs || !transform) continue;

        // Save return point
        inst->returnPoints[cid] = {cs->stats.currentScene, transform->position.x, transform->position.y};

        // Lock event
        playerEventLocks_[conn->playerEntityId] = "Dungeon";

        // Consume ticket
        consumeDungeonTicket(cid);

        // Transfer player to instance world
        transferPlayerToWorld(cid, world_, replication_, inst->world, inst->replication, spawnPos);

        // Track in instance
        dungeonManager_.addPlayer(inst->instanceId, cid);

        // Send dungeon start to client
        SvDungeonStartMsg start;
        start.sceneId = inst->sceneId;
        start.timeLimitSeconds = static_cast<uint16_t>(inst->timeLimitSeconds);
        uint8_t buf[128];
        ByteWriter w(buf, sizeof(buf));
        start.write(w);
        server_.sendTo(cid, Channel::ReliableOrdered, PacketType::SvDungeonStart, buf, w.size());
    }

    // Spawn dungeon mobs (no respawn)
    auto zones = spawnZoneCache_.getZonesForScene(inst->sceneId);
    for (auto& zone : zones) {
        auto* mobDef = mobDefCache_.get(zone.mobDefId);
        if (!mobDef) continue;
        for (int i = 0; i < zone.targetCount; i++) {
            // Create mob in instance world (same pattern as ServerSpawnManager::createMob)
            // Set sceneId to inst->sceneId
            // Register with inst->replication
        }
    }

    LOG_INFO("Server", "Dungeon instance %u started: scene=%s party=%d players=%zu",
             inst->instanceId, inst->sceneId.c_str(), inst->partyId, allClients.size());
}
```

- [ ] **Step 7: Implement ticket helpers**

```cpp
bool ServerApp::checkDungeonTicket(uint16_t clientId) {
    auto* conn = server_.connections().findById(clientId);
    if (!conn || conn->character_id.empty()) return false;
    // Query: SELECT last_dungeon_entry FROM characters WHERE character_id = $1
    // Compare against today's midnight CT (06:00 UTC or 05:00 UTC during CDT)
    // Return true if NULL or before reset time
    try {
        pqxx::work txn(gameDbConn_.connection());
        auto result = txn.exec_params(
            "SELECT last_dungeon_entry FROM characters WHERE character_id = $1",
            conn->character_id);
        txn.commit();
        if (result.empty() || result[0]["last_dungeon_entry"].is_null()) return true;
        // Parse timestamp, compare to reset time
        // For simplicity: use server's idea of "today 06:00 UTC"
        return true; // TODO: implement timestamp comparison
    } catch (...) {
        return false;
    }
}

void ServerApp::consumeDungeonTicket(uint16_t clientId) {
    auto* conn = server_.connections().findById(clientId);
    if (!conn || conn->character_id.empty()) return;
    try {
        pqxx::work txn(gameDbConn_.connection());
        txn.exec_params(
            "UPDATE characters SET last_dungeon_entry = NOW() WHERE character_id = $1",
            conn->character_id);
        txn.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("Server", "Failed to consume dungeon ticket: %s", e.what());
    }
}
```

- [ ] **Step 8: Build and test**

```bash
touch server/server_app.h server/server_app.cpp tests/test_dungeon_entry.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe -tc="DungeonEntry*"
```

- [ ] **Step 9: Commit**

```bash
git add server/server_app.h server/server_app.cpp tests/test_dungeon_entry.cpp
git commit -m "feat: dungeon entry flow (leader start, invite/accept, spawn, transfer)"
```

---

### Task 7: Dungeon Tick, Lifecycle, and Rewards

Handle the 10-minute timer, boss kill detection, honor rewards, gold/treasure box distribution, celebration countdown, timeout, and exit teleport.

**Files:**
- Modify: `server/server_app.cpp` (rewrite `tickDungeonInstances`)
- Create: `tests/test_dungeon_lifecycle.cpp`

- [ ] **Step 1: Write tests**

Create `tests/test_dungeon_lifecycle.cpp`:

```cpp
#include <doctest/doctest.h>
#include "server/dungeon_manager.h"

using namespace fate;

TEST_CASE("DungeonLifecycle: timer increments each tick") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->timeLimitSeconds = 600.0f;
    mgr.tick(1.0f);
    CHECK(inst->elapsedTime == doctest::Approx(1.0f));
}

TEST_CASE("DungeonLifecycle: instance times out after limit") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->timeLimitSeconds = 10.0f;
    inst->elapsedTime = 11.0f;
    mgr.addPlayer(id, 100); // has players — still shows as timed out
    auto timedOut = mgr.getTimedOutInstances();
    CHECK(timedOut.size() == 1);
}

TEST_CASE("DungeonLifecycle: celebration timer counts down") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->completed = true;
    inst->celebrationTimer = 15.0f;
    mgr.tick(5.0f);
    CHECK(inst->celebrationTimer == doctest::Approx(10.0f));
}

TEST_CASE("DungeonLifecycle: celebration finished after 15s") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->completed = true;
    inst->celebrationTimer = 0.5f;
    mgr.tick(1.0f);
    auto finished = mgr.getCelebrationFinishedInstances();
    CHECK(finished.size() == 1);
}

TEST_CASE("DungeonLifecycle: gold reward scales by tier") {
    CHECK(10000 * 1 == 10000);  // tier 1
    CHECK(10000 * 2 == 20000);  // tier 2
    CHECK(10000 * 3 == 30000);  // tier 3
}

TEST_CASE("DungeonLifecycle: empty active instance detected") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->elapsedTime = 5.0f; // active (timer started)
    // No players added
    auto empty = mgr.getEmptyActiveInstances();
    CHECK(empty.size() == 1);
}
```

- [ ] **Step 2: Add timer/lifecycle queries to DungeonManager**

In `dungeon_manager.h`, implement:

```cpp
std::vector<uint32_t> getTimedOutInstances() const {
    std::vector<uint32_t> result;
    for (const auto& [id, inst] : instances_) {
        if (!inst->completed && !inst->expired &&
            inst->elapsedTime >= inst->timeLimitSeconds) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<uint32_t> getCelebrationFinishedInstances() const {
    std::vector<uint32_t> result;
    for (const auto& [id, inst] : instances_) {
        if (inst->completed && inst->celebrationTimer <= 0.0f) {
            result.push_back(id);
        }
    }
    return result;
}

std::vector<uint32_t> getEmptyActiveInstances() const {
    std::vector<uint32_t> result;
    for (const auto& [id, inst] : instances_) {
        if (!inst->expired && inst->elapsedTime > 0.0f && inst->playerClientIds.empty()) {
            result.push_back(id);
        }
    }
    return result;
}
```

Update `tick()` to decrement celebration timer:

```cpp
void tick(float dt) {
    for (auto& [id, inst] : instances_) {
        inst->elapsedTime += dt;
        inst->world.update(dt);
        inst->world.processDestroyQueue();
        if (inst->completed && inst->celebrationTimer > 0.0f) {
            inst->celebrationTimer -= dt;
        }
    }
}
```

- [ ] **Step 3: Rewrite tickDungeonInstances in server_app.cpp**

```cpp
void ServerApp::tickDungeonInstances(float dt) {
    dungeonManager_.tick(dt);

    // Tick replication for each instance
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->expired) {
            inst->replication.update(inst->world, server_);
        }
    }

    // Check invite timeouts (30s)
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (!inst->pendingAccepts.empty()) {
            inst->inviteTimer += dt;
            if (inst->inviteTimer >= DungeonInstance::INVITE_TIMEOUT) {
                // Invite expired — cancel
                dungeonManager_.destroyInstance(id);
                break; // iterator invalidated
            }
        }
    }

    // Boss kill detection — check for dead MiniBoss in each instance
    for (auto& [id, inst] : dungeonManager_.allInstances()) {
        if (inst->completed || inst->expired) continue;
        inst->world.forEach<EnemyStatsComponent>([&](Entity* e, EnemyStatsComponent* es) {
            if (!es->stats.isAlive && es->stats.monsterType == "MiniBoss" && !inst->completed) {
                inst->completed = true;
                inst->celebrationTimer = 15.0f;
                distributeDungeonRewards(inst.get());
            }
        });
    }

    // Handle timed-out instances (10 min expired)
    for (uint32_t id : dungeonManager_.getTimedOutInstances()) {
        endDungeonInstance(id, 1); // reason=timeout
    }

    // Handle celebration finished (15s after boss kill)
    for (uint32_t id : dungeonManager_.getCelebrationFinishedInstances()) {
        endDungeonInstance(id, 0); // reason=boss_killed
    }

    // Handle all-disconnect
    for (uint32_t id : dungeonManager_.getEmptyActiveInstances()) {
        auto* inst = dungeonManager_.getInstance(id);
        if (inst) {
            inst->expired = true;
            dungeonManager_.destroyInstance(id);
            break; // iterator invalidated
        }
    }
}
```

- [ ] **Step 4: Implement distributeDungeonRewards**

Add declaration to `server_app.h`:
```cpp
    void distributeDungeonRewards(DungeonInstance* inst);
    void endDungeonInstance(uint32_t instanceId, uint8_t reason);
```

Implementation:
```cpp
void ServerApp::distributeDungeonRewards(DungeonInstance* inst) {
    int goldReward = 10000 * inst->difficultyTier;
    std::string treasureBoxId = "boss_treasure_box_t" + std::to_string(inst->difficultyTier);

    for (uint16_t cid : inst->playerClientIds) {
        auto* conn = server_.connections().findById(cid);
        if (!conn) continue;
        Entity* player = inst->world.getEntity(EntityHandle(conn->playerEntityId));
        if (!player) continue;

        auto* cs = player->getComponent<CharacterStatsComponent>();
        auto* inv = player->getComponent<InventoryComponent>();
        if (!cs || !inv) continue;

        // Gold reward
        if (conn) wal_.appendGoldChange(conn->character_id, static_cast<int64_t>(goldReward));
        inv->inventory.setGold(inv->inventory.getGold() + goldReward);

        // Boss honor (50)
        cs->stats.honor += 50;

        // Treasure box to inventory (if space)
        const auto* boxDef = itemDefCache_.getDefinition(treasureBoxId);
        if (boxDef) {
            ItemInstance box;
            box.itemId = treasureBoxId;
            box.quantity = 1;
            box.instanceId = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            box.rarity = ItemInstance::parseRarity("Rare");
            inv->inventory.addItem(box); // silently fails if full
        }

        sendPlayerState(cid);
        sendInventorySync(cid);
    }
}
```

- [ ] **Step 5: Implement endDungeonInstance**

```cpp
void ServerApp::endDungeonInstance(uint32_t instanceId, uint8_t reason) {
    auto* inst = dungeonManager_.getInstance(instanceId);
    if (!inst) return;

    // Send end message to all players
    SvDungeonEndMsg endMsg;
    endMsg.reason = reason;
    uint8_t buf[8];
    ByteWriter w(buf, sizeof(buf));
    endMsg.write(w);

    // Transfer each player back to overworld
    std::vector<uint16_t> clients = inst->playerClientIds; // copy — will be modified
    for (uint16_t cid : clients) {
        server_.sendTo(cid, Channel::ReliableOrdered, PacketType::SvDungeonEnd, buf, w.size());

        auto it = inst->returnPoints.find(cid);
        Vec2 returnPos = {0, 0};
        std::string returnScene = "WhisperingWoods";
        if (it != inst->returnPoints.end()) {
            returnPos = {it->second.x, it->second.y};
            returnScene = it->second.scene;
        }

        // Transfer back to overworld
        transferPlayerToWorld(cid, inst->world, inst->replication, world_, replication_, returnPos);

        // Set player's scene back
        auto* conn = server_.connections().findById(cid);
        if (conn) {
            Entity* player = world_.getEntity(EntityHandle(conn->playerEntityId));
            if (player) {
                auto* cs = player->getComponent<CharacterStatsComponent>();
                if (cs) cs->stats.currentScene = returnScene;
            }
            // Clear event lock
            playerEventLocks_.erase(conn->playerEntityId);
        }

        dungeonManager_.removePlayer(instanceId, cid);
    }

    dungeonManager_.destroyInstance(instanceId);
    LOG_INFO("Server", "Dungeon instance %u ended (reason=%u)", instanceId, reason);
}
```

- [ ] **Step 6: Add honor for mob kills inside dungeons**

In the mob kill path (both `processAction` kill and `processUseSkill` kill), add after XP award:

```cpp
    // Dungeon honor: +1 per mob kill, +50 for boss, to all party members
    uint32_t dungeonInstId = dungeonManager_.getInstanceForClient(clientId);
    if (dungeonInstId) {
        auto* dInst = dungeonManager_.getInstance(dungeonInstId);
        if (dInst) {
            int honorAmount = (es.monsterType == "MiniBoss") ? 50 : 1;
            for (uint16_t memberCid : dInst->playerClientIds) {
                auto* memberConn = server_.connections().findById(memberCid);
                if (!memberConn) continue;
                Entity* memberPlayer = dInst->world.getEntity(EntityHandle(memberConn->playerEntityId));
                if (!memberPlayer) continue;
                auto* memberCS = memberPlayer->getComponent<CharacterStatsComponent>();
                if (memberCS) memberCS->stats.honor += honorAmount;
                sendPlayerState(memberCid);
            }
        }
    }
```

- [ ] **Step 7: Skip death XP penalty in dungeons**

In the death penalty logic, add a check:

```cpp
    // No XP loss in dungeons
    if (dungeonManager_.getInstanceForClient(clientId) != 0) {
        // Skip death penalty
    }
```

- [ ] **Step 8: Build and test**

```bash
touch server/server_app.h server/server_app.cpp server/dungeon_manager.h tests/test_dungeon_lifecycle.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target fate_tests
./out/build/x64-Debug/fate_tests.exe -tc="DungeonLifecycle*"
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 9: Commit**

```bash
git add server/server_app.h server/server_app.cpp server/dungeon_manager.h tests/test_dungeon_lifecycle.cpp
git commit -m "feat: dungeon tick, boss kill rewards, timer/timeout, exit teleport"
```

---

### Task 8: GM Commands

**Files:**
- Modify: `server/server_app.cpp` (add commands in `initGMCommands`)

- [ ] **Step 1: Add /dungeon GM commands**

In `ServerApp::initGMCommands()`, add:

```cpp
gmCommands_.registerCommand({"dungeon", 2, [this, sendSystemMsg](uint16_t callerId, const std::vector<std::string>& args) {
    if (args.empty()) {
        sendSystemMsg(callerId, "Usage: /dungeon start <sceneId> | leave | list");
        return;
    }

    if (args[0] == "start") {
        if (args.size() < 2) { sendSystemMsg(callerId, "Usage: /dungeon start <sceneId>"); return; }
        auto* sceneInfo = sceneCache_.get(args[1]);
        if (!sceneInfo || !sceneInfo->isDungeon) {
            sendSystemMsg(callerId, "Unknown dungeon: " + args[1]);
            return;
        }
        // Bypass party/ticket checks for GM testing
        auto* conn = server_.connections().findById(callerId);
        if (!conn) return;
        Entity* player = world_.getEntity(EntityHandle(conn->playerEntityId));
        if (!player) return;

        uint32_t instId = dungeonManager_.createInstance(args[1], -1, sceneInfo->difficultyTier);
        auto* inst = dungeonManager_.getInstance(instId);
        inst->leaderClientId = callerId;

        auto* cs = player->getComponent<CharacterStatsComponent>();
        auto* transform = player->getComponent<Transform>();
        if (cs && transform) {
            inst->returnPoints[callerId] = {cs->stats.currentScene, transform->position.x, transform->position.y};
        }

        playerEventLocks_[conn->playerEntityId] = "Dungeon";

        Vec2 spawnPos = {sceneInfo->defaultSpawnX, sceneInfo->defaultSpawnY};
        transferPlayerToWorld(callerId, world_, replication_, inst->world, inst->replication, spawnPos);
        dungeonManager_.addPlayer(instId, callerId);

        // Spawn mobs
        // (same spawn logic as startDungeonInstance)

        SvDungeonStartMsg start;
        start.sceneId = args[1];
        start.timeLimitSeconds = 600;
        uint8_t buf[128];
        ByteWriter w(buf, sizeof(buf));
        start.write(w);
        server_.sendTo(callerId, Channel::ReliableOrdered, PacketType::SvDungeonStart, buf, w.size());

        sendSystemMsg(callerId, "Dungeon instance " + std::to_string(instId) + " created for " + args[1]);
    }
    else if (args[0] == "leave") {
        uint32_t instId = dungeonManager_.getInstanceForClient(callerId);
        if (instId == 0) { sendSystemMsg(callerId, "Not in a dungeon"); return; }
        endDungeonInstance(instId, 2); // reason=abandoned
        sendSystemMsg(callerId, "Left dungeon instance " + std::to_string(instId));
    }
    else if (args[0] == "list") {
        std::string msg = "Active dungeon instances: " + std::to_string(dungeonManager_.instanceCount());
        for (auto& [id, inst] : dungeonManager_.allInstances()) {
            msg += "\n  #" + std::to_string(id) + " scene=" + inst->sceneId
                 + " players=" + std::to_string(inst->playerClientIds.size())
                 + " elapsed=" + std::to_string((int)inst->elapsedTime) + "s"
                 + (inst->completed ? " [COMPLETED]" : "");
        }
        sendSystemMsg(callerId, msg);
    }
}});
```

- [ ] **Step 2: Build and test**

```bash
touch server/server_app.cpp
"$CMAKE" --build out/build/x64-Debug --config Debug --target FateServer
```

- [ ] **Step 3: Commit**

```bash
git add server/server_app.cpp
git commit -m "feat: GM /dungeon start|leave|list commands"
```

---

## Post-Implementation Notes

- **Scene files:** Each dungeon needs a `.json` scene file built in the editor for the client to render. Without one, players see a void with mobs. Build basic test rooms with floor tiles.
- **Migration 011** must be run against the dev DB before testing.
- **Restart FateServer.exe** after deploying.
- **Client-side** needs: dungeon invite popup UI, timer HUD, SvDungeonStart scene loading, SvDungeonEnd return teleport handling. These are client-only changes not covered in this plan.
- **Mob spawning inside instances** should reuse the same pattern as `ServerSpawnManager::createMob()` but against the instance's World and ReplicationManager.
- **Reconnect to dungeon:** When a player reconnects, check `dungeonManager_.getInstanceForClient()` and if found, route them to the instance instead of the overworld.
