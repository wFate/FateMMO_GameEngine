# Instanced Dungeons — TWOM-Style Design Spec

## Overview

Production-grade instanced dungeon system where each party gets an isolated ECS `World` with its own mobs, replication, and lifecycle. Follows TWOM dungeon mechanics: party-required entry, timed runs, no mob respawn, boss kill rewards, daily ticket limit.

## Entry Flow

1. Party leader sends `CmdStartDungeon { sceneId }` (menu selection, not a portal)
2. Server validates:
   - Party exists with 2+ members
   - All members meet `scenes.min_level` for the dungeon
   - No member is in another event (`playerEventLocks_` — covers Battlefield, Arena, Gauntlet, and other Dungeons)
   - All members have a dungeon ticket (`characters.last_dungeon_entry` is before today's reset time)
3. Server sends `SvDungeonInvite { sceneId, dungeonName, timeLimitSeconds, levelReq }` to all non-leader party members
4. Each member responds with `CmdDungeonResponse { accept: bool }`
5. If anyone declines or 30s timeout: cancel, notify party leader
6. Once all accept:
   - Save each player's current `position + currentScene` as return point (in-memory, on `DungeonInstance`)
   - Set `last_dungeon_entry = NOW()` in DB for all members (consumes ticket)
   - Lock all members in `playerEventLocks_` with `"Dungeon"`
   - Create `DungeonInstance` with its own `World` + `ReplicationManager`
   - Spawn mobs from `spawn_zones` for that scene (no respawn)
   - Move player entities from overworld `world_` to instance `world`
   - Send `SvDungeonStart { sceneId, timeLimitSeconds }` to all members — client loads dungeon scene file, starts timer HUD

## Dungeon Tickets

- 1 entry per character per day
- Resets daily at midnight Central Time (UTC-6, or UTC-5 during CDT)
- Tracked via `characters.last_dungeon_entry TIMESTAMP` column
- Server computes today's reset time on ticket check:
  - If `last_dungeon_entry IS NULL` or `last_dungeon_entry < today_midnight_ct`: ticket available
  - Ticket consumed on successful dungeon start (all members accept), not on invite
- No ticket refund if dungeon times out or party wipes

## Inside the Dungeon

### Isolated World
- Each `DungeonInstance` owns a `World` (separate ECS) and a `ReplicationManager`
- All handlers route to the correct `World` via:
  ```cpp
  World& ServerApp::getWorldForClient(uint16_t clientId) {
      uint32_t instId = dungeonManager_.getInstanceForClient(clientId);
      if (instId) return dungeonManager_.getInstance(instId)->world;
      return world_;
  }
  ```
- Every handler in `server_app.cpp` that currently references `world_` directly must go through this routing method

### Mob Spawning
- Mobs spawned once on instance creation from `spawn_zones` for the dungeon `sceneId`
- No respawn — dead mobs stay dead
- Mobs use the instance's `World`, not the overworld

### Timer
- 10 minutes per dungeon (configurable per scene if needed)
- Countdown starts when all players are teleported in
- Client displays timer on HUD (from `SvDungeonStart.timeLimitSeconds`, counts down locally)

### Death Rules
- No XP loss on death inside dungeons
- Player respawns at dungeon spawn point (`scenes.default_spawn_x/y`)
- Must run back to where mobs/boss are (mobs don't respawn, so cleared areas stay clear)

### Honor
- Each regular mob kill: +1 honor to all party members in the instance
- Boss kill: +50 honor to all party members in the instance

## Boss Kill → Reward → Exit

1. Boss dies (`monster_type == "MiniBoss"` in the instance world)
2. Dungeon marked as `completed`
3. **Rewards distributed to all party members:**
   - Boss drops a treasure loot entity on the ground (normal loot table roll, normal pickup rules)
   - Each player receives a **boss treasure box item** added to inventory (skipped if inventory full — no error, just no box)
   - Each player receives **gold** scaled by dungeon difficulty tier:
     - Tier 1 (min_level 1-5): 10,000 gold
     - Tier 2 (min_level 6-10): 20,000 gold
     - Tier 3 (min_level 11-20): 30,000 gold
     - Higher tiers: +10,000 per tier
   - Gold applied via `setGold()` (server-authoritative)
   - WAL-logged
4. **15-second celebration countdown** — players can open treasure box, see loot
5. After 15s: `SvDungeonEnd { reason=0 (boss_killed) }` sent to all
6. All players teleported to their saved return positions (pre-dungeon scene + coordinates)
7. `playerEventLocks_` cleared for all members
8. Instance destroyed

## Timeout

- Timer reaches 0 with boss still alive
- No loot, no gold, no treasure box
- `SvDungeonEnd { reason=1 (timeout) }` sent
- All players teleported to saved return positions
- Event locks cleared, instance destroyed

## Disconnect Handling

- **Single player disconnects:** Removed from instance. Saved return position preserved. If they reconnect before instance expires, they can rejoin (their return data is on the `DungeonInstance`). If the instance ends while they're offline, on reconnect they appear at their saved return position.
- **All players disconnect:** Instance expires immediately. No loot payout. No celebration window. Players will appear at their saved return positions when they reconnect.

## Handler Routing

All ~30 handlers in `server_app.cpp` that reference `world_` must use `getWorldForClient(clientId)` instead. Key handlers:

- `processAction` (auto-attack)
- `processUseSkill`
- `processEquip` / `processEnchant` / `processRepair`
- `processExtractCore` / `processCraft`
- `processBank`
- `processUseConsumable`
- `tickPetAutoLoot`
- Loot pickup handler
- Movement validation
- Entity lookup for targeting
- `recalcEquipmentBonuses`
- `sendPlayerState`
- Death/respawn logic
- Replication tick

The instance's `ReplicationManager` is ticked alongside its `World` in `tickDungeonInstances()`.

## GM Commands

- `/dungeon start <sceneId>` — force-start dungeon (bypasses party size and ticket checks, works solo for testing)
- `/dungeon leave` — force-exit current dungeon, teleport to return position
- `/dungeon list` — show active instances with player counts and elapsed time

## Protocol Messages

### Client → Server
| Message | ID | Fields |
|---------|-----|--------|
| `CmdStartDungeon` | TBD | `sceneId: string` |
| `CmdDungeonResponse` | TBD | `accept: uint8` |

### Server → Client
| Message | ID | Fields |
|---------|-----|--------|
| `SvDungeonInvite` | TBD | `sceneId: string, dungeonName: string, timeLimitSeconds: uint16, levelReq: uint8` |
| `SvDungeonStart` | TBD | `sceneId: string, timeLimitSeconds: uint16` |
| `SvDungeonEnd` | TBD | `reason: uint8` (0=boss_killed, 1=timeout, 2=abandoned) |
| `SvDungeonTimer` | TBD | `secondsRemaining: uint16` (periodic sync, optional) |

## Database Changes

### Schema
```sql
ALTER TABLE characters ADD COLUMN last_dungeon_entry TIMESTAMP;
ALTER TABLE scenes ADD COLUMN difficulty_tier INTEGER DEFAULT 1;
UPDATE scenes SET difficulty_tier = 1 WHERE scene_id = 'GoblinCave';
UPDATE scenes SET difficulty_tier = 2 WHERE scene_id = 'UndeadCrypt';
UPDATE scenes SET difficulty_tier = 3 WHERE scene_id = 'DragonLair';
```

### Item Definitions
Boss treasure box items needed in `item_definitions`:
- `boss_treasure_box_t1` (Tier 1 dungeon reward)
- `boss_treasure_box_t2` (Tier 2 dungeon reward)
- `boss_treasure_box_t3` (Tier 3 dungeon reward)

These are inventory items the player can open later (consumable type with loot table roll on use).

### No New Tables
Dungeon instance state is purely runtime. No persistence across server restarts. The only DB write is `last_dungeon_entry` on entry.

## Ticket Reset Time

- Midnight Central Time (America/Chicago)
- UTC-6 (CST) or UTC-5 (CDT) depending on daylight saving
- Server computes reset: `today at 06:00 UTC` (= midnight CT during CST) or uses timezone-aware calculation

## Event Lock Integration

Uses existing `playerEventLocks_` map (`entityId -> eventType string`):
- On dungeon start: `playerEventLocks_[entityId] = "Dungeon"` for all members
- On dungeon end/timeout/disconnect: erase lock for all members
- Entry validation checks: if player has any event lock, reject dungeon start

## DungeonInstance Changes

```cpp
struct DungeonInstance {
    uint32_t instanceId = 0;
    std::string sceneId;
    int partyId = -1;
    int difficultyTier = 1;
    World world;
    ReplicationManager replication;          // per-instance
    ServerSpawnManager spawnManager;         // no-respawn mode
    float elapsedTime = 0.0f;
    float timeLimitSeconds = 600.0f;        // 10 minutes
    float celebrationTimer = -1.0f;         // set to 15.0f on boss kill
    bool completed = false;
    std::vector<uint16_t> playerClientIds;

    // Per-player return data
    struct ReturnPoint {
        std::string scene;
        float x, y;
    };
    std::unordered_map<uint16_t, ReturnPoint> returnPoints; // clientId -> where to send them back

    // Pending invite tracking
    std::unordered_set<uint16_t> pendingAccepts; // clientIds that haven't accepted yet
};
```

## Testing

Unit tests for:
- Ticket validation (before/after reset time)
- Instance creation with return point saving
- Player routing (`getWorldForClient`)
- Boss kill reward distribution (gold, honor, treasure box)
- Timer expiry cleanup
- All-disconnect immediate expiry
- Event lock integration (can't enter dungeon while in arena, etc.)
- GM command bypass

Integration tests (scenario bot):
- Full dungeon run: enter → kill mobs → kill boss → rewards → teleport back
- Timeout flow: enter → wait → teleport back with no loot
- Decline flow: leader starts → member declines → cancelled
