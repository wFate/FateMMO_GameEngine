# Combat & PvP Integrity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two-tick death lifecycle, full PvP target validation, optimistic combat feedback, and inventory slot safety.

**Architecture:** Two-tick death uses a `LifeState` enum (Alive→Dying→Dead) processed in the server tick loop. PvP validation expands `canAttackPlayer()` with faction, party, safe-zone, and PK-status checks. Optimistic combat feedback plays attack animations immediately on client input with server reconciliation. Inventory slot safety adds occupancy checks to `addItemToSlot()`.

**Tech Stack:** C++23, existing ECS components, existing protocol layer

---

## File Structure

| Action | Path | Responsibility |
|--------|------|---------------|
| Modify | `game/shared/character_stats.h` | Add LifeState enum replacing bool isDead |
| Modify | `game/shared/character_stats.cpp` | Two-tick death transition logic |
| Modify | `server/server_app.cpp` | Process DYING→DEAD transitions in tick loop |
| Modify | `server/target_validator.h` | Full PvP validation (faction, party, safe zone, PK) |
| Modify | `game/shared/inventory.h` | addItemToSlot occupancy check |
| Modify | `game/shared/inventory.cpp` | Implement safety check |
| Modify | `engine/net/protocol.h` | Add SvAttackConfirm for optimistic feedback |
| Modify | `game/game_app.cpp` | Client-side immediate attack animation |
| Create | `tests/test_death_lifecycle.cpp` | Two-tick death state machine tests |
| Create | `tests/test_pvp_validation.cpp` | PvP target validation matrix tests |
| Create | `tests/test_inventory_safety.cpp` | Slot overwrite prevention tests |

---

### Task 1: Two-tick death lifecycle (DYING → DEAD)

**Files:**
- Modify: `game/shared/character_stats.h`
- Modify: `game/shared/character_stats.cpp`
- Modify: `server/server_app.cpp`
- Create: `tests/test_death_lifecycle.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_death_lifecycle.cpp
#include <doctest/doctest.h>
#include "game/shared/character_stats.h"

TEST_SUITE("Death Lifecycle") {

TEST_CASE("new character starts Alive") {
    CharacterStats stats;
    stats.maxHP = 100;
    stats.currentHP = 100;
    CHECK(stats.lifeState == LifeState::Alive);
    CHECK(stats.isAlive());
    CHECK_FALSE(stats.isDying());
    CHECK_FALSE(stats.isDead);
}

TEST_CASE("lethal damage transitions to Dying not Dead") {
    CharacterStats stats;
    stats.maxHP = 100;
    stats.currentHP = 100;
    stats.takeDamage(150);
    CHECK(stats.lifeState == LifeState::Dying);
    CHECK(stats.isDying());
    CHECK_FALSE(stats.isAlive());
    CHECK(stats.currentHP <= 0);
}

TEST_CASE("advanceDeathTick transitions Dying to Dead") {
    CharacterStats stats;
    stats.maxHP = 100;
    stats.currentHP = 100;
    stats.takeDamage(150);
    CHECK(stats.lifeState == LifeState::Dying);

    stats.advanceDeathTick(); // server calls this next tick
    CHECK(stats.lifeState == LifeState::Dead);
    CHECK(stats.isDead);
}

TEST_CASE("respawn transitions Dead back to Alive") {
    CharacterStats stats;
    stats.maxHP = 100;
    stats.currentHP = 100;
    stats.takeDamage(150);
    stats.advanceDeathTick();
    CHECK(stats.isDead);

    stats.respawn();
    CHECK(stats.lifeState == LifeState::Alive);
    CHECK(stats.isAlive());
    CHECK(stats.currentHP > 0);
}

TEST_CASE("onDied callback fires during Dying transition") {
    CharacterStats stats;
    stats.maxHP = 100;
    stats.currentHP = 100;
    bool callbackFired = false;
    stats.onDied = [&]() { callbackFired = true; };
    stats.takeDamage(150);
    CHECK(callbackFired); // fires when entering Dying
}

TEST_CASE("Dying entity cannot take further damage") {
    CharacterStats stats;
    stats.maxHP = 100;
    stats.currentHP = 100;
    stats.takeDamage(80);
    CHECK(stats.isAlive()); // still alive at 20 HP

    stats.takeDamage(30); // lethal
    CHECK(stats.isDying());
    int hpBeforeExtra = stats.currentHP;
    stats.takeDamage(50); // should be rejected
    CHECK(stats.currentHP == hpBeforeExtra); // unchanged
}

TEST_CASE("Dead entity cannot take damage") {
    CharacterStats stats;
    stats.maxHP = 100;
    stats.currentHP = 100;
    stats.takeDamage(150);
    stats.advanceDeathTick();
    int hp = stats.currentHP;
    stats.takeDamage(50);
    CHECK(stats.currentHP == hp);
}

} // TEST_SUITE
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build build --target fate_tests 2>&1 | head -20`
Expected: Compilation error — `LifeState` not defined

- [ ] **Step 3: Add LifeState enum and modify CharacterStats**

In `game/shared/character_stats.h`, before the class:

```cpp
enum class LifeState : uint8_t {
    Alive  = 0,
    Dying  = 1,  // one-tick window for on-death procs
    Dead   = 2
};
```

Modify CharacterStats members — replace the simple `bool isDead` with:

```cpp
LifeState lifeState = LifeState::Alive;
bool isDead = false;  // kept for backward compat, synced from lifeState

[[nodiscard]] bool isAlive() const { return lifeState == LifeState::Alive; }
[[nodiscard]] bool isDying() const { return lifeState == LifeState::Dying; }
void advanceDeathTick(); // called by server each tick to transition Dying → Dead
```

- [ ] **Step 4: Modify takeDamage to transition to Dying**

In `character_stats.cpp`, modify `takeDamage()`:

```cpp
int CharacterStats::takeDamage(int amount) {
    if (lifeState != LifeState::Alive) return 0; // reject damage to dying/dead

    int actual = std::min(amount, currentHP);
    currentHP -= actual;
    if (onDamaged) onDamaged(actual);

    if (currentHP <= 0) {
        currentHP = 0;
        lifeState = LifeState::Dying;
        // Do NOT set isDead yet — that happens on advanceDeathTick()
        if (onDied) onDied(); // on-death procs fire here (DoT kill credit, etc.)
    }
    return actual;
}
```

- [ ] **Step 5: Implement advanceDeathTick()**

```cpp
void CharacterStats::advanceDeathTick() {
    if (lifeState == LifeState::Dying) {
        lifeState = LifeState::Dead;
        isDead = true;
    }
}
```

- [ ] **Step 6: Modify respawn() to use LifeState**

```cpp
void CharacterStats::respawn() {
    lifeState = LifeState::Alive;
    isDead = false;
    currentHP = maxHP / 2; // respawn at 50% HP
    currentMP = maxMP;
    currentFury = 0.0f;
    if (onRespawned) onRespawned();
}
```

- [ ] **Step 7: Add DYING→DEAD processing to server tick loop**

In `server_app.cpp` `tick()`, add after status effect ticking but before replication:

```cpp
// Process Dying → Dead transitions (two-tick death lifecycle)
world_.forEach<CharacterStats>([](Entity*, CharacterStats* stats) {
    stats->advanceDeathTick();
});
```

- [ ] **Step 8: Update all `isDead` checks in server_app.cpp**

Replace checks like `if (stats->isDead)` with `if (!stats->isAlive())` where the intent is "can't act" (covers both Dying and Dead).

Keep `if (stats->isDead)` only where full death is required (respawn eligibility).

- [ ] **Step 9: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="Death Lifecycle"`
Expected: All 7 tests PASS

- [ ] **Step 10: Commit**

```bash
git add game/shared/character_stats.h game/shared/character_stats.cpp server/server_app.cpp tests/test_death_lifecycle.cpp
git commit -m "feat: two-tick death lifecycle with DYING state for on-death procs"
```

---

### Task 2: Full PvP target validation

**Files:**
- Modify: `server/target_validator.h`
- Modify: `server/server_app.cpp`
- Create: `tests/test_pvp_validation.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_pvp_validation.cpp
#include <doctest/doctest.h>
#include "server/target_validator.h"
#include "game/shared/character_stats.h"

TEST_SUITE("PvP Target Validation") {

TEST_CASE("same faction cannot attack each other") {
    CharacterStats attacker, target;
    attacker.faction = 1; // Siras
    target.faction = 1;   // Siras
    CHECK_FALSE(TargetValidator::canAttackPlayer(attacker, target, false, false));
}

TEST_CASE("different factions can attack") {
    CharacterStats attacker, target;
    attacker.faction = 1; // Siras
    target.faction = 2;   // Lanos
    CHECK(TargetValidator::canAttackPlayer(attacker, target, false, false));
}

TEST_CASE("cannot attack party member") {
    CharacterStats attacker, target;
    attacker.faction = 1;
    target.faction = 2;
    CHECK_FALSE(TargetValidator::canAttackPlayer(attacker, target, true, false));
}

TEST_CASE("cannot attack in safe zone") {
    CharacterStats attacker, target;
    attacker.faction = 1;
    target.faction = 2;
    CHECK_FALSE(TargetValidator::canAttackPlayer(attacker, target, false, true));
}

TEST_CASE("can attack same faction if target is Red PK") {
    CharacterStats attacker, target;
    attacker.faction = 1;
    target.faction = 1;
    target.pkStatus = PKStatus::Red;
    CHECK(TargetValidator::canAttackPlayer(attacker, target, false, false));
}

TEST_CASE("can attack same faction if target is Black PK") {
    CharacterStats attacker, target;
    attacker.faction = 1;
    target.faction = 1;
    target.pkStatus = PKStatus::Black;
    CHECK(TargetValidator::canAttackPlayer(attacker, target, false, false));
}

TEST_CASE("dead attacker cannot attack") {
    CharacterStats attacker;
    attacker.isDead = true;
    attacker.lifeState = LifeState::Dead;
    CHECK_FALSE(TargetValidator::isAttackerAlive(attacker));
}

TEST_CASE("dying attacker cannot attack") {
    CharacterStats attacker;
    attacker.lifeState = LifeState::Dying;
    CHECK_FALSE(TargetValidator::isAttackerAlive(attacker));
}

} // TEST_SUITE
```

- [ ] **Step 2: Expand canAttackPlayer with full validation**

Replace the stub in `server/target_validator.h`:

```cpp
static bool canAttackPlayer(const CharacterStats& attacker,
                            const CharacterStats& target,
                            bool inSameParty,
                            bool inSafeZone) {
    // Cannot attack in safe zones (towns, starting areas)
    if (inSafeZone) return false;

    // Cannot attack party members
    if (inSameParty) return false;

    // Cannot attack dead/dying targets
    if (!target.isAlive()) return false;

    // Same faction: only allowed if target is Red or Black (PK flagged)
    if (attacker.faction == target.faction && attacker.faction != 0) {
        return target.pkStatus == PKStatus::Red ||
               target.pkStatus == PKStatus::Black;
    }

    // Different factions: always allowed
    return true;
}

static bool isAttackerAlive(const CharacterStats& stats) {
    return stats.isAlive(); // covers both Dying and Dead
}
```

- [ ] **Step 3: Update processAction/processUseSkill to pass party/zone info**

In `server_app.cpp`, where `canAttackPlayer()` is called, pass the actual party membership and safe-zone state:

```cpp
bool inSameParty = false;
// Check party membership
auto* attackerParty = playerEntity->getComponent<PartyComponent>();
auto* targetParty = targetEntity->getComponent<PartyComponent>();
if (attackerParty && targetParty && attackerParty->partyId != 0
    && attackerParty->partyId == targetParty->partyId) {
    inSameParty = true;
}

// Check safe zone (scene-level flag)
bool inSafeZone = false;
auto* zone = playerEntity->getComponent<ZoneComponent>();
if (zone) {
    auto sceneDef = sceneCache_.get(zone->currentScene);
    if (sceneDef && !sceneDef->pvpEnabled) inSafeZone = true;
}

if (!TargetValidator::canAttackPlayer(*attackerStats, *targetStats, inSameParty, inSafeZone)) {
    return; // silently reject
}
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="PvP Target Validation"`
Expected: All 8 tests PASS

- [ ] **Step 5: Commit**

```bash
git add server/target_validator.h server/server_app.cpp tests/test_pvp_validation.cpp
git commit -m "feat: full PvP target validation with faction, party, safe zone, and PK checks"
```

---

### Task 3: Inventory slot safety (addItemToSlot overwrite prevention)

**Files:**
- Modify: `game/shared/inventory.h`
- Modify: `game/shared/inventory.cpp`
- Create: `tests/test_inventory_safety.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
// tests/test_inventory_safety.cpp
#include <doctest/doctest.h>
#include "game/shared/inventory.h"

TEST_SUITE("Inventory Slot Safety") {

TEST_CASE("addItemToSlot to empty slot succeeds") {
    Inventory inv;
    inv.initialize("test_char", 0);
    ItemInstance item;
    item.itemId = "sword_01";
    item.quantity = 1;
    CHECK(inv.addItemToSlot(0, item));
    CHECK(inv.getSlot(0).itemId == "sword_01");
}

TEST_CASE("addItemToSlot to occupied slot fails") {
    Inventory inv;
    inv.initialize("test_char", 0);
    ItemInstance item1, item2;
    item1.itemId = "sword_01"; item1.quantity = 1;
    item2.itemId = "shield_01"; item2.quantity = 1;
    REQUIRE(inv.addItemToSlot(0, item1));
    CHECK_FALSE(inv.addItemToSlot(0, item2)); // must not overwrite
    CHECK(inv.getSlot(0).itemId == "sword_01"); // original preserved
}

TEST_CASE("addItemToSlot to invalid slot index fails") {
    Inventory inv;
    inv.initialize("test_char", 0);
    ItemInstance item;
    item.itemId = "potion_01"; item.quantity = 1;
    CHECK_FALSE(inv.addItemToSlot(-1, item));
    CHECK_FALSE(inv.addItemToSlot(9999, item));
}

TEST_CASE("addItemToSlot to trade-locked slot fails") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.lockSlotForTrade(0);
    ItemInstance item;
    item.itemId = "gem_01"; item.quantity = 1;
    CHECK_FALSE(inv.addItemToSlot(0, item));
}

TEST_CASE("removing item then adding to same slot succeeds") {
    Inventory inv;
    inv.initialize("test_char", 0);
    ItemInstance item1, item2;
    item1.itemId = "sword_01"; item1.quantity = 1;
    item2.itemId = "axe_01"; item2.quantity = 1;
    REQUIRE(inv.addItemToSlot(0, item1));
    REQUIRE(inv.removeItem(0));
    CHECK(inv.addItemToSlot(0, item2));
    CHECK(inv.getSlot(0).itemId == "axe_01");
}

} // TEST_SUITE
```

- [ ] **Step 2: Verify current addItemToSlot behavior**

Read `game/shared/inventory.cpp` to check if the occupancy check exists. If `addItemToSlot()` silently overwrites, proceed with the fix. If it already checks, mark this task as verified-safe.

- [ ] **Step 3: Add occupancy check to addItemToSlot**

In `inventory.cpp`, modify `addItemToSlot()`:

```cpp
bool Inventory::addItemToSlot(int slotIndex, const ItemInstance& item) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(slots_.size())) return false;
    if (isSlotLocked(slotIndex)) return false;

    // Safety check: do not overwrite occupied slots
    if (!slots_[slotIndex].itemId.empty()) return false;

    slots_[slotIndex] = item;
    if (onInventoryChanged) onInventoryChanged();
    return true;
}
```

- [ ] **Step 4: Run tests**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe -tc="Inventory Slot Safety"`
Expected: All 5 tests PASS

- [ ] **Step 5: Run full suite to check no regressions**

Run: `cmake --build build --target fate_tests && build/Debug/fate_tests.exe`
Expected: All tests PASS (existing loot/trade/market code should use `addItem()` for finding empty slots, not `addItemToSlot()` directly)

- [ ] **Step 6: Commit**

```bash
git add game/shared/inventory.h game/shared/inventory.cpp tests/test_inventory_safety.cpp
git commit -m "fix: addItemToSlot rejects occupied slots to prevent item destruction"
```

---

### Task 4: Optimistic combat feedback (client-side)

**Files:**
- Modify: `game/game_app.cpp`
- Modify: `engine/net/net_client.h`

This task is primarily a client-side UX change. The server protocol already supports it — `SvSkillResultMsg` returns full results. The change is: play the attack animation **immediately** on input rather than waiting for the server response.

- [ ] **Step 1: Add pending prediction tracking to client**

In `net_client.h` or a new `game/combat_prediction.h`:

```cpp
struct PendingAttack {
    uint32_t predictionId = 0;
    uint64_t targetId = 0;
    float timestamp = 0.0f;
    bool resolved = false;
};

// Ring buffer of pending attacks for reconciliation
static constexpr int MAX_PENDING = 32;
std::array<PendingAttack, MAX_PENDING> pendingAttacks_{};
uint32_t nextPredictionId_ = 1;
int pendingHead_ = 0;
```

- [ ] **Step 2: On attack input, immediately play animation**

In the client's attack input handler (game_app.cpp), change from:

```cpp
// OLD: just send and wait
netClient_.sendAction(0, targetId, 0);
```

To:

```cpp
// NEW: send AND immediately start attack animation
netClient_.sendAction(0, targetId, 0);

// Immediately play attack windup animation (cosmetic prediction)
if (auto* animator = playerEntity->getComponent<Animator>()) {
    animator->playAnimation("attack", false); // non-looping
}

// Track prediction for reconciliation
PendingAttack pred;
pred.predictionId = nextPredictionId_++;
pred.targetId = targetId;
pred.timestamp = gameTime_;
pendingAttacks_[pendingHead_ % MAX_PENDING] = pred;
pendingHead_++;
```

- [ ] **Step 3: On server response, reconcile**

In the `onCombatEvent` or `onSkillResult` callback:

```cpp
// Server confirmed the hit — show damage number with final value
if (msg.hitFlags & HitFlags::HIT) {
    showDamageNumber(msg.targetId, msg.damage, msg.hitFlags & HitFlags::CRIT);
}
if (msg.hitFlags & HitFlags::MISS) {
    showFloatingText(msg.targetId, "MISS", Color::gray());
}
if (msg.hitFlags & HitFlags::DODGE) {
    showFloatingText(msg.targetId, "DODGE", Color::gray());
}
// Animation already playing from prediction — no correction needed
// since attack animations are short-lived and non-reversible
```

- [ ] **Step 4: Same pattern for skill use**

```cpp
// On skill button press:
netClient_.sendUseSkill(skillId, rank, targetId);
// Immediately play skill cast animation
if (auto* animator = playerEntity->getComponent<Animator>()) {
    animator->playAnimation(skillAnimName, false);
}
```

- [ ] **Step 5: Commit**

```bash
git add game/game_app.cpp engine/net/net_client.h
git commit -m "feat: optimistic combat feedback - play attack animations immediately on input"
```
