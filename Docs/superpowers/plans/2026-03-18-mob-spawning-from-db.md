# Mob Spawning from DB Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the SpawnSystem to the MobDefCache so spawn zones reference real mob definitions from the database (73 mobs) instead of hardcoded test values.

**Architecture:** The server already loads `MobDefCache` with 73 mobs at startup. The `SpawnSystem` creates mobs via `EntityFactory::createMob()` using hardcoded stats. Change it to look up the mob definition from the cache by `mob_def_id` and use the DB values for HP, damage, armor, XP, AI behavior, loot table, etc. The `SpawnZoneComponent`'s `MobSpawnRule` already has an `enemyId` field — this becomes the key into `MobDefCache`. On the client side, the server replicates mob entities with stats already applied.

**Tech Stack:** C++20, existing ECS SpawnSystem, MobDefCache, EntityFactory

---

## File Map

| File | Change |
|---|---|
| `game/systems/spawn_system.h` | Modify: pass MobDefCache reference, use it when spawning |
| `game/entity_factory.h` | Modify: add overload that takes CachedMobDef instead of raw stats |
| `server/server_app.cpp` | Modify: pass MobDefCache to SpawnSystem on init |

---

### Task 1: Add CachedMobDef-Based Entity Creation

**Files:**
- Modify: `game/entity_factory.h`

- [ ] **Step 1: Add a createMobFromDef static method**

Add alongside the existing `createMob()`:

```cpp
static Entity* createMobFromDef(World& world, const CachedMobDef& def, int level,
                                 Vec2 position, bool isBoss = false);
```

This method creates a mob entity using all stats from the cached definition (HP scaling, damage scaling, armor, crit rate, attack speed, move speed, aggro/attack/leash ranges, XP reward, loot table, gold drops, honor reward). Falls back to existing `createMob()` pattern for the ECS entity assembly.

- [ ] **Step 2: Implement — use def fields instead of hardcoded values**

Key mappings:
- `def.getHPForLevel(level)` → mob HP
- `def.getDamageForLevel(level)` → mob damage
- `def.getArmorForLevel(level)` → mob armor
- `def.baseXPReward` → XP reward base
- `def.aggroRange` → MobAI acquire radius
- `def.attackRange` → MobAI attack range
- `def.leashRadius` → MobAI leash radius
- `def.lootTableId` → EnemyStats loot table
- `def.isAggressive` → MobAI passive vs aggressive
- `def.moveSpeed` → MobAI chase/roam speed
- `def.displayName` → nameplate

- [ ] **Step 3: Build, test**

- [ ] **Step 4: Commit**

---

### Task 2: SpawnSystem Uses MobDefCache

**Files:**
- Modify: `game/systems/spawn_system.h`

- [ ] **Step 1: Add MobDefCache pointer to SpawnSystem**

The SpawnSystem needs access to look up mob definitions. Add a `const MobDefCache*` member and setter.

- [ ] **Step 2: When spawning a mob, look up the definition**

In the spawn logic where `EntityFactory::createMob()` is called, check if `mobDefCache_` is set and the `enemyId` exists in it. If so, call `createMobFromDef()` instead. If not (cache not set or mob not found), fall back to existing hardcoded creation.

- [ ] **Step 3: Build, test**

- [ ] **Step 4: Commit**

---

### Task 3: Wire MobDefCache into ServerApp and GameApp

**Files:**
- Modify: `server/server_app.cpp` (server already has mobDefCache_, just pass it to spawn system)
- Modify: `game/game_app.cpp` (client-side — if spawn system runs client-side, pass cache there too; if server-only, skip)

- [ ] **Step 1: Check if SpawnSystem runs on server, client, or both**

Read `game/game_app.cpp` to see if SpawnSystem is registered as an ECS system on the client. If server-only, only wire server side.

- [ ] **Step 2: Pass MobDefCache to SpawnSystem after cache initialization**

- [ ] **Step 3: Build, run server, verify mobs spawn with DB stats**

- [ ] **Step 4: Commit**

---

### Task 4: Update Docs

- [ ] **Step 1: Update ENGINE_STATE_AND_FEATURES.md**
- [ ] **Step 2: Commit**
