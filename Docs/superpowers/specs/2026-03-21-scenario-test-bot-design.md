# Scenario Test Bot Design

## Summary

A `TestBot` utility class and separate `fate_scenario_tests` executable that automates gameplay scenarios (login, movement, combat, zone transitions) against a running FateServer. Composes the existing `AuthClient` and `NetClient` classes, adding synchronous event waiting and typed event queues for test assertions.

## Decisions

- **Real database**: Tests connect to the running FateServer with real PostgreSQL, not mocks
- **External server**: Tests require a manually started FateServer.exe (no in-process server)
- **Separate executable**: `fate_scenario_tests` is independent from the fast `fate_tests` unit suite
- **Credentials via env vars**: `TEST_USERNAME`, `TEST_PASSWORD`, `TEST_HOST`, `TEST_GAME_PORT`, `TEST_AUTH_PORT`
- **Approach A (composition)**: Wraps existing AuthClient + NetClient; no raw socket duplication
- **Test idempotency**: Tests should tolerate any starting character state (position, HP, zone). The test account may drift over repeated runs; tests assert relative invariants, not absolute values. No DB reset mechanism needed initially.

## TestBot Class

```cpp
class TestBot {
    AuthClient auth_;
    NetClient  client_;

    // Monotonic clock for NetClient::poll(float currentTime).
    // Epoch = TestBot construction time. All poll calls use seconds since epoch.
    std::chrono::steady_clock::time_point epoch_;

    // Event queues - one vector per server message type that scenarios use.
    // NetClient callbacks not listed here (onTradeUpdate, onMarketResult,
    // onBountyUpdate, onGauntletUpdate, onGuildUpdate, onSocialUpdate,
    // onQuestUpdate) are intentionally unhooked — the server silently
    // ignores null callbacks for unrequested message types. These can be
    // added later as scenario coverage expands.
    std::vector<SvPlayerStateMsg>     playerStates_;
    std::vector<SvCombatEventMsg>     combatEvents_;
    std::vector<SvEntityEnterMsg>     entityEnters_;
    std::vector<SvEntityLeaveMsg>     entityLeaves_;
    std::vector<SvEntityUpdateMsg>    entityUpdates_;
    std::vector<SvZoneTransitionMsg>  zoneTransitions_;
    std::vector<SvDeathNotifyMsg>     deathNotifies_;
    std::vector<SvSkillResultMsg>     skillResults_;
    std::vector<SvLevelUpMsg>         levelUps_;
    std::vector<SvInventorySyncMsg>   inventorySyncs_;
    std::vector<SvSkillSyncMsg>       skillSyncs_;
    std::vector<SvQuestSyncMsg>       questSyncs_;
    std::vector<SvLootPickupMsg>      lootPickups_;
    std::vector<SvRespawnMsg>         respawns_;
    std::vector<SvMovementCorrectionMsg> movementCorrections_;
    std::vector<SvChatMessageMsg>     chatMessages_;

    // Stored from login() — connectUDP() reads authResponse_.authToken internally.
    // Precondition: login() must be called before connectUDP().
    AuthResponse authResponse_;

    // Connection rejection reason (from onConnectRejected callback)
    std::string connectRejectReason_;
    bool wasRejected_ = false;

    // Returns seconds since epoch_ for NetClient::poll()
    float currentTime() const;

public:
    // --- Lifecycle ---

    // Synchronous login: calls AuthClient::loginAsync(), spins until
    // hasResult() or timeout. Stores result in authResponse_.
    // Returns AuthResponse. REQUIRE-fails on timeout.
    AuthResponse login(const std::string& host, uint16_t authPort,
                       const std::string& username, const std::string& password,
                       float timeoutSec = 10.0f);

    // UDP connect using authResponse_.authToken (from prior login() call).
    // Calls NetClient::connectWithToken(), polls until onConnected fires
    // or timeout. REQUIRE-fails on timeout or rejection.
    void connectUDP(const std::string& host, uint16_t gamePort,
                    float timeoutSec = 15.0f);

    void disconnect();

    // --- Commands (thin wrappers around NetClient) ---

    // Auto-generates timestamp from monotonic clock.
    // Delegates to client_.sendMove(position, velocity, currentTime()).
    void sendMove(Vec2 position, Vec2 velocity);

    // Convenience for basic attack. Delegates to client_.sendAction(0, targetId, 0).
    void sendAttack(uint64_t targetPersistentId);

    void sendUseSkill(const std::string& skillId, uint8_t rank, uint64_t targetId);
    void sendZoneTransition(const std::string& targetScene);
    void sendRespawn(uint8_t type);
    void sendChat(uint8_t channel, const std::string& message,
                  const std::string& target = "");

    // --- Event collection ---

    // Single-pass: calls client_.poll(currentTime()) once, draining all
    // buffered packets into event queues. Does not loop or sleep.
    void pollEvents();

    // Loops pollEvents() + sleep(5ms) for the given duration,
    // collecting all events that arrive. Does NOT fail on timeout.
    void pollFor(float durationSec);

    // Loops pollEvents() + sleep(5ms) until an event of type T appears
    // or timeout. REQUIRE-fails if timeout expires without the event.
    template<typename T>
    T waitFor(float timeoutSec = 2.0f);

    // --- Query helpers ---

    const AuthResponse& authData() const;
    const std::vector<SvEntityEnterMsg>& entityEnters() const;
    std::vector<SvEntityEnterMsg> entityEntersOfType(uint8_t entityType) const;
    // Clear all event queues (useful between test phases)
    void clearEvents();
    // Clear a specific event queue
    template<typename T> void clearEventsOf();
};
```

### Constructor Wiring

The constructor hooks every `NetClient` callback to push into the corresponding vector:

```cpp
TestBot::TestBot() {
    client_.onPlayerState = [this](const SvPlayerStateMsg& msg) {
        playerStates_.push_back(msg);
    };
    client_.onCombatEvent = [this](const SvCombatEventMsg& msg) {
        combatEvents_.push_back(msg);
    };
    // ... one per callback
}
```

### waitFor Implementation

`getQueue<T>()` uses `if constexpr` chains to map each message type to its
corresponding `std::vector<T>&` member at compile time.

```cpp
template<typename T>
std::vector<T>& TestBot::getQueue() {
    if constexpr (std::is_same_v<T, SvPlayerStateMsg>)    return playerStates_;
    else if constexpr (std::is_same_v<T, SvCombatEventMsg>) return combatEvents_;
    // ... one branch per queued type
    else static_assert(!sizeof(T), "No event queue for this type");
}

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
        if (elapsed > std::chrono::duration<float>(timeoutSec)) {
            FAIL("TestBot::waitFor timed out after " + std::to_string(timeoutSec) + "s");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
```

## Test Fixture

```cpp
struct ScenarioFixture {
    TestBot bot;
    std::string host;
    uint16_t gamePort;
    uint16_t authPort;
    std::string username;
    std::string password;

    ScenarioFixture() {
        host = getEnvOr("TEST_HOST", "127.0.0.1");
        gamePort = static_cast<uint16_t>(std::stoi(getEnvOr("TEST_GAME_PORT", "7777")));
        authPort = static_cast<uint16_t>(std::stoi(getEnvOr("TEST_AUTH_PORT", "7778")));
        username = getEnvRequired("TEST_USERNAME");
        password = getEnvRequired("TEST_PASSWORD");

        auto auth = bot.login(host, authPort, username, password);
        REQUIRE(auth.success);
        bot.connectUDP(host, gamePort);
    }

    ~ScenarioFixture() {
        bot.disconnect();
    }

private:
    static std::string getEnvOr(const char* name, const char* defaultVal);
    static std::string getEnvRequired(const char* name); // REQUIRE-fails if missing
};
```

## Scenario Tests

### 1. Login stat verification

```cpp
TEST_CASE_FIXTURE(ScenarioFixture, "Login: character stats match expected values") {
    CHECK(bot.authData().level > 0);
    CHECK(bot.authData().currentHP > 0);
    CHECK(bot.authData().maxHP >= bot.authData().currentHP);
    CHECK(bot.authData().maxMP >= bot.authData().currentMP);

    auto state = bot.waitFor<SvPlayerStateMsg>(5.0f);
    CHECK(state.level == bot.authData().level);
    CHECK(state.currentHP == bot.authData().currentHP);
    CHECK(state.maxHP == bot.authData().maxHP);
    CHECK(state.gold == bot.authData().gold);
}
```

### 2. Movement and entity visibility

```cpp
TEST_CASE_FIXTURE(ScenarioFixture, "Movement: bot moves and receives entity updates") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);
    bot.sendMove({100.0f, 200.0f}, {0.0f, 0.0f});
    bot.pollFor(1.0f);
    CHECK(bot.entityEnters().size() > 0);
}
```

### 3. Combat with a mob

```cpp
TEST_CASE_FIXTURE(ScenarioFixture, "Combat: attack mob and receive damage event") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);
    bot.pollFor(2.0f);
    auto mobs = bot.entityEntersOfType(1);
    REQUIRE(!mobs.empty());

    uint64_t mobId = mobs[0].persistentId;
    bot.sendAttack(mobId);
    auto combat = bot.waitFor<SvCombatEventMsg>(3.0f);
    CHECK(combat.targetId == mobId);
    // damage may be 0 on miss/dodge; assert attack was processed
    CHECK(combat.attackerId != 0);
}
```

### 4. Zone transition preserves stats

```cpp
TEST_CASE_FIXTURE(ScenarioFixture, "Zone transition: stats preserved across scenes") {
    auto stateBefore = bot.waitFor<SvPlayerStateMsg>(5.0f);

    bot.sendZoneTransition("starting_zone_2");
    auto transition = bot.waitFor<SvZoneTransitionMsg>(5.0f);
    CHECK(transition.targetScene == "starting_zone_2");

    auto stateAfter = bot.waitFor<SvPlayerStateMsg>(5.0f);
    CHECK(stateAfter.level == stateBefore.level);
    CHECK(stateAfter.gold == stateBefore.gold);
    CHECK(stateAfter.maxHP == stateBefore.maxHP);
}
```

## Build Target

Add to `CMakeLists.txt`:

```cmake
# Scenario tests (require running FateServer + database)
# Guarded same as FateServer — not available on iOS/Android
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND NOT ANDROID)
    file(GLOB SCENARIO_TEST_SOURCES tests/scenarios/*.cpp)
    if(SCENARIO_TEST_SOURCES)
        add_executable(fate_scenario_tests ${SCENARIO_TEST_SOURCES})
        target_link_libraries(fate_scenario_tests PRIVATE fate_engine doctest::doctest spdlog::spdlog)
        target_include_directories(fate_scenario_tests PRIVATE ${CMAKE_SOURCE_DIR})
    endif()
endif()
```

OpenSSL is linked transitively through `fate_engine`.

Scenario test files live in `tests/scenarios/` to keep them separate from unit tests in `tests/`.

### File structure

```
tests/
  scenarios/
    test_bot.h            -- TestBot class declaration
    test_bot.cpp          -- TestBot implementation
    scenario_fixture.h    -- ScenarioFixture (env vars, login, connect)
    test_login_stats.cpp  -- Login stat verification scenarios
    test_combat.cpp       -- Combat scenarios
    test_zone_transition.cpp -- Zone transition scenarios
    main.cpp              -- doctest main (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)
```

## Future: Approach C (Scripted DSL)

A future enhancement once the TestBot API stabilizes. Replace procedural test code with declarative scenario scripts:

```
bot.scenario("login -> move(100,200) -> attack(nearest_mob) -> expect(damage > 0)");
```

Benefits:
- Non-programmers could author test scenarios
- Scenarios could be batch-run from text files
- Data-driven test matrices (class x level x zone combinations)

Prerequisite: stable TestBot API to build the DSL interpreter on top of.
