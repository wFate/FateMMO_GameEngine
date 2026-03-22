# Instanced Dungeons Guide

## Overview

Instanced dungeons give each party their own isolated copy of a dungeon scene. Players inside an instance only see and interact with entities in that instance — mobs, drops, and other players in the overworld are invisible, and vice versa. When the party leaves or the instance times out, it's destroyed.

## Architecture

### Key Classes

**`DungeonInstance`** (`server/dungeon_manager.h`) — A single dungeon run:
- `instanceId` — unique ID assigned on creation
- `sceneId` — which dungeon scene (e.g. `"GoblinCave"`)
- `partyId` — the party that owns this instance
- `world` — isolated `World` (ECS) with its own entities
- `spawnManager` — spawns mobs within this instance's world
- `playerClientIds` — which clients are currently inside
- `elapsedTime` / `timeoutSeconds` — 30-minute default timeout

**`DungeonManager`** (`server/dungeon_manager.h`) — Manages all active instances:
- Creates/destroys instances
- Tracks party-to-instance and client-to-instance mappings
- Ticks all instance worlds each server frame
- Auto-destroys expired empty instances

### How It Fits Into the Server

```
ServerApp::tick(dt)
    ├── world_.update(dt)           ← overworld entities
    ├── spawnManager_.tick()        ← overworld mob spawning
    ├── tickPetAutoLoot(dt)
    ├── tickAutoSave(dt)
    ├── tickMaintenance(dt)
    └── tickDungeonInstances(dt)    ← all dungeon instance worlds
          ├── dungeonManager_.tick(dt)
          │     └── for each instance:
          │           ├── world.update(dt)
          │           └── world.processDestroyQueue()
          └── destroy expired empty instances
```

Each `DungeonInstance` has its own `World`, completely separate from the server's main `world_`. Entities created in an instance don't exist in the overworld or in other instances.

### Database Schema

**`scenes` table** — dungeon scenes have `is_dungeon = true`:
```sql
SELECT scene_id, scene_name, min_level, is_dungeon FROM scenes WHERE is_dungeon = true;
-- GoblinCave    | Goblin Cave    | 3  | true
-- UndeadCrypt   | Undead Crypt   | 8  | true
-- DragonLair    | Dragon's Lair  | 15 | true
```

**`spawn_zones` table** — mobs to spawn per dungeon:
```sql
SELECT scene_id, zone_name, mob_def_id, target_count FROM spawn_zones
WHERE scene_id IN ('GoblinCave', 'UndeadCrypt', 'DragonLair')
ORDER BY scene_id, center_x;
```

Each dungeon has 3 zones (entrance → mid → boss) with mobs placed at increasing `center_x` positions (0, 300, 500) to simulate progression through the dungeon.

## Current State

### What's Done
- `DungeonManager` and `DungeonInstance` classes (fully implemented)
- Party-to-instance and client-to-instance bidirectional tracking
- Per-instance `World` (isolated ECS)
- Server tick integration (all instances updated each frame)
- Auto-cleanup of expired empty instances (30-min timeout)
- 3 dungeon scenes in DB (GoblinCave, UndeadCrypt, DragonLair)
- Spawn zones configured with overworld mobs as placeholders
- 8 unit tests covering creation, isolation, tracking, expiry

### What's NOT Done Yet
- **Enter/exit transition flow** — no way for players to actually enter a dungeon yet
- **Dungeon-specific replication** — instance entities aren't replicated to clients
- **Dungeon-specific mob definitions** — using overworld mobs as placeholders (e.g. Timber Alpha as GoblinCave boss instead of a custom Cave Troll)
- **Dungeon SpawnManager wiring** — `spawnManager` field exists but isn't initialized with zones yet
- **Loot/XP inside instances** — needs routing to the instance world instead of the overworld
- **Completion conditions** — no boss-kill detection or victory state

## How to Build on This

### Step 1: Dungeon Enter/Exit Flow

The core missing piece. Suggested approach:

1. **Add a `CmdEnterDungeon` message** (client → server):
   ```
   CmdEnterDungeon { sceneId: string }
   ```

2. **Server handler `processEnterDungeon()`:**
   - Validate player is in a party
   - Validate scene exists and `is_dungeon == true`
   - Validate player meets `min_level`
   - Check if party already has an instance (`getInstanceForParty`)
   - If not, create one (`createInstance`) and initialize its `SpawnManager`
   - Remove player entity from overworld `world_`
   - Create player entity in instance `world`
   - Track via `addPlayer(instanceId, clientId)`
   - Send scene transition to client

3. **Exit flow** (on disconnect, timeout, or explicit leave):
   - Remove player entity from instance world
   - `removePlayer(instanceId, clientId)`
   - Re-create player entity in overworld `world_` at their saved position
   - If instance is now empty, it will auto-expire after timeout

### Step 2: Instance-Specific Replication

Currently `ReplicationManager::buildVisibility()` only scans the overworld `world_`. For dungeon instances:

**Option A (simpler):** Give each `DungeonInstance` its own `ReplicationManager`. Build visibility against the instance's `world` for clients inside that instance.

**Option B (single manager):** Extend the existing `ReplicationManager` to accept a `World*` parameter and scope visibility to it based on `getInstanceForClient()`.

Option A is cleaner — each instance is fully self-contained.

### Step 3: SpawnManager Initialization

When creating a dungeon instance, initialize its spawn manager:

```cpp
uint32_t id = dungeonManager_.createInstance(sceneId, partyId);
auto* inst = dungeonManager_.getInstance(id);

// Load spawn zones for this dungeon scene
auto zones = spawnZoneCache_.getZonesForScene(sceneId);
inst->spawnManager = std::make_shared<ServerSpawnManager>();
inst->spawnManager->initialize(sceneId, inst->world, /* replication */, spawnZoneCache_, mobDefCache_);
```

### Step 4: Custom Dungeon Mobs

The current spawn zones use overworld mobs as placeholders. To add dungeon-specific mobs:

```sql
-- Add custom dungeon boss
INSERT INTO mob_definitions (mob_def_id, mob_name, display_name, base_hp, base_damage, ...)
VALUES ('crypt_lord', 'Crypt Lord', 'Crypt Lord', 5000, 120, ...);

-- Update spawn zone to use it
UPDATE spawn_zones SET mob_def_id = 'crypt_lord'
WHERE scene_id = 'UndeadCrypt' AND zone_name = 'Crypt Lord';
```

Planned custom bosses:
- **GoblinCave:** Cave Troll (replaces Timber Alpha)
- **UndeadCrypt:** Crypt Lord (replaces Spine Shell)
- **DragonLair:** Drake (replaces Spine Shell)

### Step 5: Completion & Rewards

Detect when the boss is killed and reward the party:

1. Hook into mob death in the instance world
2. Check if the dead mob is the boss (by `monster_type == "MiniBoss"` or a `is_dungeon_boss` flag)
3. Grant bonus XP/loot to all party members in the instance
4. Start a 60-second timer, then teleport everyone out and destroy the instance

## API Reference

```cpp
// Create a new instance for a party
uint32_t id = dungeonManager_.createInstance("GoblinCave", partyId);

// Look up instance by party or client
uint32_t id = dungeonManager_.getInstanceForParty(partyId);   // 0 if none
uint32_t id = dungeonManager_.getInstanceForClient(clientId); // 0 if none

// Get the instance object
DungeonInstance* inst = dungeonManager_.getInstance(id);
inst->world;            // isolated ECS world
inst->spawnManager;     // mob spawner for this instance
inst->playerClientIds;  // who's inside

// Player tracking
dungeonManager_.addPlayer(id, clientId);
dungeonManager_.removePlayer(id, clientId);

// Cleanup
dungeonManager_.destroyInstance(id);                // immediate
auto expired = dungeonManager_.getExpiredInstances(); // empty + timed out
```

## Testing

Tests are in `tests/test_dungeon_manager.cpp` (8 test cases):
- Instance creation and ID uniqueness
- World isolation between instances
- Destroy cleanup (party and client mappings)
- Party-to-instance lookup
- Tick without crash
- Player add/remove tracking
- Expiry only when empty
- Party mapping cleanup on destroy

Run with:
```bash
./fate_tests.exe -tc="DungeonManager*"
```
