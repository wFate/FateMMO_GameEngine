# Server Authority Overhaul — Design Spec

**Date:** 2026-03-20
**Status:** Draft
**Scope:** Fix all client-authoritative gameplay state to be server-authoritative with client prediction

## Problem Statement

The game is designed as a server-authoritative 2D MMORPG targeting mobile. However, a full audit revealed that basic auto-attacks bypass the server entirely — the client calculates damage, determines kills, awards XP/gold/honor, and manages mob HP locally. This means a memory editor or modified client can: one-shot mobs, gain infinite XP, skip cooldowns, respawn instantly, and award arbitrary honor/gold.

Skills (`CmdUseSkill`) already go through the server correctly. The fix is to bring auto-attacks and all remaining state changes to the same standard.

## Design Principles

1. **Server is the single source of truth** for all gameplay state (HP, XP, gold, level, honor, inventory, death, cooldowns)
2. **Client predicts for responsiveness** — shows attack animations and predicted damage text immediately, but never modifies authoritative state
3. **Shared formulas ensure prediction matches** — `game/shared/combat_system.cpp` runs identically on client and server, so predicted damage matches server 99.9% of the time
4. **Corrections are invisible** — mob HP bars are driven by server replication; floating damage text is fire-and-forget from prediction. Small discrepancies self-correct on the next tick.

## Changes by Category

### 1. Auto-Attack Pipeline (CRITICAL)

**Current flow:** Client runs full combat in `CombatActionSystem` — calculates damage, applies to mob HP, determines death, awards XP/honor/gold. Server is never contacted.

**New flow:**

```
Client                                    Server
──────                                    ──────
Auto-attack timer fires
  ├─ Play attack animation
  ├─ Calculate predicted damage (shared formulas)
  ├─ Show predicted floating damage text
  ├─ Send CmdAction{targetPid, attackType=0}
  │                                         │
  │                                    Validate:
  │                                     - Player alive?
  │                                     - Target exists & alive?
  │                                     - In attack range?
  │                                     - Attack cooldown elapsed?
  │                                    Calculate damage (same shared formulas)
  │                                    Apply to mob HP (authoritative)
  │                                    If killed:
  │                                     - Award XP to killer
  │                                     - Roll loot, spawn drops
  │                                     - Notify SpawnManager for respawn
  │                                    Broadcast SvCombatEvent
  │                                         │
  ◄─────────────────────────────────────────┘
  │
  Mob HP bar updated via SvEntityUpdate (replication)
  SvCombatEvent used for: floating text (if prediction wasn't shown),
    kill notification, death animation trigger
```

**What CombatActionSystem KEEPS:**
- Target selection (click-to-target, tab-target)
- Attack cooldown timer display (visual only — server re-validates)
- Attack animation triggering
- Predicted damage text (immediate feedback)
- Sending `CmdAction` to server

**What CombatActionSystem LOSES (all removed):**
- `es.takeDamageFrom()` — client never modifies mob HP
- `ps.currentXP += xpReward` — only server awards XP
- `ps.honor += honorReward` — only server awards honor
- Mob death determination (only server determines via `isKill` in SvCombatEvent)
- Local loot/gold award logic
- Local `die()` calls from auto-attack damage
- Any direct modification of `CharacterStats` fields (HP, MP, XP, gold, honor, level)

**Prediction philosophy:** The floating damage text is a cosmetic prediction. The mob's HP bar is always driven by `SvEntityUpdate` from replication. If prediction shows "100" but server says "80", the floating text is already fading — the HP bar just shows the server value. No replacement, no double text.

### 2. Server-Side Auto-Attack Handler (NEW)

The server's `processAction()` already exists but needs hardening:

**Validations to add:**
- Player is alive (`!isDead`)
- Target entity exists and is alive
- Target is in attack range (distance check using player stats)
- Attack cooldown has elapsed (track `lastAutoAttackTime_` per client)
- Player and target are in the same scene

**On validation pass:**
- Calculate damage using `CombatSystem::calculateDamage()` (shared code)
- Roll hit/miss using `CombatSystem::rollToHit()` (shared code)
- Apply damage to mob's `EnemyStats`
- If killed: award XP, roll loot (already implemented in processAction kill path)
- Broadcast `SvCombatEvent` to all clients in scene
- Send `SvPlayerState` to update client's XP/HP/gold

### 3. Respawn: Server-Gated (HIGH)

**Current:** Client calls `stats.respawn()` locally AND sends `CmdRespawn` to server. Client is alive before server responds.

**New:**
- Client sends `CmdRespawn` only
- Death overlay stays visible
- Client waits for `SvRespawnMsg` from server
- On `SvRespawnMsg`: clear death state, set position, dismiss overlay
- Remove local `stats.respawn()` call from `deathOverlayUI_.onRespawnRequested`

### 4. Server Skill Cooldown Validation (HIGH)

**Current:** Server accepts any `CmdUseSkill` without checking cooldown. Client tracks cooldown locally.

**New:**
- Server maintains `std::unordered_map<std::string, float> lastSkillCastTime_` per client (keyed by skillId)
- On `CmdUseSkill`: look up skill's cooldown from `SkillDefCache` ranks, check `gameTime_ - lastCastTime >= cooldown`
- If too soon: silently drop (don't execute, don't broadcast)
- On successful cast: update `lastCastTime` for that skill

### 5. Full State Sync on Connect (MEDIUM)

After `ConnectAccept`, the server sends a burst of messages to fully initialize the client:

| Order | Message | Data | Status |
|---|---|---|---|
| 1 | `SvPlayerState` | HP, MP, XP, gold, level, fury | Already sent |
| 2 | `SvSkillSync` (new) | All learned skills, activated ranks, skill bar layout | **New message** |
| 3 | `SvQuestSync` (new) | Active quest IDs + per-objective progress | **New message** |
| 4 | `SvInventorySync` (new) | Full slot contents + equipment | **New message** |

**SvSkillSync message:**
```
skillCount: u16
for each skill:
  skillId: string
  unlockedRank: u8
  activatedRank: u8
skillBarCount: u8
for each slot:
  skillId: string (empty = unbound)
```

**SvQuestSync message:**
```
questCount: u16
for each quest:
  questId: string
  state: u8 (active/completed/failed)
  objectiveCount: u8
  for each objective:
    current: i32
    target: i32
```

**SvInventorySync message:**
```
slotCount: u16
for each slot:
  slotIndex: i32
  itemId: string
  quantity: i32
  enchantLevel: i32
  rolledStats: string (JSON)
  socketStat: string
  socketValue: i32
equipCount: u16
for each equipped:
  slot: u8 (EquipmentSlot enum)
  itemId: string
  ... (same fields as above)
```

Client applies these on receipt, replacing any local defaults. Guild, friends, pets, bounties remain on-demand (opened via UI → server fetches).

### 6. CharacterStats as Read-Only Mirror

After this overhaul, the client's `CharacterStats` is a **display-only copy** of server state. The only writers are:

| Writer | What it updates |
|---|---|
| `onPlayerState` callback | HP, maxHP, MP, maxMP, fury, XP, gold, level |
| `onCombatEvent` callback | Predicted local HP adjustment (for immediate feedback) |
| `onDeathNotify` callback | isDead flag |
| `onRespawn` callback | isDead=false, position |
| `onSkillSync` callback | Skill unlocks, ranks, bar |
| `onQuestSync` callback | Quest state |

**No gameplay system on the client directly modifies these fields.** The `CombatActionSystem`, `GameplaySystem`, and UI code become read-only consumers.

### 7. Auto-Attack Cooldown Tracking (Server)

Add per-client tracking:
```cpp
std::unordered_map<uint16_t, float> lastAutoAttackTime_;  // clientId -> gameTime
```

In `processAction()` for attackType==0:
```cpp
float attackSpeed = charStats->stats.getAttackSpeed();
float cooldown = 1.0f / attackSpeed;
if (gameTime_ - lastAutoAttackTime_[clientId] < cooldown) return; // too fast
lastAutoAttackTime_[clientId] = gameTime_;
```

### 8. PvP Auto-Attacks (CRITICAL — from review)

The spec above covers PvE (mob targets). The current `CombatActionSystem` also has a full PvP branch (lines 831-1004) that applies damage, block/resist rolls, lifesteal, fury, PK status — all client-side. The server's `processAction()` only handles mob targets today.

**New:** `processAction()` must also handle player targets:
- Detect target has `CharacterStatsComponent` (player) vs `EnemyStatsComponent` (mob)
- Apply PvP damage multiplier (0.05x from `CombatSystem::getPvPDamageMultiplier()`)
- Same-faction check (prevent same-faction PvP unless flagged)
- Server determines PK status transitions
- Server awards PvP honor/kills
- Broadcast `SvCombatEvent` for PvP hits
- Send `SvDeathNotifyMsg` for PvP kills

### 9. Honor/PvP Stats in SvPlayerStateMsg (from review)

Current `SvPlayerStateMsg` sends HP, MP, XP, gold, level, fury — but NOT honor, pvpKills, or pvpDeaths. Add these fields so the client can display them.

### 10. CombatActionSystem Network Wiring (from review)

`CombatActionSystem` currently has no reference to `NetClient`. It needs a send callback injected from `GameApp`:
```cpp
// In GameApp setup:
combatSystem_->onSendAttack = [this](uint64_t targetPid) {
    netClient_.sendAction(0, targetPid, 0); // attackType=0, skillId=0
};
```

### 11. Double Damage Text Suppression (from review)

After the overhaul, `onCombatEvent` from the server would show floating text for ALL combat events — including the local player's attacks (which already showed predicted text). Suppress server text when `attackerId` matches local player's PID.

### 12. Re-enable isDead Check in CmdRespawn (from review)

Line ~2004 of server_app.cpp has `// TODO: Re-enable isDead check`. This must be resolved — reject `CmdRespawn` if the player is not actually dead, preventing double-respawn exploits.

### 13. Server-Side Level-Up (from review)

Server currently does `currentXP += xp` without checking for level-up threshold. Must implement: if `currentXP >= xpToNextLevel`, increment level, recalculate stats, reset XP. Send updated level via `SvPlayerState`.

### 14. Respawn Button Pending State (from review)

When the player taps Respawn, the button should show "Respawning..." and disable until `SvRespawnMsg` arrives. Add a 5-second timeout to re-enable (retry), but never fall back to local respawn.

### 15. RNG Divergence Acknowledgment

Hit/miss/crit rolls use thread-local RNG and will diverge between client and server. This is cosmetically acceptable — predicted "100 damage" might become a server "MISS", but the HP bar (server-authoritative) is always correct. The floating text fades before anyone notices.

## Files Affected

| File | Changes |
|---|---|
| `game/systems/combat_action_system.h` | Strip all state-modifying code, keep prediction + CmdAction send |
| `server/server_app.cpp` | Harden processAction validation, add cooldown tracking, add skill cooldown tracking, add connect-time sync messages |
| `game/game_app.cpp` | Add handlers for SvSkillSync, SvQuestSync, SvInventorySync. Remove local respawn. Wire SvCombatEvent to update ghost mob HP |
| `engine/net/protocol.h` | Add SvSkillSync, SvQuestSync, SvInventorySync message structs |
| `engine/net/packet.h` | Add packet type constants |
| `engine/net/net_client.h/cpp` | Add callbacks + deserialization for new messages |
| `game/ui/death_overlay_ui.cpp` | Remove local respawn call, wait for server |
| `tests/` | Tests for: server attack validation, cooldown enforcement, XP award, state sync round-trip |

## What This Does NOT Change

- Skill execution (`CmdUseSkill`) — already server-authoritative
- Movement — already validated with speed checks
- Loot pickup — already server-validated
- Zone transitions — already server-gated
- Chat — already server-relayed
- Trade — already server-validated

## Success Criteria

1. A modified client that sends `CmdAction` with forged damage has no effect — server calculates its own
2. A client that skips cooldowns gets rejected by server
3. A client that calls `respawn()` locally has no effect — must wait for server
4. XP, gold, honor, level can ONLY increase via server messages
5. On connect, client has correct level, HP, XP, gold, skills, quests, inventory — all from server
6. Auto-attacks feel responsive (predicted damage shown immediately)
7. All 376+ existing tests still pass
8. New tests cover: attack validation, cooldown rejection, XP server-award, state sync
