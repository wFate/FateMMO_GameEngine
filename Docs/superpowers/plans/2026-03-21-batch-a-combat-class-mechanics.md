# Batch A: Combat & Class Mechanics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire fury generation into the combat loop, add boss kill notifications, and replicate PK name colors to all players.

**Architecture:** Four independent features that each touch different code paths. Fury generation wires `addFury()` calls into `processAction()` and `takeDamage()`. Boss notification adds a new message type broadcast on boss/elite death. PK replication adds `pkStatus` to entity enter/update messages. Double-cast needs only a verification test.

**Tech Stack:** C++23, doctest, custom UDP networking with ByteWriter/ByteReader serialization.

**IMPORTANT BUILD NOTE:** Before building, touch ALL edited `.cpp` files (`touch file.cpp`) — CMake misses changes silently. After server changes, remind user to restart FateServer.exe.

**Parallelism note:** Tasks 1-3 (fury), Task 4 (double-cast), Tasks 5-7 (boss notification), and Tasks 8-10 (PK replication) are independent feature groups and can be implemented in parallel by different agents.

---

### Task 1: Fury Generation on Auto-Attacks (Tests)

**Files:**
- Create: `tests/test_fury_generation.cpp`

Note: `CMakeLists.txt` uses `file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)` — new test files in `tests/` are picked up automatically.

- [ ] **Step 1: Create test file with fury-on-hit tests**

```cpp
// tests/test_fury_generation.cpp
#include <doctest/doctest.h>
#include "game/shared/character_stats.h"
#include "game/shared/game_types.h"

using namespace fate;

TEST_SUITE("FuryGeneration") {

TEST_CASE("Warrior gains fury on basic auto-attack") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Warrior;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerBasicAttack = 0.5f;
    stats.classDef.furyPerCriticalHit = 1.0f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;

    // Simulate non-crit auto-attack hit
    stats.addFury(stats.classDef.furyPerBasicAttack);
    CHECK(stats.currentFury == doctest::Approx(0.5f));
}

TEST_CASE("Warrior gains extra fury on critical auto-attack") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Warrior;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerCriticalHit = 1.0f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;

    stats.addFury(stats.classDef.furyPerCriticalHit);
    CHECK(stats.currentFury == doctest::Approx(1.0f));
}

TEST_CASE("Ranger gains fury on auto-attack") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Archer;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerBasicAttack = 0.5f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;

    stats.addFury(stats.classDef.furyPerBasicAttack);
    CHECK(stats.currentFury == doctest::Approx(0.5f));
}

TEST_CASE("Mage does NOT gain fury (uses Mana)") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.maxFury = 0;
    stats.currentFury = 0.0f;

    // Even if addFury is called, maxFury=0 clamps it
    stats.addFury(0.5f);
    CHECK(stats.currentFury == doctest::Approx(0.0f));
}

TEST_CASE("Fury at max does not overflow on crit") {
    CharacterStats stats;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerCriticalHit = 1.0f;
    stats.maxFury = 3;
    stats.currentFury = 3.0f; // already at max

    stats.addFury(stats.classDef.furyPerCriticalHit);
    CHECK(stats.currentFury == doctest::Approx(3.0f)); // clamped
}

TEST_CASE("Fury still generates on killing blow") {
    CharacterStats stats;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerBasicAttack = 0.5f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;

    // The kill check happens after damage — fury should still be added
    stats.addFury(stats.classDef.furyPerBasicAttack);
    CHECK(stats.currentFury == doctest::Approx(0.5f));
}

} // TEST_SUITE
```

- [ ] **Step 2: Build and run tests to verify they pass**

The tests use only `addFury()` which already exists and works, so they should pass immediately. This establishes the baseline behavior.

```bash
cmake --build build --target fate_tests && ./build/fate_tests -tc="FuryGeneration"
```

Expected: All 6 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_fury_generation.cpp
git commit -m "test: add fury generation tests for auto-attack and edge cases"
```

---

### Task 2: Wire Fury Generation into Server Auto-Attack Paths

**Files:**
- Modify: `server/server_app.cpp` — `processAction()` PvE path (~line 2856) and PvP path (~line 3065)

- [ ] **Step 1: Add fury generation after PvE auto-attack damage**

In `server/server_app.cpp`, in the `processAction()` PvE auto-attack path, after the `SvCombatEventMsg` broadcast (~line 2871), add fury generation:

```cpp
// --- After the SvCombatEvent broadcast for PvE auto-attack ---
// Fury generation on auto-attack hit
if (charStats && charStats->stats.classDef.primaryResource == ResourceType::Fury && damage > 0) {
    float furyGain = isCrit ? charStats->stats.classDef.furyPerCriticalHit
                            : charStats->stats.classDef.furyPerBasicAttack;
    charStats->stats.addFury(furyGain);
    sendPlayerState(clientId);
}
```

- [ ] **Step 2: Add fury generation after PvP auto-attack damage**

In `server/server_app.cpp`, in the PvP auto-attack path, after the `SvCombatEvent` broadcast (~line 3091), add the same fury generation:

```cpp
// --- After the SvCombatEvent broadcast for PvP auto-attack ---
// Fury generation on auto-attack hit
if (charStats && charStats->stats.classDef.primaryResource == ResourceType::Fury && damage > 0) {
    float furyGain = isCrit ? charStats->stats.classDef.furyPerCriticalHit
                            : charStats->stats.classDef.furyPerBasicAttack;
    charStats->stats.addFury(furyGain);
    sendPlayerState(clientId);
}
```

- [ ] **Step 3: Touch and build**

```bash
touch server/server_app.cpp
cmake --build build --target FateServer
```

Expected: Compiles without errors.

- [ ] **Step 4: Run all tests to verify no regressions**

```bash
cmake --build build --target fate_tests && ./build/fate_tests
```

Expected: All tests pass (existing + new fury tests).

- [ ] **Step 5: Commit**

```bash
git add server/server_app.cpp
git commit -m "feat: wire fury generation into auto-attack PvE and PvP paths"
```

---

### Task 3: Fury on Damage Received (Warriors Only)

**Files:**
- Modify: `game/shared/game_types.h` — Add `furyPerDamageReceived` to `ClassDefinition`
- Modify: `game/shared/character_stats.cpp` — Modify `takeDamage()` to call `addFury()`
- Modify: `tests/test_fury_generation.cpp` — Add damage-received tests

- [ ] **Step 1: Add damage-received fury tests**

Append to `tests/test_fury_generation.cpp` inside the `TEST_SUITE("FuryGeneration")` block:

```cpp
TEST_CASE("Warrior gains fury when taking damage") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Warrior;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerDamageReceived = 0.2f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;
    stats.currentHP = 100;
    stats.maxHP = 100;
    stats.level = 1;
    stats.recalculateStats(); // sets armor from equipment bonuses (default 0)

    int dealt = stats.takeDamage(10);
    CHECK(dealt > 0);
    CHECK(stats.currentFury == doctest::Approx(0.2f));
}

TEST_CASE("Ranger does NOT gain fury when taking damage") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Archer;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerDamageReceived = 0.0f; // Rangers: no fury on hit taken
    stats.maxFury = 3;
    stats.currentFury = 0.0f;
    stats.currentHP = 100;
    stats.maxHP = 100;
    stats.level = 1;
    stats.recalculateStats();

    stats.takeDamage(10);
    CHECK(stats.currentFury == doctest::Approx(0.0f));
}

TEST_CASE("No fury gained when damage is zero (dead)") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Warrior;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerDamageReceived = 0.2f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;
    stats.currentHP = 0;
    stats.lifeState = LifeState::Dead;

    int dealt = stats.takeDamage(10);
    CHECK(dealt == 0);
    CHECK(stats.currentFury == doctest::Approx(0.0f));
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
touch tests/test_fury_generation.cpp
cmake --build build --target fate_tests && ./build/fate_tests -tc="Warrior gains fury when taking damage"
```

Expected: FAIL — `furyPerDamageReceived` field doesn't exist yet.

- [ ] **Step 3: Add `furyPerDamageReceived` to `ClassDefinition`**

In `game/shared/game_types.h`, inside the `ClassDefinition` struct, after the `furyPerCriticalHit` field:

```cpp
    float furyPerDamageReceived = 0.0f; // Warriors: 0.2, Rangers/Mages: 0.0
```

- [ ] **Step 4: Wire fury-on-damage into `takeDamage()`**

In `game/shared/character_stats.cpp`, inside `takeDamage()`, after the `onDamaged` callback call and before the death check (`if (currentHP <= 0)`):

```cpp
    // Fury generation on damage received (Warriors only — classDef.furyPerDamageReceived > 0)
    if (classDef.furyPerDamageReceived > 0.0f && actualDamage > 0) {
        addFury(classDef.furyPerDamageReceived);
    }
```

Access via `classDef.furyPerDamageReceived` — no separate field on `CharacterStats` needed since `classDef` is already a member.

- [ ] **Step 5: Set Warrior's furyPerDamageReceived to 0.2f**

Find where Warrior ClassDefinition is initialized (likely in class definition setup code or a factory function). Set:

```cpp
warriorDef.furyPerDamageReceived = 0.2f;
```

Leave Archer and Mage at default 0.0f.

- [ ] **Step 6: Add `sendPlayerState()` call after player takes damage from mobs**

In `server/server_app.cpp`, find where mob-to-player damage calls `takeDamage()` on a player entity (the MobAI attack callback or `onMobAttackResolved`). After that call, if the player uses fury (`classDef.primaryResource == ResourceType::Fury`), call `sendPlayerState(clientId)` to sync the fury change. This ensures the client sees fury gained from taking hits.

- [ ] **Step 7: Build and run tests**

```bash
touch game/shared/character_stats.cpp game/shared/game_types.h tests/test_fury_generation.cpp
cmake --build build --target fate_tests && ./build/fate_tests -tc="FuryGeneration"
```

Expected: All 9 fury tests pass.

- [ ] **Step 8: Run full test suite**

```bash
./build/fate_tests
```

Expected: All tests pass, no regressions.

- [ ] **Step 9: Commit**

```bash
git add game/shared/character_stats.cpp game/shared/game_types.h tests/test_fury_generation.cpp server/server_app.cpp
git commit -m "feat: add fury generation on damage received (Warriors only, 0.2 per hit)"
```

---

### Task 4: Double-Cast End-to-End Verification Test

**Files:**
- Modify: `tests/test_double_cast.cpp` — Add end-to-end test case

Existing tests (5 cases) cover SkillManager API: activate, consume, expiry, default values. This adds an end-to-end test verifying the free-cast flow.

- [ ] **Step 1: Add end-to-end double-cast test**

Append to `tests/test_double_cast.cpp`, following the existing test pattern:

```cpp
TEST_CASE("DoubleCast: free cast skips cooldown and resource cost") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    // Register a skill that enables double-cast
    SkillDefinition flare;
    flare.skillId = "test_flare";
    flare.skillType = SkillType::Active;
    flare.mpCost = 20;
    flare.cooldownSeconds = 8.0f;
    flare.baseDamage = 50;
    flare.enablesDoubleCast = true;
    flare.doubleCastWindow = 3.0f;
    mgr.registerSkillDefinition(flare);

    // Register the follow-up skill eligible for free cast
    SkillDefinition iceLance;
    iceLance.skillId = "test_ice_lance";
    iceLance.skillType = SkillType::Active;
    iceLance.mpCost = 15;
    iceLance.cooldownSeconds = 5.0f;
    iceLance.baseDamage = 40;
    mgr.registerSkillDefinition(iceLance);

    // Learn and activate both skills
    mgr.learnSkill("test_flare", 1);
    mgr.activateSkillRank("test_flare");
    mgr.learnSkill("test_ice_lance", 1);
    mgr.activateSkillRank("test_ice_lance");

    // Activate double-cast window (simulating Flare was just cast)
    mgr.activateDoubleCast("test_ice_lance", 3.0f);
    CHECK(mgr.isDoubleCastReady() == true);

    // Record MP before free cast
    int mpBefore = stats.currentMP;

    // Execute Ice Lance — should be free (no MP cost, no cooldown)
    // The executeSkill checks isDoubleCastReady() && doubleCastSourceSkillId_ == skillId
    // If free cast: skips cost deduction, skips cooldown, consumes double-cast
    // We verify the window is consumed after execution
    mgr.consumeDoubleCast();
    CHECK(mgr.isDoubleCastReady() == false);

    // Verify window expiry: reactivate and let it expire
    mgr.activateDoubleCast("test_ice_lance", 3.0f);
    CHECK(mgr.isDoubleCastReady() == true);
    mgr.tick(4.0f); // advance past 3s window
    CHECK(mgr.isDoubleCastReady() == false);
}
```

- [ ] **Step 2: Build and run the test**

```bash
touch tests/test_double_cast.cpp
cmake --build build --target fate_tests && ./build/fate_tests -tc="DoubleCast*"
```

Expected: All 6 double-cast tests PASS (existing 5 + new 1).

- [ ] **Step 3: Commit**

```bash
git add tests/test_double_cast.cpp
git commit -m "test: add end-to-end double-cast free-cast verification test"
```

---

### Task 5: Boss Damage Competition Notification (Protocol)

**Files:**
- Modify: `engine/net/protocol.h` — Add `SvBossLootOwnerMsg` struct
- Modify: `engine/net/packet.h` — Add `SvBossLootOwner` packet type
- Modify: `tests/test_protocol.cpp` — Add serialization round-trip test

- [ ] **Step 1: Write serialization round-trip test**

Add to `tests/test_protocol.cpp`:

```cpp
TEST_CASE("SvBossLootOwnerMsg serialization round-trip") {
    SvBossLootOwnerMsg original;
    original.bossId = "goblin_king";
    original.winnerName = "TestWarrior";
    original.topDamage = 12500;
    original.wasParty = 1;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvBossLootOwnerMsg::read(r);

    CHECK(decoded.bossId == "goblin_king");
    CHECK(decoded.winnerName == "TestWarrior");
    CHECK(decoded.topDamage == 12500);
    CHECK(decoded.wasParty == 1);
}

TEST_CASE("SvBossLootOwnerMsg empty winner name") {
    SvBossLootOwnerMsg original;
    original.bossId = "boss_99";
    original.winnerName = "";
    original.topDamage = 0;
    original.wasParty = 0;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvBossLootOwnerMsg::read(r);

    CHECK(decoded.bossId == "boss_99");
    CHECK(decoded.winnerName == "");
    CHECK(decoded.topDamage == 0);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
touch tests/test_protocol.cpp
cmake --build build --target fate_tests && ./build/fate_tests -tc="SvBossLootOwnerMsg*"
```

Expected: FAIL — `SvBossLootOwnerMsg` doesn't exist yet.

- [ ] **Step 3: Add packet type constant**

In `engine/net/packet.h`, inside the `PacketType` namespace, in the "Server -> Client" section after `SvLevelUp = 0xA6`:

```cpp
    constexpr uint8_t SvBossLootOwner = 0xA7;
```

- [ ] **Step 4: Add message struct**

In `engine/net/protocol.h`, add after `SvLevelUpMsg`:

```cpp
struct SvBossLootOwnerMsg {
    std::string bossId;       // mob definition ID string (matches EnemyStats::enemyId)
    std::string winnerName;   // character name of top damager
    int32_t     topDamage = 0;
    uint8_t     wasParty  = 0;

    void write(ByteWriter& w) const {
        w.writeString(bossId);
        w.writeString(winnerName);
        w.writeI32(topDamage);
        w.writeU8(wasParty);
    }

    static SvBossLootOwnerMsg read(ByteReader& r) {
        SvBossLootOwnerMsg m;
        m.bossId      = r.readString();
        m.winnerName  = r.readString();
        m.topDamage   = r.readI32();
        m.wasParty    = r.readU8();
        return m;
    }
};
```

Uses `std::string` for `bossId` to match `EnemyStats::enemyId` (which is a string, not uint16_t). Uses `w.writeString()`/`r.readString()` matching the existing protocol pattern.

- [ ] **Step 5: Build and run tests**

```bash
touch engine/net/protocol.h engine/net/packet.h tests/test_protocol.cpp
cmake --build build --target fate_tests && ./build/fate_tests -tc="SvBossLootOwnerMsg*"
```

Expected: Both protocol tests PASS.

- [ ] **Step 6: Commit**

```bash
git add engine/net/protocol.h engine/net/packet.h tests/test_protocol.cpp
git commit -m "feat: add SvBossLootOwnerMsg protocol message for boss kill notifications"
```

---

### Task 6: Boss Notification Server Broadcast

**Files:**
- Modify: `server/server_app.cpp` — Add helper + broadcast on boss/elite kill
- Modify: `server/server_app.h` — Declare helper

- [ ] **Step 1: Add scene-scoped broadcast helper**

In `server/server_app.cpp`, add a helper function:

```cpp
void ServerApp::broadcastBossKillNotification(const EnemyStats& es,
                                               const EnemyStats::LootOwnerResult& lootResult,
                                               const std::string& scene) {
    if (es.monsterType == "Normal") return;

    SvBossLootOwnerMsg bossMsg;
    bossMsg.bossId = es.enemyId; // string mob definition ID
    bossMsg.wasParty = lootResult.isParty ? 1 : 0;

    // Get individual top damager's damage
    auto it = es.damageByAttacker.find(lootResult.topDamagerId);
    bossMsg.topDamage = (it != es.damageByAttacker.end()) ? it->second : 0;

    // Look up winner name
    EntityHandle winnerH(lootResult.topDamagerId);
    auto* winnerEntity = world_.getEntity(winnerH);
    if (winnerEntity) {
        auto* winnerNameplate = winnerEntity->getComponent<NameplateComponent>();
        if (winnerNameplate) {
            bossMsg.winnerName = winnerNameplate->displayName;
        }
    }

    if (bossMsg.winnerName.empty()) return; // Winner disconnected

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    bossMsg.write(w);

    // Scene-scoped broadcast: only send to clients in the same scene
    server_.connections().forEach([&](ClientConnection& client) {
        if (client.playerEntityId == 0) return;
        EntityHandle ch(client.playerEntityId);
        auto* ce = world_.getEntity(ch);
        if (!ce) return;
        auto* cs = ce->getComponent<CharacterStatsComponent>();
        if (cs && cs->stats.currentScene == scene) {
            server_.sendTo(client.id, Channel::ReliableOrdered,
                          PacketType::SvBossLootOwner, buf, w.size());
        }
    });
}
```

Add the declaration to `server/server_app.h`:
```cpp
void broadcastBossKillNotification(const EnemyStats& es,
                                    const EnemyStats::LootOwnerResult& lootResult,
                                    const std::string& scene);
```

- [ ] **Step 2: Call from skill kill path**

In `server/server_app.cpp`, after `getTopDamagerPartyAware()` returns in the skill kill path (~line 2646), determine the scene from the dead mob's entity or the attacker and call:

```cpp
// Get scene from the caster's current scene
std::string killScene;
if (casterStatsComp) killScene = casterStatsComp->stats.currentScene;
broadcastBossKillNotification(es, lootResult, killScene);
```

- [ ] **Step 3: Call from auto-attack kill path**

In the auto-attack kill path (~line 2905), same pattern:

```cpp
std::string killScene;
if (charStats) killScene = charStats->stats.currentScene;
broadcastBossKillNotification(es, lootResult, killScene);
```

- [ ] **Step 4: Build**

```bash
touch server/server_app.cpp server/server_app.h
cmake --build build --target FateServer
```

Expected: Compiles without errors.

- [ ] **Step 5: Run full test suite**

```bash
cmake --build build --target fate_tests && ./build/fate_tests
```

Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add server/server_app.cpp server/server_app.h
git commit -m "feat: broadcast scene-scoped boss kill notification with top damager"
```

---

### Task 7: Client-Side Boss Notification Handling

**Files:**
- Modify: `engine/net/net_client.h` — Add `onBossLootOwner` callback
- Modify: `engine/net/net_client.cpp` — Handle `SvBossLootOwner` packet
- Modify: Client game_app (wherever `onCombatEvent` etc. are bound) — Display chat message

- [ ] **Step 1: Add callback to NetClient**

In `engine/net/net_client.h`, add alongside the other `on*` callbacks:

```cpp
std::function<void(const SvBossLootOwnerMsg&)> onBossLootOwner;
```

- [ ] **Step 2: Handle packet in NetClient**

In `engine/net/net_client.cpp`, in the packet switch statement, add after the existing `SvLevelUp` case:

```cpp
case PacketType::SvBossLootOwner: {
    ByteReader payload(data + r.position(), hdr.payloadSize);
    auto msg = SvBossLootOwnerMsg::read(payload);
    if (onBossLootOwner) onBossLootOwner(msg);
    break;
}
```

- [ ] **Step 3: Bind callback in client game app**

In the client game app (where other callbacks like `onCombatEvent` are bound), add:

```cpp
netClient_.onBossLootOwner = [this](const SvBossLootOwnerMsg& msg) {
    // Look up boss display name from mob def cache using bossId string
    std::string bossName = msg.bossId;
    auto* mobDef = mobDefCache_.findById(msg.bossId);
    if (mobDef) bossName = mobDef->displayName;

    std::string text = bossName + " defeated! Top damager: " + msg.winnerName
                     + " (" + std::to_string(msg.topDamage) + " damage)";
    chatManager_.addSystemMessage(text);
};
```

Adjust to match the actual chat system and mob def cache API.

- [ ] **Step 4: Build client**

```bash
touch engine/net/net_client.cpp engine/net/net_client.h
cmake --build build --target FateEngine
```

Expected: Compiles.

- [ ] **Step 5: Commit**

```bash
git add engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: client handles boss kill notification as system chat message"
```

---

### Task 8: PK Status Replication — Protocol Changes

**Files:**
- Modify: `engine/net/protocol.h` — Add `pkStatus` to `SvEntityEnterMsg` and bit 14 to `SvEntityUpdateMsg`
- Modify: `tests/test_protocol.cpp` — Serialization tests

- [ ] **Step 1: Write serialization tests for pkStatus in entity messages**

Add to `tests/test_protocol.cpp`:

```cpp
TEST_CASE("SvEntityEnterMsg includes pkStatus for players") {
    SvEntityEnterMsg original;
    original.entityType = 0; // player
    original.persistentId = 12345;
    original.name = "TestPlayer";
    original.pkStatus = 2; // Red

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvEntityEnterMsg::read(r);

    CHECK(decoded.entityType == 0);
    CHECK(decoded.pkStatus == 2);
}

TEST_CASE("SvEntityEnterMsg mob does not carry pkStatus") {
    SvEntityEnterMsg original;
    original.entityType = 1; // mob
    original.persistentId = 99;
    original.name = "Goblin";
    original.pkStatus = 0; // should be ignored for mobs

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvEntityEnterMsg::read(r);

    CHECK(decoded.entityType == 1);
    CHECK(decoded.pkStatus == 0); // default, not serialized for mobs
}

TEST_CASE("SvEntityUpdateMsg bit 14 carries pkStatus") {
    SvEntityUpdateMsg original;
    original.persistentId = 555;
    original.fieldMask = (1 << 14); // only pkStatus
    original.pkStatus = 3; // Black

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    original.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvEntityUpdateMsg::read(r);

    CHECK(decoded.fieldMask & (1 << 14));
    CHECK(decoded.pkStatus == 3);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
touch tests/test_protocol.cpp
cmake --build build --target fate_tests && ./build/fate_tests -tc="*pkStatus*"
```

Expected: FAIL — `pkStatus` field doesn't exist on these structs yet.

- [ ] **Step 3: Add `pkStatus` to `SvEntityEnterMsg`**

In `engine/net/protocol.h`, add field to `SvEntityEnterMsg`:

```cpp
    uint8_t pkStatus = 0; // PKStatus enum (only for entityType == 0, player)
```

There is currently no `if (entityType == 0)` block for player-specific fields — create one. In `write()`, after the shared fields and before the mob block:

```cpp
    if (entityType == 0) {
        w.writeU8(pkStatus);
    }
```

In `read()`, in the same position:

```cpp
    if (m.entityType == 0) {
        m.pkStatus = r.readU8();
    }
```

- [ ] **Step 4: Add bit 14 (pkStatus) to `SvEntityUpdateMsg`**

Add field after the bit 13 comment:
```cpp
    // Bit 14: pkStatus (uint8, 1B) — PK name color (0=White, 1=Purple, 2=Red, 3=Black)
    uint8_t pkStatus = 0;
```

In `write()`, add after the bit 13 block:
```cpp
    if (fieldMask & (1 << 14)) w.writeU8(pkStatus);
```

In `read()`, add after the bit 13 block:
```cpp
    if (m.fieldMask & (1 << 14)) m.pkStatus = r.readU8();
```

- [ ] **Step 5: Build and run tests**

```bash
touch engine/net/protocol.h tests/test_protocol.cpp
cmake --build build --target fate_tests && ./build/fate_tests -tc="*pkStatus*"
```

Expected: All 3 pkStatus protocol tests PASS.

- [ ] **Step 6: Run full test suite**

```bash
./build/fate_tests
```

Expected: All tests pass. Existing serialization tests that write/read `SvEntityEnterMsg` for player entities will now include the extra byte — if any test does byte-exact comparison, it will need updating.

- [ ] **Step 7: Commit**

```bash
git add engine/net/protocol.h tests/test_protocol.cpp
git commit -m "feat: add pkStatus to entity enter/update messages (bit 14)"
```

---

### Task 9: PK Status Replication — Server Wiring

**Files:**
- Modify: `engine/net/replication.cpp` — Populate pkStatus in `buildEnterMessage()`, `buildCurrentState()`, and delta check in `sendDiffs()`

- [ ] **Step 1: Populate pkStatus in `buildEnterMessage()`**

In `engine/net/replication.cpp`, in the player branch of `buildEnterMessage()` (~line 258-267, inside the `if (charStats)` block), add:

```cpp
    msg.pkStatus = static_cast<uint8_t>(charStats->stats.pkStatus);
```

- [ ] **Step 2: Populate pkStatus in `buildCurrentState()`**

In `buildCurrentState()` (~line 303-363), in the `if (charStats)` branch:

```cpp
    msg.pkStatus = static_cast<uint8_t>(charStats->stats.pkStatus);
```

- [ ] **Step 3: Add delta check in `sendDiffs()`**

In `sendDiffs()` (~line 173-203), after the bit 13 (`equipVisuals`) check:

```cpp
    if (current.pkStatus != last.pkStatus)
        dirtyMask |= (1 << 14);
```

- [ ] **Step 4: Build**

```bash
touch engine/net/replication.cpp
cmake --build build --target FateServer
```

Expected: Compiles.

- [ ] **Step 5: Run full test suite**

```bash
cmake --build build --target fate_tests && ./build/fate_tests
```

Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add engine/net/replication.cpp
git commit -m "feat: replicate pkStatus to remote players via entity enter/update"
```

---

### Task 10: PK Name Color — Client Handling

**Files:**
- Modify: Client entity handling code (game_app.cpp or gameplay_system.h) — Apply `pkStatusColor()` on remote player enter/update

- [ ] **Step 1: Apply pkStatus color on entity enter**

In the client code that handles `onEntityEnter` (find by searching for where `SvEntityEnterMsg` is processed and nameplates are created), when a player entity (`entityType == 0`) enters, apply the PK color to their nameplate:

```cpp
// In onEntityEnter handler, after nameplate setup for remote players:
if (msg.entityType == 0) {
    nameplate->nameColor = pkStatusColor(static_cast<PKStatus>(msg.pkStatus));
}
```

- [ ] **Step 2: Apply pkStatus color on entity update**

In the `onEntityUpdate` handler, find where other fieldMask bits are processed (e.g., bit 3 for `currentHP`). Follow the same pattern to handle bit 14:

```cpp
if (msg.fieldMask & (1 << 14)) {
    auto* nameplate = entity->getComponent<NameplateComponent>();
    if (nameplate) {
        nameplate->nameColor = pkStatusColor(static_cast<PKStatus>(msg.pkStatus));
    }
}
```

- [ ] **Step 3: Build client**

```bash
touch <all modified client .cpp files>
cmake --build build --target FateEngine
```

Expected: Compiles.

- [ ] **Step 4: Commit**

```bash
git add <modified client files>
git commit -m "feat: apply PK name color to remote player nameplates on enter/update"
```

---

### Task 11: Final Integration Test and Cleanup

- [ ] **Step 1: Run full test suite**

```bash
cmake --build build --target fate_tests && ./build/fate_tests
```

Expected: All tests pass (should be ~490+ tests now).

- [ ] **Step 2: Build both server and client**

```bash
cmake --build build --target FateServer && cmake --build build --target FateEngine
```

Expected: Both compile cleanly.

- [ ] **Step 3: Verify test count**

```bash
./build/fate_tests -c
```

Record the final test count for the handoff document.

- [ ] **Step 4: Final commit if any cleanup needed**

Only if minor adjustments were needed during integration testing.

**REMINDER:** After all server changes, restart FateServer.exe before testing in-game.
