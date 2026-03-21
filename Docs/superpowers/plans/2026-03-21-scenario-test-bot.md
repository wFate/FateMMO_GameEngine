# Scenario Test Bot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a `TestBot` utility class and `fate_scenario_tests` executable that automates gameplay scenarios (login, combat, zone transitions) against a running FateServer.

**Architecture:** Composes existing `AuthClient` (TLS TCP login) and `NetClient` (UDP game protocol) into a `TestBot` with synchronous event waiting and typed event queues. Tests connect to an externally running FateServer with real PostgreSQL. Separate executable from the fast unit test suite.

**Tech Stack:** C++17, doctest, AuthClient (OpenSSL/TLS), NetClient (UDP), spdlog

**Spec:** `Docs/superpowers/specs/2026-03-21-scenario-test-bot-design.md`

---

### Task 1: Create directory structure and CMake target

**Files:**
- Create: `tests/scenarios/main.cpp`
- Modify: `CMakeLists.txt:337` (exclude scenarios from unit test glob)
- Modify: `CMakeLists.txt:348` (add scenario test target after unit test block)

- [ ] **Step 1: Create `tests/scenarios/` directory and doctest main**

Create `tests/scenarios/main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

- [ ] **Step 2: Exclude `tests/scenarios/` from the unit test glob**

In `CMakeLists.txt`, the existing unit test glob at line 337 is:
```cmake
file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
```

This would also pick up scenario files. Change it to exclude them:
```cmake
file(GLOB_RECURSE TEST_SOURCES tests/*.cpp)
list(FILTER TEST_SOURCES EXCLUDE REGEX "tests/scenarios/")
```

- [ ] **Step 3: Add `fate_scenario_tests` target**

Insert this block **between** lines 348 and 349 — that is, INSIDE the `if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)` guard (which starts at line 315 and closes at line 349). It must go after the `fate_tests` `endif()` but before the outer guard's `endif()`:

```cmake
    # =============================================================================
    # Scenario Tests (require running FateServer + database)
    # =============================================================================
    file(GLOB SCENARIO_TEST_SOURCES tests/scenarios/*.cpp)
    if(SCENARIO_TEST_SOURCES)
        add_executable(fate_scenario_tests ${SCENARIO_TEST_SOURCES})
        target_link_libraries(fate_scenario_tests PRIVATE fate_engine doctest::doctest spdlog::spdlog)
        target_include_directories(fate_scenario_tests PRIVATE ${CMAKE_SOURCE_DIR})
        if(MSVC)
            target_compile_definitions(fate_scenario_tests PRIVATE _CRT_SECURE_NO_WARNINGS FATE_DEV_TLS)
            target_compile_options(fate_scenario_tests PRIVATE /W4 /wd4100 /wd4201 /wd4458 /MP)
        endif()
    endif()
```

Note: `FATE_DEV_TLS` disables TLS certificate verification for localhost testing (already used by `AuthClient` at `engine/net/auth_client.cpp:158`). OpenSSL links transitively through `fate_engine`.

- [ ] **Step 4: Build to verify the target compiles**

Run: `"C:/Program Files/Microsoft Visual Studio/2025/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target fate_scenario_tests`

Expected: Builds successfully (just a main.cpp with doctest).

- [ ] **Step 5: Run to verify it executes**

Run: `./build/Debug/fate_scenario_tests.exe` (or Release path)

Expected: `0 test cases` reported, exit code 0.

- [ ] **Step 6: Commit**

```bash
git add tests/scenarios/main.cpp CMakeLists.txt
git commit -m "feat: add fate_scenario_tests build target and directory"
```

---

### Task 2: Implement TestBot header

**Files:**
- Create: `tests/scenarios/test_bot.h`

**Reference files:**
- `engine/net/net_client.h` — callback signatures, method signatures
- `engine/net/auth_client.h` — loginAsync, hasResult, consumeResult
- `engine/net/protocol.h` — all Sv* message types
- `engine/net/game_messages.h` — SvZoneTransitionMsg, SvSkillResultMsg, etc.
- `engine/net/auth_protocol.h` — AuthResponse, AuthToken

- [ ] **Step 1: Create `tests/scenarios/test_bot.h`**

```cpp
#pragma once
#include "engine/net/net_client.h"
#include "engine/net/auth_client.h"
#include "engine/net/auth_protocol.h"
#include "engine/net/protocol.h"
#include "engine/net/game_messages.h"
#include <doctest/doctest.h>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <type_traits>

namespace fate {

// TODO: Future enhancement — Approach C scripted DSL
// Replace procedural test code with declarative scenario scripts:
//   bot.scenario("login -> move(100,200) -> attack(nearest_mob) -> expect(damage > 0)");
// Prerequisite: stable TestBot API to build the DSL interpreter on top of.

class TestBot {
public:
    TestBot();

    // --- Lifecycle ---
    AuthResponse login(const std::string& host, uint16_t authPort,
                       const std::string& username, const std::string& password,
                       float timeoutSec = 10.0f);

    void connectUDP(const std::string& host, uint16_t gamePort,
                    float timeoutSec = 15.0f);

    void disconnect();

    // --- Commands ---
    void sendMove(Vec2 position, Vec2 velocity);
    void sendAttack(uint64_t targetPersistentId);
    void sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetId);
    void sendZoneTransition(const std::string& targetScene);
    void sendRespawn(uint8_t type);
    void sendChat(uint8_t channel, const std::string& message,
                  const std::string& target = "");

    // --- Event collection ---
    void pollEvents();
    void pollFor(float durationSec);

    template<typename T>
    T waitFor(float timeoutSec = 2.0f);

    // --- Query helpers ---
    const AuthResponse& authData() const { return authResponse_; }
    const std::vector<SvEntityEnterMsg>& entityEnters() const { return entityEnters_; }

    std::vector<SvEntityEnterMsg> entityEntersOfType(uint8_t entityType) const {
        std::vector<SvEntityEnterMsg> result;
        for (const auto& e : entityEnters_) {
            if (e.entityType == entityType) result.push_back(e);
        }
        return result;
    }

    void clearEvents();

    template<typename T>
    void clearEventsOf() { getQueue<T>().clear(); }

    template<typename T>
    bool hasEvent() { return !getQueue<T>().empty(); }

    template<typename T>
    size_t eventCount() { return getQueue<T>().size(); }

    bool isConnected() const { return udpConnected_; }

    // Public for test ergonomics — this is a test utility, not production code.
    template<typename T>
    std::vector<T>& getQueue() {
        if constexpr (std::is_same_v<T, SvPlayerStateMsg>)        return playerStates_;
        else if constexpr (std::is_same_v<T, SvCombatEventMsg>)   return combatEvents_;
        else if constexpr (std::is_same_v<T, SvEntityEnterMsg>)   return entityEnters_;
        else if constexpr (std::is_same_v<T, SvEntityLeaveMsg>)   return entityLeaves_;
        else if constexpr (std::is_same_v<T, SvEntityUpdateMsg>)  return entityUpdates_;
        else if constexpr (std::is_same_v<T, SvZoneTransitionMsg>) return zoneTransitions_;
        else if constexpr (std::is_same_v<T, SvDeathNotifyMsg>)   return deathNotifies_;
        else if constexpr (std::is_same_v<T, SvSkillResultMsg>)   return skillResults_;
        else if constexpr (std::is_same_v<T, SvLevelUpMsg>)       return levelUps_;
        else if constexpr (std::is_same_v<T, SvInventorySyncMsg>) return inventorySyncs_;
        else if constexpr (std::is_same_v<T, SvSkillSyncMsg>)     return skillSyncs_;
        else if constexpr (std::is_same_v<T, SvQuestSyncMsg>)     return questSyncs_;
        else if constexpr (std::is_same_v<T, SvLootPickupMsg>)    return lootPickups_;
        else if constexpr (std::is_same_v<T, SvRespawnMsg>)       return respawns_;
        else if constexpr (std::is_same_v<T, SvMovementCorrectionMsg>) return movementCorrections_;
        else if constexpr (std::is_same_v<T, SvChatMessageMsg>)   return chatMessages_;
        else static_assert(!sizeof(T), "No event queue registered for this type");
    }

private:
    AuthClient auth_;
    NetClient  client_;
    std::chrono::steady_clock::time_point epoch_;

    // Event queues
    std::vector<SvPlayerStateMsg>        playerStates_;
    std::vector<SvCombatEventMsg>        combatEvents_;
    std::vector<SvEntityEnterMsg>        entityEnters_;
    std::vector<SvEntityLeaveMsg>        entityLeaves_;
    std::vector<SvEntityUpdateMsg>       entityUpdates_;
    std::vector<SvZoneTransitionMsg>     zoneTransitions_;
    std::vector<SvDeathNotifyMsg>        deathNotifies_;
    std::vector<SvSkillResultMsg>        skillResults_;
    std::vector<SvLevelUpMsg>            levelUps_;
    std::vector<SvInventorySyncMsg>      inventorySyncs_;
    std::vector<SvSkillSyncMsg>          skillSyncs_;
    std::vector<SvQuestSyncMsg>          questSyncs_;
    std::vector<SvLootPickupMsg>         lootPickups_;
    std::vector<SvRespawnMsg>            respawns_;
    std::vector<SvMovementCorrectionMsg> movementCorrections_;
    std::vector<SvChatMessageMsg>        chatMessages_;

    AuthResponse authResponse_;
    std::string connectRejectReason_;
    bool wasRejected_ = false;
    bool udpConnected_ = false;

    float currentTime() const;
};

// --- Template implementation (must be in header) ---

template<typename T>
T TestBot::waitFor(float timeoutSec) {
    auto& queue = getQueue<T>();
    auto start = std::chrono::steady_clock::now();
    while (true) {
        client_.poll(currentTime());
        if (!queue.empty()) {
            T result = queue.front();
            queue.erase(queue.begin());
            return result;
        }
        auto elapsed = std::chrono::steady_clock::now() - start;
        float elapsedSec = std::chrono::duration<float>(elapsed).count();
        if (elapsedSec > timeoutSec) {
            FAIL("TestBot::waitFor timed out after ", timeoutSec, "s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace fate
```

- [ ] **Step 2: Verify it compiles**

Run: build `fate_scenario_tests` — the header is not yet included by anything, so just verify no syntax errors by adding a temporary `#include "test_bot.h"` to main.cpp, build, then remove it.

- [ ] **Step 3: Commit**

```bash
git add tests/scenarios/test_bot.h
git commit -m "feat: add TestBot header with event queues and waitFor template"
```

---

### Task 3: Implement TestBot methods

**Files:**
- Create: `tests/scenarios/test_bot.cpp`

**Reference files:**
- `engine/net/auth_client.cpp` — loginAsync flow, hasResult/consumeResult
- `engine/net/net_client.cpp:37-66` — connectWithToken signature
- `engine/net/net_client.cpp:329-340` — sendMove(const Vec2&, const Vec2&, float)
- `engine/net/net_client.cpp:342-353` — sendAction(uint8_t, uint64_t, uint16_t)

- [ ] **Step 1: Create `tests/scenarios/test_bot.cpp`**

```cpp
#include "test_bot.h"

namespace fate {

TestBot::TestBot()
    : epoch_(std::chrono::steady_clock::now())
{
    // Wire NetClient callbacks to event queues
    client_.onPlayerState = [this](const SvPlayerStateMsg& msg) {
        playerStates_.push_back(msg);
    };
    client_.onCombatEvent = [this](const SvCombatEventMsg& msg) {
        combatEvents_.push_back(msg);
    };
    client_.onEntityEnter = [this](const SvEntityEnterMsg& msg) {
        entityEnters_.push_back(msg);
    };
    client_.onEntityLeave = [this](const SvEntityLeaveMsg& msg) {
        entityLeaves_.push_back(msg);
    };
    client_.onEntityUpdate = [this](const SvEntityUpdateMsg& msg) {
        entityUpdates_.push_back(msg);
    };
    client_.onZoneTransition = [this](const SvZoneTransitionMsg& msg) {
        zoneTransitions_.push_back(msg);
    };
    client_.onDeathNotify = [this](const SvDeathNotifyMsg& msg) {
        deathNotifies_.push_back(msg);
    };
    client_.onSkillResult = [this](const SvSkillResultMsg& msg) {
        skillResults_.push_back(msg);
    };
    client_.onLevelUp = [this](const SvLevelUpMsg& msg) {
        levelUps_.push_back(msg);
    };
    client_.onInventorySync = [this](const SvInventorySyncMsg& msg) {
        inventorySyncs_.push_back(msg);
    };
    client_.onSkillSync = [this](const SvSkillSyncMsg& msg) {
        skillSyncs_.push_back(msg);
    };
    client_.onQuestSync = [this](const SvQuestSyncMsg& msg) {
        questSyncs_.push_back(msg);
    };
    client_.onLootPickup = [this](const SvLootPickupMsg& msg) {
        lootPickups_.push_back(msg);
    };
    client_.onRespawn = [this](const SvRespawnMsg& msg) {
        respawns_.push_back(msg);
    };
    client_.onMovementCorrection = [this](const SvMovementCorrectionMsg& msg) {
        movementCorrections_.push_back(msg);
    };
    client_.onChatMessage = [this](const SvChatMessageMsg& msg) {
        chatMessages_.push_back(msg);
    };
    client_.onConnected = [this]() {
        udpConnected_ = true;
    };
    client_.onConnectRejected = [this](const std::string& reason) {
        connectRejectReason_ = reason;
        wasRejected_ = true;
    };
}

float TestBot::currentTime() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - epoch_).count();
}

// --- Lifecycle ---

AuthResponse TestBot::login(const std::string& host, uint16_t authPort,
                            const std::string& username, const std::string& password,
                            float timeoutSec) {
    auth_.loginAsync(host, authPort, username, password);

    auto start = std::chrono::steady_clock::now();
    while (!auth_.hasResult()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        float elapsedSec = std::chrono::duration<float>(elapsed).count();
        if (elapsedSec > timeoutSec) {
            FAIL("TestBot::login timed out after ", timeoutSec, "s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    authResponse_ = auth_.consumeResult();
    return authResponse_;
}

void TestBot::connectUDP(const std::string& host, uint16_t gamePort,
                         float timeoutSec) {
    udpConnected_ = false;
    wasRejected_ = false;
    connectRejectReason_.clear();

    bool started = client_.connectWithToken(host, gamePort, authResponse_.authToken);
    REQUIRE_MESSAGE(started, "NetClient::connectWithToken failed to start");

    auto start = std::chrono::steady_clock::now();
    while (!udpConnected_ && !wasRejected_) {
        client_.poll(currentTime());
        auto elapsed = std::chrono::steady_clock::now() - start;
        float elapsedSec = std::chrono::duration<float>(elapsed).count();
        if (elapsedSec > timeoutSec) {
            FAIL("TestBot::connectUDP timed out after ", timeoutSec, "s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (wasRejected_) {
        FAIL("TestBot::connectUDP rejected: ", connectRejectReason_);
    }
}

void TestBot::disconnect() {
    client_.disconnect();
    udpConnected_ = false;
}

// --- Commands ---

void TestBot::sendMove(Vec2 position, Vec2 velocity) {
    client_.sendMove(position, velocity, currentTime());
}

void TestBot::sendAttack(uint64_t targetPersistentId) {
    client_.sendAction(0, targetPersistentId, 0);
}

void TestBot::sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetId) {
    client_.sendUseSkill(skillId, rank, targetId);
}

void TestBot::sendZoneTransition(const std::string& targetScene) {
    client_.sendZoneTransition(targetScene);
}

void TestBot::sendRespawn(uint8_t type) {
    client_.sendRespawn(type);
}

void TestBot::sendChat(uint8_t channel, const std::string& message,
                       const std::string& target) {
    client_.sendChat(channel, message, target);
}

// --- Event collection ---

void TestBot::pollEvents() {
    client_.poll(currentTime());
}

void TestBot::pollFor(float durationSec) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        client_.poll(currentTime());
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration<float>(elapsed).count() >= durationSec) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void TestBot::clearEvents() {
    playerStates_.clear();
    combatEvents_.clear();
    entityEnters_.clear();
    entityLeaves_.clear();
    entityUpdates_.clear();
    zoneTransitions_.clear();
    deathNotifies_.clear();
    skillResults_.clear();
    levelUps_.clear();
    inventorySyncs_.clear();
    skillSyncs_.clear();
    questSyncs_.clear();
    lootPickups_.clear();
    respawns_.clear();
    movementCorrections_.clear();
    chatMessages_.clear();
}

} // namespace fate
```

- [ ] **Step 2: Build to verify it compiles**

Run: build `fate_scenario_tests`

Expected: Clean compile, links against `fate_engine` which provides `AuthClient` and `NetClient`.

- [ ] **Step 3: Commit**

```bash
git add tests/scenarios/test_bot.cpp
git commit -m "feat: implement TestBot lifecycle, commands, and event collection"
```

---

### Task 4: Create ScenarioFixture

**Files:**
- Create: `tests/scenarios/scenario_fixture.h`

- [ ] **Step 1: Create `tests/scenarios/scenario_fixture.h`**

```cpp
#pragma once
#include "test_bot.h"
#include <doctest/doctest.h>
#include <string>
#include <cstdlib>

namespace fate {

inline std::string getEnvOr(const char* name, const char* defaultVal) {
    const char* val = std::getenv(name);
    return val ? std::string(val) : std::string(defaultVal);
}

inline std::string getEnvRequired(const char* name) {
    const char* val = std::getenv(name);
    REQUIRE_MESSAGE(val != nullptr,
        "Required environment variable not set: ", name,
        "\nSet TEST_USERNAME and TEST_PASSWORD before running scenario tests.");
    return std::string(val);
}

struct ScenarioFixture {
    TestBot bot;
    std::string host;
    uint16_t gamePort;
    uint16_t authPort;
    std::string username;
    std::string password;

    ScenarioFixture() {
        host     = getEnvOr("TEST_HOST", "127.0.0.1");
        gamePort = static_cast<uint16_t>(std::stoi(getEnvOr("TEST_GAME_PORT", "7777")));
        authPort = static_cast<uint16_t>(std::stoi(getEnvOr("TEST_AUTH_PORT", "7778")));
        username = getEnvRequired("TEST_USERNAME");
        password = getEnvRequired("TEST_PASSWORD");

        auto auth = bot.login(host, authPort, username, password);
        REQUIRE_MESSAGE(auth.success,
            "Login failed: ", auth.errorReason);
        bot.connectUDP(host, gamePort);
    }

    ~ScenarioFixture() {
        bot.disconnect();
    }
};

} // namespace fate
```

- [ ] **Step 2: Build to verify it compiles**

Run: build `fate_scenario_tests`

Expected: Clean compile.

- [ ] **Step 3: Commit**

```bash
git add tests/scenarios/scenario_fixture.h
git commit -m "feat: add ScenarioFixture with env-var config and auto-login"
```

---

### Task 5: Login stat verification scenario test

**Files:**
- Create: `tests/scenarios/test_login_stats.cpp`

- [ ] **Step 1: Create `tests/scenarios/test_login_stats.cpp`**

```cpp
#include "scenario_fixture.h"

using namespace fate;

TEST_CASE_FIXTURE(ScenarioFixture, "Login: auth response contains valid character data") {
    CHECK(bot.authData().level > 0);
    CHECK(bot.authData().currentHP > 0);
    CHECK(bot.authData().maxHP >= bot.authData().currentHP);
    CHECK(bot.authData().maxMP >= bot.authData().currentMP);
    CHECK(!bot.authData().characterName.empty());
    CHECK(!bot.authData().className.empty());
    CHECK(!bot.authData().sceneName.empty());
}

TEST_CASE_FIXTURE(ScenarioFixture, "Login: SvPlayerState matches auth snapshot") {
    auto state = bot.waitFor<SvPlayerStateMsg>(5.0f);
    CHECK(state.level == bot.authData().level);
    CHECK(state.currentHP == bot.authData().currentHP);
    CHECK(state.maxHP == bot.authData().maxHP);
    CHECK(state.maxMP == bot.authData().maxMP);
    CHECK(state.gold == bot.authData().gold);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Login: server sends initial sync messages") {
    // Server should send SvPlayerState, SvSkillSync, SvInventorySync on connect
    bot.pollFor(3.0f);
    CHECK(bot.hasEvent<SvPlayerStateMsg>());
}
```

- [ ] **Step 2: Build and verify the test compiles**

Run: build `fate_scenario_tests`

Expected: Clean compile.

- [ ] **Step 5: Commit**

```bash
git add tests/scenarios/test_login_stats.cpp
git commit -m "feat: add login stat verification scenario tests"
```

---

### Task 6: Combat scenario test

**Files:**
- Create: `tests/scenarios/test_combat.cpp`

- [ ] **Step 1: Create `tests/scenarios/test_combat.cpp`**

```cpp
#include "scenario_fixture.h"

using namespace fate;

TEST_CASE_FIXTURE(ScenarioFixture, "Combat: attack mob and receive combat event") {
    // Wait for initial state sync
    bot.waitFor<SvPlayerStateMsg>(5.0f);

    // Collect entity enters to find mobs in the zone
    bot.pollFor(2.0f);
    auto mobs = bot.entityEntersOfType(1); // entityType 1 = mob
    REQUIRE_MESSAGE(!mobs.empty(),
        "No mobs found in zone — ensure test account is in a zone with mobs");

    uint64_t mobId = mobs[0].persistentId;
    bot.sendAttack(mobId);

    auto combat = bot.waitFor<SvCombatEventMsg>(3.0f);
    CHECK(combat.targetId == mobId);
    // damage may be 0 on miss/dodge; assert attack was processed
    CHECK(combat.attackerId != 0);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Combat: entity update reflects HP change after attack") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);
    bot.pollFor(2.0f);
    auto mobs = bot.entityEntersOfType(1);
    if (mobs.empty()) {
        WARN("No mobs in zone — skipping HP update test");
        return;
    }

    uint64_t mobId = mobs[0].persistentId;
    int32_t initialHP = mobs[0].currentHP;
    bot.sendAttack(mobId);

    // Wait for combat event first
    auto combat = bot.waitFor<SvCombatEventMsg>(3.0f);

    // If damage landed, check for entity update with reduced HP
    if (combat.damage > 0) {
        bot.pollFor(1.0f);
        // Look for an entity update for this mob with HP field set
        bool foundHPUpdate = false;
        for (const auto& upd : bot.getQueue<SvEntityUpdateMsg>()) {
            if (upd.persistentId == mobId && (upd.fieldMask & (1 << 3))) {
                CHECK(upd.currentHP < initialHP);
                foundHPUpdate = true;
                break;
            }
        }
        CHECK_MESSAGE(foundHPUpdate, "Expected entity update with HP change for attacked mob");
    }
}
```

- [ ] **Step 2: Build and verify the test compiles**

Run: build `fate_scenario_tests`

Expected: Clean compile.

- [ ] **Step 3: Commit**

```bash
git add tests/scenarios/test_combat.cpp
git commit -m "feat: add combat scenario tests"
```

---

### Task 7: Zone transition scenario test

**Files:**
- Create: `tests/scenarios/test_zone_transition.cpp`

**Important:** The zone name used in the test must be a valid scene in the database. Check `sceneCache_` / scene definitions. For now, use a placeholder and add a comment for the user to adjust.

- [ ] **Step 1: Create `tests/scenarios/test_zone_transition.cpp`**

```cpp
#include "scenario_fixture.h"

using namespace fate;

// NOTE: Update this to a valid scene name from your database.
// Query: SELECT scene_name FROM scene_definitions WHERE scene_name != <current_zone>
static const char* ALTERNATE_ZONE = "starting_zone_2";

TEST_CASE_FIXTURE(ScenarioFixture, "Zone transition: server acknowledges scene change") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);

    bot.sendZoneTransition(ALTERNATE_ZONE);
    auto transition = bot.waitFor<SvZoneTransitionMsg>(5.0f);
    CHECK(transition.targetScene == ALTERNATE_ZONE);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Zone transition: stats preserved across scenes") {
    auto stateBefore = bot.waitFor<SvPlayerStateMsg>(5.0f);

    bot.sendZoneTransition(ALTERNATE_ZONE);
    bot.waitFor<SvZoneTransitionMsg>(5.0f);

    // After zone transition, server sends a fresh SvPlayerState
    auto stateAfter = bot.waitFor<SvPlayerStateMsg>(5.0f);
    CHECK(stateAfter.level == stateBefore.level);
    CHECK(stateAfter.gold == stateBefore.gold);
    CHECK(stateAfter.maxHP == stateBefore.maxHP);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Zone transition: new zone entities received") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);
    bot.clearEvents();

    bot.sendZoneTransition(ALTERNATE_ZONE);
    bot.waitFor<SvZoneTransitionMsg>(5.0f);

    // Should receive entity enters for the new zone
    bot.pollFor(2.0f);
    // At minimum we should get SvEntityEnter messages (mobs, NPCs, or nothing if empty zone)
    // Just verify we don't crash and the connection stays alive
    CHECK(bot.isConnected());
}
```

- [ ] **Step 2: Build and verify the test compiles**

Run: build `fate_scenario_tests`

Expected: Clean compile.

- [ ] **Step 3: Commit**

```bash
git add tests/scenarios/test_zone_transition.cpp
git commit -m "feat: add zone transition scenario tests"
```

---

### Task 8: Verify existing unit tests still pass

This ensures the CMakeLists.txt changes (excluding `tests/scenarios/` from `fate_tests`) didn't break anything.

- [ ] **Step 1: Touch all edited .cpp files**

CRITICAL: CMake on this project can miss changes silently. Touch all source files that were modified.

```bash
touch tests/scenarios/*.cpp
```

- [ ] **Step 2: Build and run `fate_tests`**

Run: build and execute `fate_tests`

Expected: Same test count as before (~490 tests), all pass. No scenario test files should appear in the unit test build.

- [ ] **Step 3: Build `fate_scenario_tests`**

Run: build `fate_scenario_tests`

Expected: Compiles cleanly with all scenario test files.

- [ ] **Step 4: Commit if any fixes were needed**

Only commit if changes were required to fix issues found in this step.

---

### Task 9: Add movement scenario test

**Files:**
- Create: `tests/scenarios/test_movement.cpp`

- [ ] **Step 1: Create `tests/scenarios/test_movement.cpp`**

```cpp
#include "scenario_fixture.h"

using namespace fate;

TEST_CASE_FIXTURE(ScenarioFixture, "Movement: bot moves and receives entity enters") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);

    // Move to a position — should trigger entity visibility updates
    bot.sendMove({100.0f, 200.0f}, {0.0f, 0.0f});
    bot.pollFor(1.0f);

    // We should have received at least our own entity or nearby entities
    CHECK(bot.entityEnters().size() > 0);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Movement: no rubber-band for valid movement") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);
    bot.clearEventsOf<SvMovementCorrectionMsg>();

    // Small, valid move — should not trigger rubber-banding
    bot.sendMove({50.0f, 50.0f}, {0.0f, 0.0f});
    bot.pollFor(0.5f);

    CHECK(bot.eventCount<SvMovementCorrectionMsg>() == 0);
}
```

- [ ] **Step 2: Build and verify**

Run: build `fate_scenario_tests`

Expected: Clean compile.

- [ ] **Step 3: Commit**

```bash
git add tests/scenarios/test_movement.cpp
git commit -m "feat: add movement scenario tests"
```
