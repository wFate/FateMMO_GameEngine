# FateMMO Mob Creation Guide
## C++ Engine — Data-Driven Workflow

> **To create a new mob, you need: 2 SQL INSERTs + 1 sprite file + server restart.** No prefabs, no C++ code, no recompilation. No editor placement — mobs are spawned entirely by the server from database rows.

---

## How Mobs Get Into the Game

There are NO mob objects in the editor. Unlike static scene elements (tiles, NPCs, decorations), mobs are **100% server-spawned at runtime** from two database tables:

1. **`mob_definitions`** — defines WHAT a mob is (stats, AI, loot)
2. **`spawn_zones`** — defines WHERE and HOW MANY spawn in the world

When the server starts, it reads both tables, and for each `spawn_zones` row it creates that many mob entities at random positions within the zone's circle. When a mob dies, the server respawns it after the configured timer. You never touch the editor or C++ code — just SQL and a sprite file.

---

## Quick Start: Full Example

Here's a complete example of adding Cave Bats to WhisperingWoods:

### Step 1: Define the mob (what it IS)

```sql
INSERT INTO mob_definitions (
    mob_def_id, display_name,
    base_hp, base_damage, base_armor,
    aggro_range, attack_range, leash_radius,
    move_speed, attack_speed, respawn_seconds,
    base_xp_reward, min_gold_drop, max_gold_drop, gold_drop_chance,
    min_spawn_level, max_spawn_level,
    is_boss, loot_table_id
) VALUES (
    'cave_bat',             -- Unique ID (lowercase, underscores, no spaces)
    'Cave Bat',             -- Display name (shown in UI AND used for sprite filename)
    80, 10, 1,              -- HP, Damage, Armor at level 1
    4.0, 1.2, 6.0,         -- Aggro, Attack, Leash (IN TILES — engine converts ×32 to pixels)
    1.5, 2.0, 30,          -- Move speed (tiles/sec), Attack cooldown (sec), Respawn (sec)
    25, 5, 15, 0.6,        -- Base XP, Gold min, Gold max, Gold drop chance (0.0-1.0)
    10, 14,                 -- Min/Max spawn level
    false, 'loot_forest_common'  -- Not a boss, uses forest common loot table
);
```

This row alone does NOT make the mob appear in the game. It just registers the mob type in the system.

### Step 2: Place a sprite file

Place a PNG at: `assets/sprites/mob_Cave Bat.png`

The filename is **exactly** `mob_` + the `display_name` value + `.png`. Case matters. If the sprite is missing, the client shows a colored placeholder rectangle.

Boss mobs render at 48×48px, normal mobs at 32×32px.

### Step 3: Add a spawn zone (WHERE it appears in the world)

This is the step that actually makes mobs appear in the game:

```sql
INSERT INTO spawn_zones (
    scene_id, zone_name, center_x, center_y, radius,
    mob_def_id, target_count
) VALUES (
    'WhisperingWoods',      -- Which scene (must exist in the scene system)
    'Bat Cave',             -- Human-readable label (for admin/debug only)
    -200, 150,              -- Center position in PIXELS (not tiles!)
    100,                    -- Spawn radius in PIXELS (mobs appear randomly within this circle)
    'cave_bat',             -- Must match mob_def_id from Step 1 exactly
    4                       -- Server keeps 4 Cave Bats alive at all times in this zone
);
```

The server will spawn 4 Cave Bats at random positions within a 100px radius circle centered at pixel (-200, 150) in WhisperingWoods. When one dies, it respawns after 30 seconds (from Step 1).

### Step 4: Restart the Server

Both tables are cached on startup. **You must restart `FateServer.exe`** for new rows to take effect.

After restart, the mobs appear immediately — no editor interaction needed.

### What you should see

- 4 Cave Bat entities in the world at the specified location
- Each with a random level between 10-14 (from min/max_spawn_level)
- HP scaled: `base_hp + hp_per_level × (level - 1)`
- They aggro players within ~128px (4 tiles × 32), attack within ~38px (1.2 tiles × 32)
- On kill: XP + gold drop + loot table roll
- Respawn after 30 seconds at a new random position in the same zone

---

## Architecture Overview

```
SERVER STARTUP
│
├─ MobDefCache loads ALL mob_definitions from PostgreSQL
├─ SpawnZoneCache loads ALL spawn_zones, grouped by scene_id
│
└─ ServerSpawnManager.initialize(sceneId):
   │
   For each spawn zone in the scene:
   ├─ Look up CachedMobDef by mob_def_id
   ├─ Create target_count mob entities
   │   ├─ Random level in [min_spawn_level, max_spawn_level]
   │   ├─ Random position within zone radius
   │   ├─ EnemyStatsComponent ← scaled stats from CachedMobDef
   │   ├─ MobAIComponent ← aggro/attack/leash (tiles × 32 = pixels)
   │   ├─ MobNameplateComponent ← display_name + level + isBoss
   │   └─ Register with ReplicationManager (assigns PersistentId)
   │
   └─ Replication sends SvEntityEnterMsg to nearby clients
      └─ Client creates ghost entity with sprite from display_name
```

### Key Principles

| Concept | Description |
|---------|-------------|
| **Server-Authoritative** | Server controls ALL mob behavior, stats, damage, XP, and loot |
| **Client Display Only** | Clients see results (health bars, positions, nameplates) |
| **Data-Driven** | All stats from PostgreSQL — zero hardcoding in C++ |
| **No Prefabs** | Mob entities are created programmatically from DB data |
| **Tile/Pixel Conversion** | DB stores AI radii in tiles; engine converts ×32 to pixels |

---

## Database Reference

### mob_definitions — All Columns

| Column | Type | Required | Description | Example |
|--------|------|----------|-------------|---------|
| `mob_def_id` | VARCHAR(64) | **Yes** (PK) | Unique ID, lowercase | `timber_wolf` |
| `display_name` | VARCHAR(128) | **Yes** | UI name + sprite filename | `Timber Wolf` |
| `mob_name` | VARCHAR(64) | No | Legacy field, use `display_name` | — |
| `base_hp` | INTEGER | **Yes** | HP at level 1 | 120 |
| `base_damage` | INTEGER | **Yes** | Damage at level 1 | 15 |
| `base_armor` | INTEGER | No (default 0) | Armor at level 1 | 3 |
| `crit_rate` | REAL | No (default 0.05) | Crit probability 0-1 | 0.05 |
| `attack_speed` | REAL | No (default 2.0) | Seconds between attacks | 1.5 |
| `move_speed` | REAL | No (default 1.0) | Tiles per second | 1.0 |
| `aggro_range` | REAL | **Yes** | Detection radius **in tiles** | 4.0 |
| `attack_range` | REAL | **Yes** | Attack distance **in tiles** | 1.2 |
| `leash_radius` | REAL | **Yes** | Max chase distance **in tiles** | 6.0 |
| `respawn_seconds` | INTEGER | No (default 30) | Time to respawn after death | 30 |
| `hp_per_level` | REAL | No (default 0) | HP scaling per level | 10.0 |
| `damage_per_level` | REAL | No (default 0) | Damage scaling per level | 2.0 |
| `armor_per_level` | REAL | No (default 0) | Armor scaling per level | 0.5 |
| `base_xp_reward` | INTEGER | No (default 0) | Base XP on kill | 45 |
| `xp_per_level` | INTEGER | No (default 0) | XP scaling per level | 5 |
| `min_spawn_level` | INTEGER | No (default 1) | Minimum spawn level | 3 |
| `max_spawn_level` | INTEGER | No (default 1) | Maximum spawn level | 7 |
| `is_boss` | BOOLEAN | No (default false) | Boss flag (48px sprite, larger nameplate) | false |
| `is_elite` | BOOLEAN | No (default false) | Elite flag | false |
| `attack_style` | VARCHAR(16) | No (default 'Melee') | `Melee`, `Ranged` | Melee |
| `monster_type` | VARCHAR(16) | No (default 'Normal') | `Normal`, `MiniBoss`, `Boss`, `RaidBoss` | Normal |
| `loot_table_id` | VARCHAR(64) | No | References `loot_tables` table | `loot_forest_common` |
| `min_gold_drop` | INTEGER | No (default 0) | Minimum gold on kill | 10 |
| `max_gold_drop` | INTEGER | No (default 0) | Maximum gold on kill | 25 |
| `gold_drop_chance` | REAL | No (default 0) | Probability of gold drop 0-1 | 0.8 |
| `magic_resist` | INTEGER | No (default 0) | Magic resistance points | 0 |
| `deals_magic_damage` | BOOLEAN | No (default false) | Uses MR instead of armor | false |
| `mob_hit_rate` | INTEGER | No (default 0) | Accuracy/to-hit stat | 5 |
| `honor_reward` | INTEGER | No (default 0) | Honor gained on kill | 0 |
| `spawn_weight` | INTEGER | No (default 10) | Relative spawn weighting | 10 |

### Understanding AI Radii

**All AI radii are stored in TILES in the database.** The engine multiplies by 32 to convert to pixels.

```
Database (tiles)  →  Engine (pixels)
aggro_range: 4.0  →  128px (4 × 32)
attack_range: 1.2 →  38.4px (1.2 × 32)
leash_radius: 6.0 →  192px (6 × 32)
move_speed: 1.0   →  32px/sec (1 × 32)
```

**Recommended values for melee mobs:**
- `aggro_range`: 3.0-5.0 tiles (96-160px)
- `attack_range`: 1.0-1.5 tiles (32-48px) — sprites touching to small gap
- `leash_radius`: 6.0-10.0 tiles (192-320px)

**For ranged mobs:**
- `aggro_range`: 5.0-8.0 tiles
- `attack_range`: 4.0-6.0 tiles
- `leash_radius`: 8.0-12.0 tiles

### Stat Scaling Formulas

Stats scale with mob level using these formulas (from `CachedMobDef`):

```
HP at level L     = base_hp + hp_per_level × (L - 1)
Damage at level L = base_damage + damage_per_level × (L - 1)
Armor at level L  = base_armor + armor_per_level × (L - 1)
XP reward         = base_xp_reward + xp_per_level × (L - 1)
```

---

## Sprite Naming Convention

The client builds sprite paths from `display_name`:

```
Sprite path = "assets/sprites/mob_" + display_name + ".png"
```

| display_name | Sprite File |
|-------------|-------------|
| Timber Wolf | `assets/sprites/mob_Timber Wolf.png` |
| Grizzly Bear | `assets/sprites/mob_Grizzly Bear.png` |
| Elder Treant | `assets/sprites/mob_Elder Treant.png` |

If the sprite file doesn't exist, the client generates a **colored placeholder rectangle** based on a hash of the mob name.

**Boss mobs** render at 48×48 pixels. Normal mobs render at 32×32 pixels.

---

## Mob AI Behavior

### AI States

| State | Description |
|-------|-------------|
| **Idle** | Stationary at home position |
| **Roam** | Wander within roam radius (40% of spawn zone radius) |
| **Chase** | Pursuit when player enters aggro range |
| **ChaseMemory** | Continue to last-known player position |
| **Attack** | Attack when within attack range |
| **ReturnHome** | Retreat when player leaves leash radius |

### Distance-Based Tick Scaling (DEAR)

Mobs tick at reduced frequency when far from any player:

| Distance (tiles) | Tick Interval | Effective FPS |
|------------------:|:-------------|:-------------|
| 3 | ~0.02s | Every frame |
| 10 | ~0.2s | 5 fps |
| 20 | ~0.8s | 1.25 fps |
| 48+ | Dormant | 0 fps |

### Mob Attack Calculation

1. **Hit check:** `CombatSystem::rollToHit(mobLevel, mobHitRate, playerLevel, playerEvasion)`
2. **Damage roll:** random in `[scaledDamage × 0.8, scaledDamage × 1.2]`
3. **Crit check:** if `random() < critRate` → damage × 1.95
4. **Reduction:** armor or magic resist reduction applied
5. **Apply:** damage to player HP, broadcast `SvCombatEventMsg`

---

## Respawn System

When a mob dies:
1. `ServerSpawnManager::tick()` detects `isAlive == false`
2. Sets `respawnAt = gameTime + respawnSeconds`
3. Unregisters entity from replication (clients see `SvEntityLeave`)
4. Destroys server entity
5. After `respawnSeconds` elapses, creates a new mob entity at a random position within the same zone
6. New mob gets a fresh random level within `[min_spawn_level, max_spawn_level]`

**Respawn time** is controlled by:
- `mob_definitions.respawn_seconds` (default per mob type)
- `spawn_zones.respawn_override_seconds` (per-zone override, -1 = use mob default)

---

## SQL Templates

### Create a Basic Melee Mob
```sql
INSERT INTO mob_definitions (
    mob_def_id, display_name, base_hp, base_damage, base_armor,
    aggro_range, attack_range, leash_radius,
    move_speed, attack_speed, respawn_seconds,
    hp_per_level, damage_per_level, armor_per_level,
    base_xp_reward, xp_per_level,
    min_gold_drop, max_gold_drop, gold_drop_chance,
    min_spawn_level, max_spawn_level,
    loot_table_id
) VALUES (
    'forest_wolf', 'Forest Wolf', 80, 12, 2,
    4.0, 1.2, 6.0,
    1.2, 2.0, 30,
    8.0, 1.5, 0.3,
    30, 4,
    5, 15, 0.7,
    3, 7,
    'loot_forest_common'
);
```

### Create a Boss Mob
```sql
INSERT INTO mob_definitions (
    mob_def_id, display_name, base_hp, base_damage, base_armor,
    aggro_range, attack_range, leash_radius,
    move_speed, attack_speed, respawn_seconds,
    hp_per_level, damage_per_level, armor_per_level,
    base_xp_reward, xp_per_level,
    min_gold_drop, max_gold_drop, gold_drop_chance,
    min_spawn_level, max_spawn_level,
    is_boss, monster_type, honor_reward,
    loot_table_id
) VALUES (
    'elder_treant', 'Elder Treant', 5000, 80, 15,
    6.0, 2.0, 10.0,
    0.8, 3.0, 3600,          -- 1 hour respawn
    200.0, 15.0, 2.0,
    500, 50,
    200, 500, 1.0,            -- guaranteed gold drop
    10, 10,                    -- fixed level
    true, 'Boss', 50,         -- boss flag, honor reward
    'loot_elder_treant'
);
```

### View All Mobs for a Scene
```sql
SELECT md.mob_def_id, md.display_name, md.base_hp, md.base_damage,
       sz.zone_name, sz.target_count, sz.center_x, sz.center_y, sz.radius
FROM spawn_zones sz
JOIN mob_definitions md ON md.mob_def_id = sz.mob_def_id
WHERE sz.scene_id = 'WhisperingWoods'
ORDER BY md.min_spawn_level;
```

### Update AI Ranges
```sql
UPDATE mob_definitions
SET aggro_range = 5.0, attack_range = 1.5, leash_radius = 8.0
WHERE mob_def_id = 'forest_wolf';
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Mob not spawning | Check `mob_def_id` matches exactly (case-sensitive) between `mob_definitions` and `spawn_zones` |
| Mob attacking from too far | Reduce `attack_range` in DB. Remember: DB is in tiles, engine multiplies ×32 |
| Mob not chasing | Check `aggro_range` is > 0. Check `is_aggressive` isn't explicitly false |
| Mob stuck / not moving | Check `move_speed` > 0. Check `leash_radius` > `aggro_range` |
| Wrong sprite showing | Sprite filename must be `mob_` + exact `display_name` + `.png` |
| No XP on kill | Check `base_xp_reward` > 0 |
| No gold drop | Check `gold_drop_chance` > 0 (it's 0-1, not a percentage) |
| Changes not taking effect | **Restart the server** — mob defs are cached on startup |
| Mob HP too high/low at spawn | Check `hp_per_level` and the level range in `spawn_zones` |

---

## Current Mob Counts (March 2026)

- **73 mob definitions** in `mob_definitions` table
- **6 spawn zones** in `spawn_zones` table (all in WhisperingWoods)
- **quest_definitions** table is **EMPTY** — quests not yet populated
