# Server-Side Mob→Player Combat — Design Spec

**Date:** 2026-03-19
**Status:** Approved

## Overview

Mobs already chase, aggro, and attack players on the server. `MobAISystem::resolveAttack()` applies damage via `CharacterStats::takeDamage()`. The ONLY gaps are: (1) no network broadcast when mobs hit players, (2) no `SvDeathNotifyMsg` when mob damage kills a player, (3) no hit/miss roll for mob attacks. This spec closes those gaps.

## What Already Works

- `MobAISystem::update()` ticks every mob via `world_.update(dt)` in server_app.cpp
- Mobs chase players, enter Attack mode, fire `onAttackFired` callback
- `resolveAttack()` rolls damage, applies armor/MR reduction, calls `takeDamage()`
- `CharacterStats::takeDamage()` reduces HP and calls `die(DeathSource::PvE)` when HP <= 0
- `die()` applies XP loss, sets `isDead = true`, fires `onDied` callback
- `SvCombatEventMsg` and `SvDeathNotifyMsg` packet types already exist
- Client `onCombatEvent` handler exists (currently log-only)

## Changes Required

### 1. Add hit/miss roll to resolveAttack()

In `MobAISystem::resolveAttack()` (mob_ai_system.h:362), before damage calculation, add:
```
bool hit = CombatSystem::rollToHit(mobStats.level, mobStats.mobHitRate, playerStats.level, 0);
if (!hit) → fire onMobMissed callback, return
```

Matches Unity's `CombatHitRateSystem.RollMobVsPlayerHit()`. Mob hit rates by type:
- Normal: 10, MiniBoss: 12, Boss: 16, RaidBoss: 20

### 2. Add onMobAttackResolved callback to MobAISystem

```cpp
// Fired after each mob→player attack resolves (hit or miss)
std::function<void(Entity* mob, Entity* player, int damage, bool isCrit, bool isKill, bool isMiss)> onMobAttackResolved;
```

Called at the end of `resolveAttack()` with the final damage (0 if missed). Server_app.cpp wires this to broadcast `SvCombatEventMsg`.

### 3. Wire callback in server_app.cpp

After MobAISystem is obtained from the world, wire the callback:
```
mobAISystem->onMobAttackResolved = [this](Entity* mob, Entity* player, int damage, bool isCrit, bool isKill, bool isMiss) {
    // Get persistent IDs for both entities
    // Build and broadcast SvCombatEventMsg
    // If isKill: send SvDeathNotifyMsg to the killed player's client
};
```

### 4. Send SvDeathNotifyMsg on mob-caused death

When `isKill == true` in the callback:
- Calculate XP lost from `CharacterStats::getXPLossPercent(level)`
- Build `SvDeathNotifyMsg` with `deathSource=0` (PvE), `respawnTimer=5.0`, `xpLost`, `honorLost=0`
- Send to the killed player's client (not broadcast — only the dead player needs it)

### 5. Re-enable isDead check on CmdRespawn

The server now knows when players die. Re-enable the validation:
```cpp
if (!sc->stats.isDead) {
    LOG_WARN("Server", "Client %d sent CmdRespawn but is not dead", clientId);
    break;
}
```

### 6. Enhance client onCombatEvent handler

Currently just logs. When the target is the local player:
- Show floating damage text at player position (reuse CombatActionSystem's spawnDamageText)
- Auto-target the attacking mob (TWOM-style: getting hit targets the attacker)

### 7. Crit roll for mobs

`EnemyStats` already has `critRate` (default 0.05). Roll crit in resolveAttack:
```
bool isCrit = CombatSystem::rollCrit(mobStats.critRate);
if (isCrit) rawDamage = (int)(rawDamage * 1.95f);
```

## Files Modified

| File | Change |
|------|--------|
| `game/systems/mob_ai_system.h` | Add hit roll, crit roll, callback, enhance resolveAttack() |
| `server/server_app.cpp` | Wire callback, broadcast SvCombatEventMsg, send SvDeathNotifyMsg on kill, re-enable isDead check |
| `game/game_app.cpp` | Enhance onCombatEvent to show damage on local player + auto-target attacker |

## Files NOT Modified (other session working on these)

- `game/shared/skill_manager.h/cpp` — skill execution system
- `game/shared/combat_system.h/cpp` — only calling existing methods
- `game/shared/character_stats.h/cpp` — only calling existing methods
- `game/systems/combat_action_system.h` — only calling existing public methods

## Security

- All damage calculated server-side (mob stats, hit roll, armor reduction)
- Client receives final damage amount only — cannot influence calculations
- Death notification only sent to the dead player's client
- isDead validation re-enabled on CmdRespawn

## Testing

- Existing 203 tests should still pass (no formula changes)
- Manual test: mob aggros player, deals damage, floating text appears, player dies, death overlay shows
