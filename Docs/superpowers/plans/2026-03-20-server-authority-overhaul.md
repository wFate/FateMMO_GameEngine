# Server Authority Overhaul Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make all gameplay state server-authoritative — auto-attacks, XP, gold, honor, death, cooldowns, and inventory — with client-side prediction for responsiveness.

**Architecture:** Server validates and executes all combat. Client predicts damage for immediate feedback but never modifies authoritative state. New sync messages ensure client mirrors server state on connect. Shared `game/shared/combat_system.cpp` ensures prediction matches server 99.9% of the time.

**Tech Stack:** C++, custom ECS, custom UDP netcode, PostgreSQL, doctest

**Spec:** `Docs/superpowers/specs/2026-03-20-server-authority-overhaul-design.md`

**Build command:**
```bash
export LIB="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64"
export INCLUDE="C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Tools\\MSVC\\14.50.35717\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared"
CMAKE="/c/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
```
**CRITICAL:** `touch` ALL edited .cpp files before building (CMake misses changes silently). Use `powershell -Command "(Get-Item 'path').LastWriteTime = Get-Date"` to force timestamp updates.

**Test command:** `./out/build/x64-Debug/fate_tests.exe`

**Do NOT add Co-Authored-By lines to commits.**

---

## File Structure

| File | Role | Change type |
|---|---|---|
| `engine/net/packet.h` | Packet type constants | Modify: add 3 new constants |
| `engine/net/protocol.h` | Message structs | Modify: add honor to SvPlayerState, add 3 new structs |
| `engine/net/net_client.h` | Client callbacks | Modify: add 3 new callbacks |
| `engine/net/net_client.cpp` | Packet deserialization | Modify: add 3 new cases |
| `server/server_app.h` | Server state tracking | Modify: add cooldown maps |
| `server/server_app.cpp` | Server authority logic | Modify: harden processAction, add sync, add cooldowns |
| `game/systems/combat_action_system.h` | Client combat | Modify: strip state changes, add network send |
| `game/game_app.cpp` | Client message handlers | Modify: add sync handlers, suppress double text, wire combat send |
| `game/game_app.h` | Client state | Modify: add pending state for new sync messages |
| `game/ui/death_overlay_ui.h` | Death UI | Modify: add pending respawn state |
| `game/ui/death_overlay_ui.cpp` | Death UI render | Modify: show "Respawning..." state |
| `game/shared/character_stats.h` | Shared stats | Modify: ensure addXP handles level-up |
| `game/shared/character_stats.cpp` | Shared stats impl | Modify: addXP with level-up logic |
| `tests/test_server_authority.cpp` | New test file | Create |
| `tests/test_state_sync.cpp` | New test file | Create |

---

## Task 1: Add Honor to SvPlayerStateMsg

**Files:**
- Modify: `engine/net/protocol.h:272-305` (SvPlayerStateMsg)
- Modify: `server/server_app.cpp:2672-2680` (sendPlayerState fills msg)
- Modify: `game/game_app.cpp:945-965` (onPlayerState handler)
- Test: `tests/test_protocol.cpp`

- [ ] **Step 1: Write failing test for honor round-trip**

In `tests/test_protocol.cpp`, add test that SvPlayerStateMsg serializes/deserializes honor, pvpKills, pvpDeaths fields.

- [ ] **Step 2: Run test — expect FAIL** (fields don't exist yet)

- [ ] **Step 3: Add fields to SvPlayerStateMsg**

In `engine/net/protocol.h`, add to SvPlayerStateMsg struct:
```cpp
int32_t honor      = 0;
int32_t pvpKills   = 0;
int32_t pvpDeaths  = 0;
```
Add to write(): `w.writeI32(honor); w.writeI32(pvpKills); w.writeI32(pvpDeaths);`
Add to read(): `m.honor = r.readI32(); m.pvpKills = r.readI32(); m.pvpDeaths = r.readI32();`

- [ ] **Step 4: Update server sendPlayerState to fill new fields**

In `server/server_app.cpp` sendPlayerState(), add:
```cpp
msg.honor     = s.honor;
msg.pvpKills  = s.pvpKills;
msg.pvpDeaths = s.pvpDeaths;
```

- [ ] **Step 5: Update client onPlayerState to read new fields**

In `game/game_app.cpp` onPlayerState handler, add:
```cpp
stats->stats.honor = msg.honor;
stats->stats.pvpKills = msg.pvpKills;
stats->stats.pvpDeaths = msg.pvpDeaths;
```

- [ ] **Step 6: Run tests — expect PASS**

- [ ] **Step 7: Commit**
```
feat: add honor/pvp stats to SvPlayerStateMsg
```

---

## Task 2: Server-Side addXP with Level-Up

**Files:**
- Modify: `game/shared/character_stats.h:107` (addXP declaration)
- Modify: `game/shared/character_stats.cpp` (addXP implementation)
- Test: `tests/test_server_authority.cpp` (new file)

- [ ] **Step 1: Write failing test for server-side addXP with level-up**

Create `tests/test_server_authority.cpp`:
```cpp
#include <doctest/doctest.h>
#include "game/shared/character_stats.h"
using namespace fate;

TEST_CASE("addXP awards XP and triggers level-up") {
    CharacterStats s;
    s.level = 1;
    s.currentXP = 0;
    s.xpToNextLevel = 100;
    s.maxHP = 100;
    s.currentHP = 100;

    s.addXP(150); // 50 more than needed to level

    CHECK(s.level == 2);
    CHECK(s.currentXP == 50); // overflow carries
    CHECK(s.maxHP > 100);     // stats recalculated
}
```

- [ ] **Step 2: Run test — verify it fails or check current addXP behavior**

Read current `addXP()` in `character_stats.cpp`. If it already handles level-up with overflow, skip to step 4.

- [ ] **Step 3: Implement addXP with level-up**

In `game/shared/character_stats.cpp`, ensure `addXP()`:
```cpp
void CharacterStats::addXP(int64_t amount) {
    currentXP += amount;
    while (currentXP >= xpToNextLevel && xpToNextLevel > 0) {
        currentXP -= xpToNextLevel;
        level++;
        recalculateStats();
        recalculateXPRequirement();
    }
}
```

- [ ] **Step 4: Update server XP award paths to use addXP()**

In `server/server_app.cpp`, replace both instances of `currentXP += xp` (in processUseSkill kill path ~line 2281 and processAction kill path ~line 2486) with:
```cpp
casterStatsComp->stats.addXP(xp);
// or
charStats->stats.addXP(xp);
```

- [ ] **Step 5: Run tests — expect PASS**

- [ ] **Step 6: Commit**
```
feat: server-side addXP with level-up and overflow carry
```

---

## Task 3: Harden Server processAction Validation

**Files:**
- Modify: `server/server_app.h` (add lastAutoAttackTime_ map)
- Modify: `server/server_app.cpp:2411-2470` (processAction validation)
- Test: `tests/test_server_authority.cpp`

- [ ] **Step 1: Write failing tests for attack validation**

```cpp
TEST_CASE("Server rejects attack on dead mob") { /* ... */ }
TEST_CASE("Server rejects attack while player is dead") { /* ... */ }
TEST_CASE("Server rejects attack faster than cooldown") { /* ... */ }
```

These are unit-level tests using mock World + entities. Create player entity with CharacterStatsComponent, mob entity with EnemyStatsComponent. Call processAction logic directly or extract validation into a testable function.

- [ ] **Step 2: Add lastAutoAttackTime_ to server_app.h**

```cpp
std::unordered_map<uint16_t, float> lastAutoAttackTime_;
```

- [ ] **Step 3: Add validation checks at top of processAction()**

In `server/server_app.cpp` processAction(), before the damage calculation, add:
```cpp
// Validate player is alive
if (charStats && charStats->stats.isDead) return;

// Validate target is alive
if (enemyStats && !enemyStats->stats.isAlive) return;

// Validate attack cooldown
float attackSpeed = charStats ? charStats->stats.getAttackSpeed() : 1.0f;
float cooldown = (attackSpeed > 0.0f) ? (1.0f / attackSpeed) : 1.5f;
auto& lastTime = lastAutoAttackTime_[clientId];
if (gameTime_ - lastTime < cooldown * 0.8f) return; // 0.8x tolerance for latency
lastAutoAttackTime_[clientId] = gameTime_;
```

- [ ] **Step 4: Clean up lastAutoAttackTime_ on disconnect**

In `onClientDisconnected()`, add: `lastAutoAttackTime_.erase(clientId);`

- [ ] **Step 5: Run tests — expect PASS**

- [ ] **Step 6: Commit**
```
feat: server validates auto-attack cooldown, alive state, target state
```

---

## Task 4: Server Skill Cooldown Validation

**Files:**
- Modify: `server/server_app.h` (add skillCooldowns_ map)
- Modify: `server/server_app.cpp:2114-2150` (processUseSkill)
- Test: `tests/test_server_authority.cpp`

- [ ] **Step 1: Write failing test for skill cooldown rejection**

- [ ] **Step 2: Add per-client skill cooldown tracking**

In `server/server_app.h`:
```cpp
std::unordered_map<uint16_t, std::unordered_map<std::string, float>> skillCooldowns_;
```

- [ ] **Step 3: Validate cooldown in processUseSkill()**

At the top of `processUseSkill()`, after finding the skill definition:
```cpp
auto& clientCooldowns = skillCooldowns_[clientId];
auto cooldownIt = clientCooldowns.find(msg.skillId);
if (cooldownIt != clientCooldowns.end()) {
    const CachedSkillRank* rank = skillDefCache_.getRank(msg.skillId, msg.rank);
    float cooldown = rank ? rank->cooldownSeconds : 1.0f;
    if (gameTime_ - cooldownIt->second < cooldown * 0.8f) return; // reject
}
clientCooldowns[msg.skillId] = gameTime_;
```

- [ ] **Step 4: Clean up on disconnect**

In `onClientDisconnected()`: `skillCooldowns_.erase(clientId);`

- [ ] **Step 5: Run tests — expect PASS**

- [ ] **Step 6: Commit**
```
feat: server validates skill cooldowns, rejects spam
```

---

## Task 5: Server PvP Auto-Attack Path

**Files:**
- Modify: `server/server_app.cpp:2411-2470` (processAction — add player target branch)
- Test: `tests/test_server_authority.cpp`

- [ ] **Step 1: Write test for PvP damage through server**

- [ ] **Step 2: Add player-target branch in processAction()**

After the existing mob-target code, add:
```cpp
auto* targetCharStats = target->getComponent<CharacterStatsComponent>();
if (targetCharStats) {
    // PvP auto-attack
    int damage = charStats ? charStats->stats.calculateDamage(false, isCrit) : 10;
    damage = static_cast<int>(std::round(damage * CombatSystem::getPvPDamageMultiplier()));

    // Apply damage
    targetCharStats->stats.takeDamage(damage);
    bool killed = targetCharStats->stats.isDead;

    // Broadcast SvCombatEvent
    // ... (same pattern as mob combat event broadcast)

    // Handle PvP death
    if (killed) {
        // Award honor, pvpKills, send SvDeathNotifyMsg
    }
}
```

- [ ] **Step 3: Run tests — expect PASS**

- [ ] **Step 4: Commit**
```
feat: server-side PvP auto-attack with damage, death, honor
```

---

## Task 6: Re-enable isDead Check in CmdRespawn

**Files:**
- Modify: `server/server_app.cpp:2004-2006` (remove TODO, add isDead guard)
- Test: `tests/test_server_authority.cpp`

- [ ] **Step 1: Write test that double-respawn is rejected**

- [ ] **Step 2: Replace TODO with actual isDead check**

At `server/server_app.cpp:2004`, replace the TODO comment with:
```cpp
if (!sc->stats.isDead) {
    LOG_WARN("Server", "Client %d respawn rejected: not dead", clientId);
    break;
}
```

- [ ] **Step 3: Run tests — expect PASS**

- [ ] **Step 4: Commit**
```
fix: reject CmdRespawn when player is not dead
```

---

## Task 7: New Protocol Messages (SvSkillSync, SvQuestSync, SvInventorySync)

**Files:**
- Modify: `engine/net/packet.h:70` (add 3 packet type constants)
- Modify: `engine/net/protocol.h` (add 3 message structs)
- Modify: `engine/net/net_client.h` (add 3 callbacks)
- Modify: `engine/net/net_client.cpp:304-310` (add 3 cases in handlePacket)
- Test: `tests/test_state_sync.cpp` (new file)

- [ ] **Step 1: Write round-trip serialization tests for all 3 messages**

Create `tests/test_state_sync.cpp` with tests for SvSkillSyncMsg, SvQuestSyncMsg, SvInventorySyncMsg write/read.

- [ ] **Step 2: Add packet type constants**

In `engine/net/packet.h`:
```cpp
constexpr uint8_t SvSkillSync     = 0xA3;
constexpr uint8_t SvQuestSync     = 0xA4;
constexpr uint8_t SvInventorySync = 0xA5;
```

- [ ] **Step 3: Add message structs to protocol.h**

Add `SvSkillSyncMsg`, `SvQuestSyncMsg`, `SvInventorySyncMsg` with write/read methods as described in the spec.

- [ ] **Step 4: Add callbacks to net_client.h**

```cpp
std::function<void(const SvSkillSyncMsg&)> onSkillSync;
std::function<void(const SvQuestSyncMsg&)> onQuestSync;
std::function<void(const SvInventorySyncMsg&)> onInventorySync;
```

- [ ] **Step 5: Add deserialization cases to net_client.cpp**

After the last case in handlePacket switch, add cases for SvSkillSync, SvQuestSync, SvInventorySync.

- [ ] **Step 6: Run tests — expect PASS**

- [ ] **Step 7: Commit**
```
feat: add SvSkillSync, SvQuestSync, SvInventorySync protocol messages
```

---

## Task 8: Server Sends State Sync on Connect

**Files:**
- Modify: `server/server_app.cpp:793` (after sendPlayerState, add sync sends)
- Test: `tests/test_state_sync.cpp`

- [ ] **Step 1: Write test that server sends sync messages after connect**

- [ ] **Step 2: Add sendSkillSync() method**

Reads from player's SkillManagerComponent, builds SvSkillSyncMsg, sends to client.

- [ ] **Step 3: Add sendQuestSync() method**

Reads from player's QuestComponent, builds SvQuestSyncMsg, sends to client.

- [ ] **Step 4: Add sendInventorySync() method**

Reads from player's InventoryComponent, builds SvInventorySyncMsg, sends to client.

- [ ] **Step 5: Call all three after sendPlayerState() on connect**

At `server/server_app.cpp:793`:
```cpp
sendPlayerState(clientId);
sendSkillSync(clientId);
sendQuestSync(clientId);
sendInventorySync(clientId);
```

- [ ] **Step 6: Run tests — expect PASS**

- [ ] **Step 7: Commit**
```
feat: server sends full state sync (skills, quests, inventory) on connect
```

---

## Task 9: Client Handles State Sync Messages

**Files:**
- Modify: `game/game_app.cpp` (add onSkillSync, onQuestSync, onInventorySync handlers)
- Test: manual verification (handlers apply to local components)

- [ ] **Step 1: Add onSkillSync handler**

In `game/game_app.cpp` onInit, after existing callbacks:
```cpp
netClient_.onSkillSync = [this](const SvSkillSyncMsg& msg) {
    auto* sc = SceneManager::instance().currentScene();
    if (!sc) return;
    sc->world().forEach<SkillManagerComponent, PlayerController>(
        [&](Entity*, SkillManagerComponent* sm, PlayerController* ctrl) {
            if (!ctrl->isLocalPlayer) return;
            sm->skills.clearAll();
            for (const auto& skill : msg.skills) {
                sm->skills.learnSkill(skill.skillId, skill.unlockedRank);
                for (int r = 0; r < skill.activatedRank; ++r)
                    sm->skills.activateSkillRank(skill.skillId);
            }
            for (int i = 0; i < (int)msg.skillBar.size() && i < 20; ++i) {
                if (!msg.skillBar[i].empty())
                    sm->skills.assignSkillToSlot(msg.skillBar[i], i);
            }
        }
    );
};
```

- [ ] **Step 2: Add onQuestSync handler** (same pattern with QuestComponent)

- [ ] **Step 3: Add onInventorySync handler** (same pattern with InventoryComponent)

- [ ] **Step 4: Store pending sync state if player not created yet** (same pattern as pendingPlayerState_)

- [ ] **Step 5: Run tests — expect PASS**

- [ ] **Step 6: Commit**
```
feat: client applies server skill/quest/inventory sync on connect
```

---

## Task 10: Strip CombatActionSystem of State Changes

**Files:**
- Modify: `game/systems/combat_action_system.h:589-1092`
- Test: Existing combat tests + manual verification

This is the core change. The client's combat system becomes prediction + display only.

- [ ] **Step 1: Add onSendAttack callback to CombatActionSystem**

Add a public callback field:
```cpp
std::function<void(uint64_t targetPid)> onSendAttack;
```

- [ ] **Step 2: Wire the callback in GameApp**

In `game/game_app.cpp` after combatSystem_ is created (~line 775):
```cpp
combatSystem_->onSendAttack = [this](uint64_t targetPid) {
    if (netClient_.isConnected()) {
        netClient_.sendAction(0, targetPid, 0);
    }
};
```

- [ ] **Step 3: In tryAttackTarget(), send CmdAction instead of applying damage**

Replace the mob damage application section (around line 721):
- KEEP: damage calculation (for prediction), animation, predicted floating text
- REMOVE: `es.takeDamageFrom(player->id(), damage)`
- ADD: `if (onSendAttack) onSendAttack(targetPid);` where targetPid is looked up from ghostEntities

- [ ] **Step 4: Remove client-side mob death handling**

In the section after damage (~line 827), remove the `if (!es.isAlive)` block that awards XP, gold, honor. Mob death is now determined by `SvCombatEvent.isKill`.

- [ ] **Step 5: Remove onMobDeath XP/honor/gold awards**

In `onMobDeath()` (~line 1010-1092), remove:
- `ps.addXP(xp)` (line 1029)
- `ps.honor += es.honorReward` (line 1078)
- All gold rolling and `addGold()` calls (lines 1048-1075)

Keep: death animation, death VFX, mob sprite disable

- [ ] **Step 6: Do the same for PvP branch**

In the PvP section (~lines 831-1004), remove `ts.takeDamage(damage)` and all local stat modifications. Keep animation and predicted text. Add `onSendAttack` call.

- [ ] **Step 7: Run tests — fix any that relied on client-side combat**

- [ ] **Step 8: Commit**
```
feat: CombatActionSystem prediction-only, all damage via server
```

---

## Task 11: Suppress Double Damage Text

**Files:**
- Modify: `game/game_app.cpp:878-930` (onCombatEvent handler)
- Modify: `game/game_app.h` (add localPlayerPid_ field)

- [ ] **Step 1: Track local player's PersistentId**

In `game/game_app.h`, add: `uint64_t localPlayerPid_ = 0;`

Set it when ConnectAccept arrives (the server sends the player's PID in the auth flow).

- [ ] **Step 2: In onCombatEvent, suppress floating text when attacker is local player**

```cpp
// Local player already showed predicted damage text — skip server duplicate
if (msg.attackerId == localPlayerPid_) return;
```

Wait — we still need to process the combat event for OTHER players attacking mobs (ghost HP updates, kill animations). Refine:
```cpp
bool isLocalAttack = (msg.attackerId == localPlayerPid_);
// Skip floating text for local attacks (prediction already showed it)
// But still process kill/death effects
if (!isLocalAttack) {
    combatSystem_->showDamageText(targetPos, msg.damage, msg.isCrit != 0);
}
// Always process kill effects
if (msg.isKill) {
    // Play death animation on target ghost
}
```

- [ ] **Step 3: Run tests — expect PASS**

- [ ] **Step 4: Commit**
```
fix: suppress double damage text for local player auto-attacks
```

---

## Task 12: Respawn Server-Gated

**Files:**
- Modify: `game/game_app.cpp:1169-1207` (deathOverlayUI_.onRespawnRequested)
- Modify: `game/ui/death_overlay_ui.h` (add pending state)
- Modify: `game/ui/death_overlay_ui.cpp` (show "Respawning..." when pending)

- [ ] **Step 1: Remove local respawn from onRespawnRequested**

In `game/game_app.cpp`, the `deathOverlayUI_.onRespawnRequested` callback currently calls `sc->stats.respawn()` locally AND sends to server. Remove the local respawn. Keep only `netClient_.sendRespawn(respawnType)`.

- [ ] **Step 2: Add pending state to death_overlay_ui.h**

```cpp
bool respawnPending_ = false;
float respawnPendingTime_ = 0.0f;
```

- [ ] **Step 3: Update death_overlay_ui.cpp render**

When `respawnPending_`, disable all 3 buttons and show "Respawning..." text. If 5 seconds elapse without SvRespawnMsg, re-enable buttons (retry).

- [ ] **Step 4: Reset pending state when SvRespawnMsg arrives**

In `game/game_app.cpp` onRespawn handler, add: `deathOverlayUI_.respawnPending_ = false;`

- [ ] **Step 5: Run tests — expect PASS**

- [ ] **Step 6: Commit**
```
feat: respawn waits for server approval, pending UI state
```

---

## Task 13: Integration Tests

**Files:**
- Create: `tests/test_server_authority.cpp` (extend from Task 2)
- Create: `tests/test_state_sync.cpp` (extend from Task 7)

- [ ] **Step 1: Test server rejects attack on dead mob**
- [ ] **Step 2: Test server rejects attack while player dead**
- [ ] **Step 3: Test server rejects skill during cooldown**
- [ ] **Step 4: Test server awards XP on mob kill**
- [ ] **Step 5: Test addXP handles multi-level overflow**
- [ ] **Step 6: Test SvPlayerStateMsg honor round-trip**
- [ ] **Step 7: Test SvSkillSyncMsg round-trip**
- [ ] **Step 8: Test SvQuestSyncMsg round-trip**
- [ ] **Step 9: Test SvInventorySyncMsg round-trip**
- [ ] **Step 10: Test double-respawn rejected when not dead**
- [ ] **Step 11: Run full suite — expect all 376+ tests PASS**
- [ ] **Step 12: Commit**
```
test: comprehensive server authority and state sync tests
```

---

## Task Order & Dependencies

```
Task 1 (honor in SvPlayerState) ─────────────────────────────┐
Task 2 (server addXP + level-up) ────────────────────────────┤
Task 3 (harden processAction) ───────────────────────────────┤
Task 4 (skill cooldown validation) ──────────────────────────┤
Task 5 (PvP auto-attack path) ──── depends on Task 3 ───────┤
Task 6 (isDead check in respawn) ────────────────────────────┤
                                                              │
Task 7 (new protocol messages) ──────────────────────────────┤
Task 8 (server sends sync) ──── depends on Task 7 ──────────┤
Task 9 (client handles sync) ── depends on Task 7,8 ────────┤
                                                              │
Task 10 (strip CombatActionSystem) ── depends on Task 3 ────┤
Task 11 (suppress double text) ── depends on Task 10 ───────┤
Task 12 (respawn server-gated) ── depends on Task 6 ────────┤
                                                              │
Task 13 (integration tests) ──── depends on ALL above ──────┘
```

**Parallel-safe groups:**
- Tasks 1, 2, 3, 4, 6, 7 can all run in parallel (independent protocol/server changes)
- Tasks 5, 8 depend on earlier tasks
- Tasks 9, 10, 11, 12 are client-side and depend on protocol/server being done
- Task 13 is the final integration gate
