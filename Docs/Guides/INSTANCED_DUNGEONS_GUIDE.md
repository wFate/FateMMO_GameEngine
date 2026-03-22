# Instanced Dungeons Guide

## Overview

TWOM-style instanced dungeons where each party gets an isolated ECS world. Players inside an instance only interact with entities in their instance. Mobs don't respawn, there's a 10-minute timer, and killing the boss rewards the entire party.

## How Dungeons Work (Player Perspective)

1. Party leader opens dungeon menu, selects a dungeon, clicks "Start"
2. All party members get an invite popup — 30 seconds to accept
3. If anyone declines, the dungeon is cancelled
4. Once all accept: everyone teleports to the dungeon spawn point
5. Kill mobs (they don't respawn) and work toward the boss
6. If you die, you respawn at the dungeon entrance and run back
7. Kill the boss → 15-second celebration window → everyone gets rewards → teleport home
8. If the 10-minute timer expires, everyone teleports home with no rewards

## Rules

- **Party required:** minimum 2 players
- **Daily ticket:** 1 dungeon entry per character per day, resets at midnight Central Time
- **No XP loss:** dying in a dungeon has no XP penalty
- **Honor:** +1 per mob kill, +50 per boss kill (to ALL party members)
- **No respawn:** dead mobs stay dead
- **Event lock:** can't enter a dungeon while in Arena, Battlefield, or Gauntlet (and vice versa)

## Rewards (Boss Kill)

All party members receive:
- **Gold:** 10,000 × difficulty tier (tier 1 = 10K, tier 2 = 20K, tier 3 = 30K)
- **Boss treasure box:** inventory item (Goblin Hoard / Crypt Reliquary / Dragon Hoard)
- **+50 honor**
- **Boss ground loot:** rolled from the boss's loot table (normal pickup rules)

## Current Dungeons

| Scene ID | Name | Level | Tier | Gold | Treasure Box |
|----------|------|-------|------|------|--------------|
| GoblinCave | Goblin Cave | 3+ | 1 | 10,000 | Goblin Hoard (Rare) |
| UndeadCrypt | Undead Crypt | 8+ | 2 | 20,000 | Crypt Reliquary (Epic) |
| DragonLair | Dragon's Lair | 15+ | 3 | 30,000 | Dragon Hoard (Legendary) |

---

## Architecture

### Key Classes

**`DungeonInstance`** (`server/dungeon_manager.h`):
```
instanceId          — unique runtime ID
sceneId             — DB scene (e.g. "GoblinCave")
partyId             — party that owns this instance
difficultyTier      — reward scaling (1/2/3)
world               — isolated ECS World
replication         — per-instance ReplicationManager
timeLimitSeconds    — 600 (10 minutes)
celebrationTimer    — set to 15.0 on boss kill
completed / expired — lifecycle flags
playerClientIds     — clients currently inside
returnPoints        — saved position+scene per client (for teleport home)
pendingAccepts      — invite flow tracking
```

**`DungeonManager`** (`server/dungeon_manager.h`):
- Creates/destroys instances
- Tracks mappings: instance↔party, instance↔client
- Ticks all instance worlds + replication each server frame
- Detects timeouts, boss kills, empty instances

### Handler Routing

Every server handler that touches entities goes through:
```cpp
World& world = getWorldForClient(clientId);
ReplicationManager& repl = getReplicationForClient(clientId);
```

If the client is in a dungeon, these return the instance's World/Replication. Otherwise, they return the overworld's. This means combat, skills, loot, equipment, and every other handler works inside dungeons automatically.

### Player Transfer

`transferPlayerToWorld()` moves a player between worlds:
1. Snapshots all components (stats, inventory, skills, pet, party, faction, etc.)
2. Unregisters from source ReplicationManager
3. Destroys entity in source World
4. Creates new entity in destination World
5. Copies all component data
6. Registers in destination ReplicationManager
7. Updates `conn->playerEntityId`

### Server Tick Integration

```
ServerApp::tick(dt)
    ├── world_.update(dt)              ← overworld
    ├── spawnManager_.tick()           ← overworld mob respawn
    ├── forEachAllWorlds: status effects, regen, death, etc.
    └── tickDungeonInstances(dt)
          ├── dungeonManager_.tick()   ← all instance worlds
          ├── instance replication     ← entity sync per instance
          ├── invite timeouts          ← 30s
          ├── boss kill detection      ← dead MiniBoss
          ├── timer expiry             ← 10 min
          ├── celebration finish       ← 15s after boss
          └── empty instance cleanup   ← all disconnected
```

---

## How to Add a New Dungeon

### 1. Add the scene to the DB

```sql
INSERT INTO scenes (scene_id, scene_name, scene_type, min_level, is_dungeon, pvp_enabled, difficulty_tier, default_spawn_x, default_spawn_y)
VALUES ('VolcanicPit', 'Volcanic Pit', 'Dungeon', 25, true, false, 4, 0, 0);
```

Key fields:
- `is_dungeon = true` — marks it as an instanced dungeon
- `difficulty_tier` — gold reward multiplier (tier 4 = 40,000 gold)
- `min_level` — party members below this can't enter
- `default_spawn_x/y` — where players spawn inside the dungeon

### 2. Add spawn zones

```sql
INSERT INTO spawn_zones (scene_id, zone_name, center_x, center_y, radius, mob_def_id, target_count) VALUES
('VolcanicPit', 'Entrance',    0,   0,   150, 'sandstorm_elemental', 4),
('VolcanicPit', 'Inner Cave',  300, 0,   120, 'sunscale_raptor',     3),
('VolcanicPit', 'Boss Arena',  500, 0,   80,  'sand_shark',          1);
```

- Place zones at increasing `center_x` to create progression
- The boss zone should have exactly **1 mob with `monster_type = 'MiniBoss'`** — that's what triggers the boss kill detection
- Regular mobs can be any type/count

### 3. Add a treasure box item

```sql
INSERT INTO item_definitions (item_id, name, type, subtype, description, rarity, max_stack, gold_value)
VALUES ('boss_treasure_box_t4', 'Volcanic Treasure', 'Consumable', 'TreasureBox',
        'Molten treasure from the Volcanic Pit.', 'Legendary', 1, 8000)
ON CONFLICT (item_id) DO NOTHING;
```

The treasure box ID must be `boss_treasure_box_t<tier>` — the server constructs this string from the difficulty tier.

### 4. Build a scene file in the editor

Open the editor, create a new scene, paint floor tiles and walls, save as `VolcanicPit.json`. This is what the client renders — without it, players see a void.

### 5. Restart FateServer.exe

The server loads scenes and spawn zones at startup.

### 6. Test with GM command

```
/dungeon start VolcanicPit
```

This bypasses party and ticket requirements so you can test solo.

---

## How to Add Custom Dungeon Mobs

Currently dungeons reuse overworld mobs. To add dungeon-specific mobs:

```sql
-- Add the mob definition
INSERT INTO mob_definitions (mob_def_id, mob_name, display_name, base_hp, base_damage, base_armor,
    base_xp_reward, xp_per_level, hp_per_level, damage_per_level,
    min_spawn_level, max_spawn_level, aggro_range, attack_range, leash_radius,
    respawn_seconds, is_aggressive, monster_type, min_gold_drop, max_gold_drop, gold_drop_chance)
VALUES
    ('crypt_lord', 'Crypt Lord', 'Crypt Lord', 5000, 120, 30,
     200, 10, 50, 5,
     8, 8, 6.0, 2.0, 12.0,
     0, true, 'MiniBoss', 50, 200, 1.0);

-- Update the spawn zone to use it
UPDATE spawn_zones SET mob_def_id = 'crypt_lord'
WHERE scene_id = 'UndeadCrypt' AND zone_name = 'Crypt Lord';
```

Set `monster_type = 'MiniBoss'` for the dungeon boss — this is what triggers boss kill detection and the 50-honor reward.

---

## GM Commands

| Command | Description |
|---------|-------------|
| `/dungeon start <sceneId>` | Create and enter a dungeon solo (bypasses party/ticket checks) |
| `/dungeon leave` | Force-exit current dungeon |
| `/dungeon list` | Show all active instances with player counts and elapsed time |

Requires admin role (level 2).

---

## Troubleshooting

**"Not in a dungeon" when using /dungeon leave:**
- You're in the overworld, not in a dungeon instance

**Mobs don't appear in dungeon:**
- Check `spawn_zones` table has entries for the dungeon's `scene_id`
- Check `mob_definitions` table has entries for the `mob_def_id` values referenced
- Restart FateServer.exe (caches load at startup)

**Boss kill doesn't end the dungeon:**
- The boss mob must have `monster_type = 'MiniBoss'` in `mob_definitions`
- Check with: `SELECT mob_def_id, monster_type FROM mob_definitions WHERE mob_def_id = 'your_boss_id'`

**Rewards not distributed:**
- Treasure box requires `boss_treasure_box_t<tier>` in `item_definitions`
- Gold uses `setGold()` (server-authoritative) — check WAL log for the gold change entry
- Players with full inventory don't get the treasure box (silently skipped)

**Event lock stuck (can't enter new dungeon after disconnect):**
- Event locks are cleared on dungeon exit. If a player disconnects during a dungeon and the instance expires, the lock should be cleared on reconnect via the disconnect handler
- Manual fix: restart the server (locks are runtime-only, not persisted)

**Daily ticket not resetting:**
- Resets at midnight Central Time (America/Chicago timezone)
- Check with: `SELECT character_id, last_dungeon_entry FROM characters WHERE character_id = 'your_char'`
- Manual reset: `UPDATE characters SET last_dungeon_entry = NULL WHERE character_id = 'your_char'`

---

## Client-Side Requirements (Not Yet Implemented)

These need to be built on the client for the full player experience:

1. **Dungeon invite popup** — triggered by `SvDungeonInviteMsg`, shows dungeon name/level/timer, Accept/Decline buttons, sends `CmdDungeonResponseMsg`
2. **Dungeon timer HUD** — triggered by `SvDungeonStartMsg`, countdown display from `timeLimitSeconds`
3. **Scene loading** — on `SvDungeonStartMsg`, load the dungeon scene file (e.g. `GoblinCave.json`)
4. **Return teleport** — on `SvDungeonEndMsg`, load the return scene and reposition camera
5. **Dungeon menu** — UI for the party leader to select and start dungeons (sends `CmdStartDungeonMsg`)

---

## Testing Checklist

```bash
# Run unit tests
./fate_tests.exe -tc="DungeonManager*"
./fate_tests.exe -tc="DungeonTransfer*"
./fate_tests.exe -tc="DungeonEntry*"
./fate_tests.exe -tc="DungeonLifecycle*"

# In-game GM test
/dungeon start GoblinCave    # Enter dungeon solo
/dungeon list                 # Verify instance exists
# Kill mobs, kill boss        # Verify rewards
/dungeon leave                # Force-exit (or wait for timer)
```
