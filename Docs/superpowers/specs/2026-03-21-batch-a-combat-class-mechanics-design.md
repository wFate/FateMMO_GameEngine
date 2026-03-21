# Batch A: Combat & Class Mechanics — Design Spec

**Date:** 2026-03-21
**Scope:** Wire existing combat/class systems that have code but aren't connected to the game loop.

---

## 1. Fury Per-Hit Generation

### Problem
`CharacterStats` has `addFury()`, `furyPerBasicAttack` (0.5f), `furyPerCriticalHit` (1.0f), and `furyOnHit` per skill definition. The combat flow in `ServerApp::processAction()` never calls `addFury()` for auto-attacks, and Warriors don't gain fury when taking damage.

### Design

**Auto-attack fury (PvE and PvP):**
- In `processAction()`, after confirming damage > 0 on both PvE (~line 2851) and PvP (~line 3042) paths, call:
  ```cpp
  charStats->stats.addFury(isCrit ? charStats->stats.furyPerCriticalHit : charStats->stats.furyPerBasicAttack);
  ```
- Fury is only added when the attack lands (damage > 0), not on miss/block/dodge.
- **Mage exclusion:** Guard fury generation with `charStats->stats.primaryResource == ResourceType::Fury`. Mages use `ResourceType::Mana` and have `maxFury = 0`, so this naturally excludes them. The guard is explicit rather than relying on `addFury()` clamping to zero.

**Skill fury generation:**
- Already wired at `skill_manager.cpp:786-787` via `furyOnHit` field on skill definitions. Verify this fires correctly during testing.

**Warrior fury on damage received:**
- Add `float furyPerDamageReceived = 0.0f` field to `CharacterStats` (populated from class definition data: 0.2f for Warriors, 0.0f for Rangers/Mages).
- In `CharacterStats::takeDamage()`, after applying damage and confirming damage > 0:
  ```cpp
  if (furyPerDamageReceived > 0.0f) {
      addFury(furyPerDamageReceived);
  }
  ```
- This is per TWOM: Warriors gain +0.2 fury per hit received; Rangers use fury but don't gain from taking hits.

**Fury capacity:** Keep existing values unchanged (base 3, +1 per 10 levels).

**Fury sync to client:** After fury changes from auto-attacks or damage received, call `sendPlayerState()` to push the updated `currentFury` value. Skill-path fury changes at `skill_manager.cpp` already trigger a state sync via the skill result flow. For auto-attacks, add `sendPlayerState()` after the `addFury()` call in `processAction()`. Batch the call — if both fury and HP change in the same tick, one `sendPlayerState()` suffices.

### Files to modify
- `server/server_app.cpp` — `processAction()` PvE and PvP paths
- `game/shared/character_stats.h` — Add `furyPerDamageReceived` field, modify `takeDamage()`
- `game/shared/game_types.h` — Verify class definitions set the field

---

## 2. Double-Cast Mage Mechanic

### Problem
Server-side validation is fully implemented (`skill_manager.cpp:385-387`). `activateDoubleCast()` fires after eligible skills. Free casts skip cooldown and resource cost. No work needed.

### Design
**No changes.** The mechanic is intentionally hidden — no client UI indicator, no network notification of double-cast readiness. Players discover it through experimentation. Server validates silently.

### Verification only
- Existing tests in `tests/test_double_cast.cpp` cover the SkillManager API: activate, consume, expiry, default values (5 test cases).
- Add one end-to-end test: define two skills (Flare with `enablesDoubleCast=true`, Ice Lance eligible for double-cast), verify that casting Flare then Ice Lance within the window results in zero MP cost and no cooldown applied, and that casting after expiry costs normally.

---

## 3. Boss Damage Competition Notification

### Problem
`EnemyStats::getTopDamagerPartyAware()` determines loot ownership on mob death. It's called at `server_app.cpp:2624` (skill kill path) and `server_app.cpp:2899` (auto-attack kill path). No notification is sent to players about who won.

### Design

**New message type: `SvBossLootOwnerMsg`**
- `uint16_t bossDefId` — mob definition ID (client looks up display name from cached mob defs)
- `std::string winnerName` — character name of the individual top damager (even if party won)
- `int32_t topDamage` — damage dealt by winner/winning group
- `uint8_t wasParty` — 1 if winning group was a party

Uses `std::string` + `writeString()`/`readString()` to match existing protocol conventions.

**Trigger conditions:**
- Mob dies AND `enemyStats->stats.monsterType != "Normal"` (covers Boss, MiniBoss, RaidBoss, Elite without needing a separate `isElite` field — `monsterType` is already populated from DB)
- Sent to all clients in the same scene as the dead mob

**Integration point:**
- After `getTopDamagerPartyAware()` returns at both kill paths, before loot spawning
- Look up winner's character name from the session map
- If winner has disconnected, skip notification (don't crash on missing session)
- Broadcast to all clients in same scene

**Client handling:**
- Display as system chat message: "[BossName] defeated! Top damager: [PlayerName] (X damage)"

### Files to modify
- `engine/net/protocol.h` — Add `SvBossLootOwnerMsg` struct and packet type
- `engine/net/game_messages.h` — Message serialization
- `server/server_app.cpp` — Broadcast after boss kill at both paths
- Client game_app.cpp — Handle message, format chat notification

---

## 4. PK Name Color on Remote Players

### Problem
Local player PK nameplate colors work — `pkStatusColor()` maps White/Purple/Red/Black to colors, applied via `syncNameplate()`. But PK status is sent only in `SvPlayerStateMsg` (local player). Remote players' nameplates don't reflect their PK status because `SvEntityEnterMsg` / `SvEntityUpdateMsg` don't carry `pkStatus`.

### Design

**Add pkStatus to entity replication:**
- Add `uint8_t pkStatus` field to `SvEntityEnterMsg` — conditionally serialized inside `if (entityType == 0)` block (player only), matching existing pattern for type-specific fields
- Add `pkStatus` as bit 14 in `SvEntityUpdateMsg` fieldMask — only written when pkStatus differs from last-acked state
- **Bitmask capacity:** After this change, bits 0-14 are used, leaving 1 reserved bit. If future features need more update fields, expand `fieldMask` to `uint32_t` at that time
- In `replication.cpp` `sendDiffs()`: add delta comparison `if (current.pkStatus != last.pkStatus) dirtyMask |= (1 << 14);`
- Server populates from `charStats->stats.pkStatus` when building enter/update messages in `buildEnterMessage()` and `buildCurrentState()`

**Client-side:**
- On `SvEntityEnter` for player entities: set `nameplate->nameColor = pkStatusColor(static_cast<PKStatus>(msg.pkStatus))`
- On `SvEntityUpdate` with bit 14 set: update the nameplate color
- Existing `pkStatusColor()` function handles the color mapping (White, Purple, Red, Dark Gray)
- Cold-start case: when a player connects and receives entity enter messages for existing players, those messages include their current pkStatus — no special handling needed

**Replication efficiency:**
- pkStatus changes are rare (only on PvP engagement/kill/death) so adding to the update bitmask has negligible bandwidth cost
- Only replicate for player entities, skip for mobs

### Files to modify
- `engine/net/protocol.h` — Add pkStatus to `SvEntityEnterMsg` (conditional on entityType==0), add bit 14 to update mask
- `engine/net/replication.cpp` — Populate pkStatus in `buildEnterMessage()`, `buildCurrentState()`, and add delta check in `sendDiffs()`
- Client entity handling — Apply pkStatusColor on remote player enter/update

---

## Testing Plan

| Test | Validates |
|---|---|
| Warrior auto-attacks mob, fury increases by 0.5 | Fury on PvE auto-attack |
| Warrior crits mob, fury increases by 1.0 | Fury on crit |
| Warrior takes damage from mob, fury increases by 0.2 | Fury on damage received |
| Ranger auto-attacks mob, fury increases by 0.5 | Ranger fury gen |
| Ranger takes damage, fury does NOT increase | Ranger no fury on hit taken |
| Mage auto-attacks, fury does NOT increase | Mage has no fury |
| Warrior at max fury crits, fury stays at max | Fury cap overflow |
| Warrior auto-attack kills mob, fury still increases | Fury on kill |
| Warrior PvP auto-attack, fury increases | Fury on PvP hit |
| Skill with furyOnHit generates correct fury | Skill fury gen |
| Flare → Ice Lance within window: free cast | Double-cast validation |
| Flare → wait → Ice Lance: normal cost | Double-cast expiry |
| Boss dies, SvBossLootOwnerMsg sent with correct winner | Boss competition |
| Elite dies, notification sent | Elite notification |
| Regular mob dies, no notification | Non-boss silent |
| Boss dies, top damager disconnected, no crash | Disconnected winner |
| Remote player goes purple, nameplate turns purple | PK color replication |
| Remote player PK status changes, nameplate updates | PK color update |
| Mobs don't carry pkStatus in entity messages | Mob optimization |
