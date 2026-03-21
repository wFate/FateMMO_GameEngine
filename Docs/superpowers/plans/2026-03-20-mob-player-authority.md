# Server-Authoritative Mob→Player Combat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove client-side mob→player damage application so the server is the sole authority for all combat damage. Prevents modified clients from becoming invincible.

**Architecture:** The server already runs MobAISystem and broadcasts SvCombatEventMsg with authoritative damage. The client's `resolveAttack()` currently also applies damage locally. We remove the client-side `takeDamage()` call and update the `onCombatEvent` handler to show floating damage text for mob→player attacks (currently suppressed for "local" attacks). Two files change, zero server changes.

**Tech Stack:** C++20, existing MobAISystem, existing SvCombatEventMsg

**Build command:** `"C:/Program Files/Microsoft Visual Studio/2025/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build`

**Test command:** `./build/Debug/fate_tests.exe`

**IMPORTANT:** Before building, `touch` every edited `.cpp` file (CMake misses changes silently on this setup).

---

## File Map

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `game/systems/mob_ai_system.h:374-427` | Remove `takeDamage()` from client-side `resolveAttack()` |
| Modify | `game/game_app.cpp:899-958` | Fix `onCombatEvent` to show damage text for mob→player attacks |

---

### Task 1: Remove client-side damage application from resolveAttack()

**Files:**
- Modify: `game/systems/mob_ai_system.h:374-427`

The `resolveAttack()` method runs on BOTH client and server. On the server, the damage application is correct and the callback broadcasts `SvCombatEventMsg`. On the client, we need to:
- Keep the hit/miss roll (for animation timing)
- Keep the callback invocation (for visual feedback)
- **Remove** the `playerStats.takeDamage(finalDamage)` call (line 420)
- **Remove** the kill check that depends on local HP (lines 421)

- [ ] **Step 1: Guard takeDamage with server-only check**

In `game/systems/mob_ai_system.h`, find the `resolveAttack()` method (line 374). The section starting at line 418 currently reads:

```cpp
        // Apply damage to player
        bool wasDead = playerStats.isDead;
        playerStats.takeDamage(finalDamage);
        bool isKill = !wasDead && playerStats.isDead;

        // Fire callback for network broadcast
        if (onMobAttackResolved) {
            onMobAttackResolved(mobEntity, playerEntity, finalDamage, isCrit, isKill, false);
        }
```

Replace with:

```cpp
        // On the server, apply damage and check for kill.
        // On the client, skip damage application — wait for SvCombatEventMsg from server.
        bool isKill = false;
#ifndef FATEMMO_CLIENT_ONLY
        bool wasDead = playerStats.isDead;
        playerStats.takeDamage(finalDamage);
        isKill = !wasDead && playerStats.isDead;
#endif

        // Fire callback (server: broadcasts SvCombatEventMsg; client: visual feedback)
        if (onMobAttackResolved) {
            onMobAttackResolved(mobEntity, playerEntity, finalDamage, isCrit, isKill, false);
        }
```

**WAIT** — There's a simpler approach. The game executable (FateEngine) is always the client. The server executable (FateServer) is always the server. The `MobAISystem` header is shared code compiled into both. But looking at the architecture more carefully:

- **FateServer** runs `MobAISystem` with `onMobAttackResolved` wired to broadcast.
- **FateEngine** (client) also runs `MobAISystem` for local mob AI simulation. Its `onMobAttackResolved` is NOT wired in game_app.cpp (only the server wires it).

So the actual fix is even simpler: the client's `resolveAttack()` calls `takeDamage()` regardless of whether a callback is set. We just need to guard the `takeDamage()` call behind the callback check — if no callback is set (client), skip damage. But that's fragile.

The cleanest approach: **add a `serverAuthority_` flag** to MobAISystem. When true (server), apply damage. When false (client), skip damage but still fire the callback for animations.

Actually, the simplest and most robust approach: just check if the `onMobAttackResolved` callback is set. On the server, it's wired (server_app.cpp:133). On the client, it's NOT wired. Use this as the authority signal:

Replace lines 418-426 with:

```cpp
        // Apply damage only on server (where onMobAttackResolved is wired).
        // Client receives authoritative damage via SvCombatEventMsg instead.
        bool isKill = false;
        if (onMobAttackResolved) {
            // Server path: apply damage, check kill, broadcast
            bool wasDead = playerStats.isDead;
            playerStats.takeDamage(finalDamage);
            isKill = !wasDead && playerStats.isDead;
            onMobAttackResolved(mobEntity, playerEntity, finalDamage, isCrit, isKill, false);
        }
```

This is safe because the client never sets `onMobAttackResolved`. Verify by searching for assignments to `onMobAttackResolved` — it should only appear in `server_app.cpp`.

Also apply the same pattern to the MISS path. Find lines 391-395:

```cpp
        if (!hit) {
            if (onMobAttackResolved) {
                onMobAttackResolved(mobEntity, playerEntity, 0, false, false, true);
            }
            return;
        }
```

This is already correct — it only fires the callback if set. No change needed here.

Also check `processDeferredAttacks()` (line 275-295). This runs the same `takeDamage()` on deferred attacks. It needs the same guard. Find line 293:

```cpp
            playerStatsComp->stats.takeDamage(finalDamage);
```

This function is only called from the server's tick (it processes fiber-deferred attacks). The implementer should verify this by searching for all call sites of `processDeferredAttacks()`. If it's only called from server code, no change needed. If it's called from client code too, add the same `onMobAttackResolved` guard.

- [ ] **Step 2: Touch, build, verify compilation**

```bash
touch game/systems/mob_ai_system.h
find . -name "*.cpp" -not -path "./build/*" -not -path "./out/*" -exec touch {} +
```
Build. Expected: compiles cleanly.

- [ ] **Step 3: Run tests**

`./build/Debug/fate_tests.exe`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add game/systems/mob_ai_system.h
git commit -m "fix: remove client-side mob damage application, server is sole authority"
```

---

### Task 2: Fix onCombatEvent to show mob→player damage text

**Files:**
- Modify: `game/game_app.cpp:899-958`

Currently, the `onCombatEvent` handler uses `isLocalAttack` to decide whether to show floating damage text. The logic is: if the attacker is NOT in `ghostEntities_`, it's "our" attack, so skip the text (because CombatActionSystem already showed predicted text).

But for mob→player attacks, the attacker IS in `ghostEntities_` (mobs are ghosts), so `isLocalAttack = false`, and the text DOES show. **This already works correctly for mob→player.**

However, we need to verify that the HP application path (lines 927-934) works for both mob→player and player→mob scenarios. Currently it checks if `targetId` is NOT in `ghostEntities_` (meaning it's the local player) and applies damage directly:

```cpp
                    if (msg.damage > 0 && sc->stats.isAlive()) {
                        sc->stats.currentHP = (std::max)(0, sc->stats.currentHP - msg.damage);
                        if (sc->stats.currentHP <= 0) {
                            sc->stats.die(DeathSource::PvE);
                        }
                    }
```

This is the CORRECT path — it applies server-authoritative damage to the local player. With Task 1 removing the client-side `takeDamage()`, this becomes the **only** place the local player takes mob damage. No changes needed here.

**BUT** — we should verify that mob→player combat events show floating damage text. The current code at line 943:

```cpp
        if (!isLocalAttack) {
```

When a mob (ghost entity) attacks the local player: `isLocalAttack = false` (mob IS in ghostEntities_), so we enter the block and show text. **This is correct.**

The one thing to fix: the comment at line 941 says "Local player already showed predicted damage text via CombatActionSystem" — this is true for player→mob but NOT for mob→player. Since we're removing client-side mob damage, mob→player combat events should ALWAYS show floating text from the server event. The current code already does this, but the comment is misleading.

- [ ] **Step 1: Update the comment for clarity**

In `game/game_app.cpp`, replace the comment block at lines 941-949:

```cpp
        // Show floating text only for OTHER players' attacks.
        // Local player already showed predicted damage text via CombatActionSystem.
        if (!isLocalAttack) {
```

With:

```cpp
        // Show floating damage text for:
        // - Mob→player attacks (mob is a ghost, isLocalAttack=false)
        // - Remote player→mob attacks (remote player is a ghost)
        // Skip for local player→mob (CombatActionSystem already showed predicted text).
        if (!isLocalAttack) {
```

- [ ] **Step 2: Touch, build, verify**

```bash
touch game/game_app.cpp
```
Build. Expected: compiles cleanly.

- [ ] **Step 3: Run tests**

`./build/Debug/fate_tests.exe`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add game/game_app.cpp
git commit -m "docs: clarify mob→player combat event handling comments"
```

---

### Task 3: Verify processDeferredAttacks is server-only

**Files:**
- Read: `game/systems/mob_ai_system.h:275-295`

- [ ] **Step 1: Search for all call sites of processDeferredAttacks()**

The implementer should search for `processDeferredAttacks` across the codebase. If it's only called from `server/server_app.cpp` (or files that only compile into FateServer), no change is needed — the deferred path is already server-only.

If it's called from `game/game_app.cpp` or other client code, it needs the same `onMobAttackResolved` guard added in Task 1.

- [ ] **Step 2: If client calls it, add guard**

In `processDeferredAttacks()`, wrap the `takeDamage` call:

```cpp
        if (onMobAttackResolved) {
            playerStatsComp->stats.takeDamage(finalDamage);
        }
```

- [ ] **Step 3: Commit if changes were made**

```bash
git add game/systems/mob_ai_system.h
git commit -m "fix: guard deferred mob attacks to server-only execution"
```

---

### Task 4: Full regression test

- [ ] **Step 1: Touch all modified files, rebuild**

```bash
touch game/systems/mob_ai_system.h game/game_app.cpp
```
Build.

- [ ] **Step 2: Run full test suite**

`./build/Debug/fate_tests.exe`
Expected: All tests pass.

- [ ] **Step 3: Manual verification notes**

To fully test this change requires a running server + client:
1. Connect client to server
2. Walk near mobs in WhisperingWoods
3. Let a mob attack you — damage should appear from `SvCombatEventMsg` (server), NOT from local `resolveAttack()`
4. Verify HP decreases correctly
5. Verify death triggers correctly when HP reaches 0
6. Verify respawn still works

**Note to user:** After implementing, restart FateServer.exe and test with the game client.
