# Spawn Zone Creation Guide
## C++ Engine — Database-Driven Spawn Zones

> **Spawn zones are 100% database-driven.** One SQL INSERT per zone — no editor setup, no prefabs, no scene objects.

---

## Quick Start

### Step 1: Ensure Mob Exists in Database

The mob must already exist in `mob_definitions`. See [MOB_CREATION_GUIDE.md](MOB_CREATION_GUIDE.md).

### Step 2: INSERT into spawn_zones

```sql
INSERT INTO spawn_zones (
    scene_id, zone_name, center_x, center_y, radius,
    mob_def_id, target_count, respawn_override_seconds
) VALUES (
    'WhisperingWoods',       -- Scene name (must match scene system)
    'Spider Nest',           -- Zone display name (for admin/debug)
    200, -100,               -- Center position in PIXELS
    120,                     -- Spawn radius in PIXELS
    'forest_spider',         -- mob_def_id from mob_definitions
    4,                       -- How many to maintain alive
    NULL                     -- NULL = use mob's default respawn time
);
```

### Step 3: Restart Server

Spawn zones are cached on startup. Restart the server to load new zones.

---

## spawn_zones Table Reference

| Column | Type | Required | Description |
|--------|------|----------|-------------|
| `zone_id` | SERIAL | Auto (PK) | Auto-increment ID |
| `scene_id` | VARCHAR(64) | **Yes** | Which scene this zone belongs to |
| `zone_name` | VARCHAR(128) | **Yes** | Human-readable name for admin/debug |
| `center_x` | REAL | **Yes** | Zone center X position **in pixels** |
| `center_y` | REAL | **Yes** | Zone center Y position **in pixels** |
| `radius` | REAL | **Yes** | Spawn area radius **in pixels** |
| `mob_def_id` | VARCHAR(64) | **Yes** | Which mob to spawn (FK to `mob_definitions`) |
| `target_count` | INTEGER | **Yes** | How many of this mob to keep alive |
| `respawn_override_seconds` | INTEGER | No (NULL) | Override mob's default respawn time. NULL = use `mob_definitions.respawn_seconds` |

### Important: Coordinates Are in PIXELS

Unlike `mob_definitions` (which stores AI radii in tiles), `spawn_zones` uses **pixel coordinates** directly:

```
center_x, center_y = pixel position in the world
radius = pixel radius for the spawn area
```

The engine's coordinate system is 32 pixels per tile:
- A zone at `center_x = 160` is at tile X = 5
- A zone with `radius = 128` covers a 4-tile radius

---

## How Spawning Works

### On Server Startup

```
ServerSpawnManager.initialize("WhisperingWoods"):
│
├─ SpawnZoneCache.getZonesForScene("WhisperingWoods")
│  → Returns all spawn_zones rows for this scene
│
└─ For each zone row:
   ├─ Look up CachedMobDef by mob_def_id
   ├─ Determine respawn time:
   │   if respawn_override_seconds >= 0: use it
   │   else: use mob_definitions.respawn_seconds
   │
   └─ Spawn target_count mobs:
      ├─ Pick random level in [min_spawn_level, max_spawn_level]
      ├─ Pick random position within zone radius
      │   (uniform random in rectangle [center ± radius])
      ├─ Create Entity with:
      │   ├─ Transform (position, depth=1)
      │   ├─ EnemyStatsComponent (scaled from CachedMobDef)
      │   ├─ MobAIComponent (radii: tiles × 32 → pixels)
      │   └─ MobNameplateComponent (display_name, level, isBoss)
      └─ Register with ReplicationManager
```

### During Gameplay

`ServerSpawnManager::tick()` runs every server tick (50ms):
1. Checks each tracked mob — is the entity still alive?
2. If dead: marks `alive = false`, schedules `respawnAt = gameTime + respawnSeconds`
3. Unregisters dead entity from replication, destroys it
4. When `respawnAt` expires: creates a new mob at a random position in the same zone

### Mob Roaming

Mobs roam within **40% of the zone's radius** from their spawn point:
```
roamRadius = zone.radius × 0.4
```

So a zone with `radius = 200px` gives mobs a `80px` roam area around their home position.

---

## Zone Design Guidelines

### Zone Sizing

| Zone Type | Radius (px) | Approx Tiles | Use Case |
|-----------|------------:|:-------------|----------|
| Small cluster | 80-120 | 2.5-3.75 | Tight mob pack, boss arena |
| Standard zone | 120-200 | 3.75-6.25 | Normal field area |
| Large zone | 200-400 | 6.25-12.5 | Spread-out wilderness |
| Huge zone | 400-600 | 12.5-18.75 | Open plains, desert |

### Target Count Guidelines

| Mob Type | target_count | Reasoning |
|----------|:------------:|-----------|
| Common mobs | 3-8 | Enough for grinding, not overwhelming |
| Uncommon mobs | 2-4 | Scarcer, slightly harder |
| Elite mobs | 1-2 | Challenging encounters |
| Bosses | 1 | Single boss per zone |

### Multiple Zones Per Scene

Use multiple `spawn_zones` rows with the same `scene_id` to create level-graded areas:

```sql
-- Starter area: easy mobs near spawn
INSERT INTO spawn_zones VALUES (DEFAULT, 'WhisperingWoods', 'Starter Meadow', 0, 0, 150, 'squirrel', 5, NULL);

-- Mid area: tougher mobs
INSERT INTO spawn_zones VALUES (DEFAULT, 'WhisperingWoods', 'Forest Edge', -100, 50, 120, 'timber_wolf', 3, NULL);

-- Deep forest: challenging mobs
INSERT INTO spawn_zones VALUES (DEFAULT, 'WhisperingWoods', 'Deep Woods', -250, 100, 100, 'grizzly_bear', 2, NULL);

-- Boss: rare spawn, long respawn
INSERT INTO spawn_zones VALUES (DEFAULT, 'WhisperingWoods', 'Ancient Grove', -300, 150, 60, 'elder_treant', 1, 3600);
```

### Zone Overlap

Zones CAN overlap. Mobs from different zones will coexist in overlapping areas. This is useful for creating density gradients:

```
Zone A (radius 200) ─── Overlapping center ─── Zone B (radius 200)
  (weak mobs)            (both mob types)         (strong mobs)
```

---

## Respawn Time Guidelines

| Mob Type | respawn_seconds | Notes |
|----------|:---------------:|-------|
| Normal mobs | 30 | Standard, keeps grinding smooth |
| Minibosses | 300-900 | 5-15 minutes |
| Field bosses | 3600-7200 | 1-2 hours |
| Raid bosses | 43200-86400 | 12-24 hours |

Use `respawn_override_seconds` in `spawn_zones` to override per-zone:
```sql
-- Same mob, different respawn times in different zones
INSERT INTO spawn_zones VALUES (DEFAULT, 'Zone1', 'Pack A', 0, 0, 100, 'wolf', 3, 30);   -- 30s respawn
INSERT INTO spawn_zones VALUES (DEFAULT, 'Zone2', 'Pack B', 0, 0, 100, 'wolf', 2, 120);  -- 2min respawn (harder area)
```

---

## Current Spawn Zones (March 2026)

```sql
SELECT zone_id, scene_id, zone_name, mob_def_id, target_count, center_x, center_y, radius
FROM spawn_zones ORDER BY scene_id, zone_id;
```

| zone_id | scene_id | zone_name | mob_def_id | count | center | radius |
|--------:|----------|-----------|------------|------:|-------:|-------:|
| 1 | WhisperingWoods | Starter Meadow | squirrel | 3 | (0, 0) | 150 |
| 2 | WhisperingWoods | Forest Edge | giant_rat | 3 | (-100, 50) | 120 |
| 3 | WhisperingWoods | Woodland Path | horned_hare | 3 | (100, -80) | 130 |
| 4 | WhisperingWoods | Dense Thicket | timber_wolf | 2 | (-200, 100) | 100 |
| 5 | WhisperingWoods | Bear Den | grizzly_bear | 2 | (200, 150) | 90 |
| 6 | WhisperingWoods | Alpha Territory | timber_alpha | 1 | (-250, 200) | 80 |

**Only 1 scene has spawn zones.** Additional scenes need zones added to the database.

---

## SQL Templates

### Add a Complete Zone with Multiple Mob Types

```sql
-- New scene: Crystal Caves (Lv10-20)
INSERT INTO spawn_zones (scene_id, zone_name, center_x, center_y, radius, mob_def_id, target_count) VALUES
    ('CrystalCaves', 'Cave Entrance',    0,    0, 150, 'cave_bat',         5),
    ('CrystalCaves', 'Fungal Grotto',  -150,  80, 120, 'mushroom_lurker',  4),
    ('CrystalCaves', 'Crystal Chamber', -300, 150, 100, 'crystal_golem',    3),
    ('CrystalCaves', 'Deep Shaft',     -400, 200,  80, 'cave_spider',      4),
    ('CrystalCaves', 'Boss Chamber',   -500, 250,  60, 'crystal_guardian',  1);

-- Override boss respawn to 30 minutes
UPDATE spawn_zones SET respawn_override_seconds = 1800
WHERE scene_id = 'CrystalCaves' AND mob_def_id = 'crystal_guardian';
```

### View Zone Layout for a Scene

```sql
SELECT zone_name, mob_def_id, target_count,
       center_x, center_y, radius,
       COALESCE(respawn_override_seconds, md.respawn_seconds) as respawn_sec
FROM spawn_zones sz
JOIN mob_definitions md ON md.mob_def_id = sz.mob_def_id
WHERE sz.scene_id = 'WhisperingWoods'
ORDER BY sz.zone_id;
```

### Delete a Zone

```sql
DELETE FROM spawn_zones WHERE zone_id = 6;
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Mobs not spawning | Check `mob_def_id` matches exactly (case-sensitive) between tables |
| Mobs spawning in wrong place | `center_x`/`center_y` are in **pixels**, not tiles. Multiply tile coords by 32 |
| Too many mobs | Reduce `target_count` |
| Mobs spawning outside visible area | Check `center_x`/`center_y` are within the scene's world bounds |
| Boss respawning too fast | Set `respawn_override_seconds` on the spawn zone row |
| Mob levels wrong | Check `min_spawn_level`/`max_spawn_level` in `mob_definitions` |
| Changes not taking effect | **Restart the server** — spawn zones are cached on startup |
| Zone not loading for scene | Check `scene_id` matches the scene name exactly (e.g., `WhisperingWoods` not `Whispering Woods`) |

---

## Key Differences from Unity Prototype

| Feature | Unity (Mirror) | C++ Engine |
|---------|---------------|------------|
| Zone definition | Scene GameObject + NetworkSpawnZone component | SQL row in `spawn_zones` table |
| Zone bounds | BoxCollider2D (trigger) | Circle: center_x/y + radius |
| Mob prefabs | 3 generic prefabs (Melee/Ranged/Boss) | No prefabs — entities created from DB |
| Sprite loading | `Resources/Sprites/Mobs/{enemyID}/` folder | `assets/sprites/mob_{display_name}.png` |
| AI configuration | Inspector values as fallback | 100% from DB |
| Hot reload | Enter Play Mode | Restart server |
| Coordinate system | Unity world units | Pixels (32px per tile) |
| AI radii storage | Tiles in DB | Tiles in DB, ×32 conversion in code |
