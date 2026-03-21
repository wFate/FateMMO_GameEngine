# Scenario Testing Guide

Automated gameplay scenario tests that connect to a running FateServer and verify login, combat, movement, and zone transitions against the real database.

## Quick Start

1. Start FateServer.exe
2. Set environment variables:
   ```bash
   export TEST_USERNAME="your_test_account"
   export TEST_PASSWORD="your_test_password"
   ```
3. Run:
   ```bash
   ./build/Debug/fate_scenario_tests.exe
   ```

Optional env vars (with defaults):
- `TEST_HOST` (default: `127.0.0.1`)
- `TEST_GAME_PORT` (default: `7777`)
- `TEST_AUTH_PORT` (default: `7778`)

## Test Account Setup

Create a dedicated test account in the database. The account needs:
- A character at level 1+ in a zone that has mobs (for combat tests)
- Valid credentials for TCP auth login

Tests are idempotent -- they tolerate any starting character state (position, HP, zone). The test character's state may drift over repeated runs.

## What's Tested

| File | Tests | What it verifies |
|------|-------|-----------------|
| `test_login_stats.cpp` | 3 | Auth response has valid data, SvPlayerState matches auth snapshot, server sends initial sync |
| `test_combat.cpp` | 2 | Attack mob produces SvCombatEvent, entity update reflects HP change |
| `test_movement.cpp` | 2 | Movement triggers entity visibility, valid moves don't rubber-band |
| `test_zone_transition.cpp` | 3 | Server acknowledges zone change, stats preserved, connection stays alive |

## Architecture

```
fate_scenario_tests.exe
  |
  +-- ScenarioFixture (scenario_fixture.h)
  |     Reads env vars, auto-logins + UDP connects per test case
  |
  +-- TestBot (test_bot.h / test_bot.cpp)
        Wraps AuthClient (TLS TCP login) + NetClient (UDP gameplay)
        Event queues for all 16 server message types
        Synchronous helpers: waitFor<T>(), pollFor(), pollEvents()
```

Each `TEST_CASE_FIXTURE(ScenarioFixture, ...)` gets a fresh login + UDP connection. The fixture disconnects in the destructor.

## TestBot API Reference

### Lifecycle

```cpp
AuthResponse login(host, authPort, username, password, timeout=10s);
void connectUDP(host, gamePort, timeout=15s);  // uses token from login()
void disconnect();
```

### Commands

```cpp
bot.sendMove({x, y}, {vx, vy});           // auto-generates timestamp
bot.sendAttack(targetPersistentId);         // maps to sendAction(0, id, 0)
bot.sendUseSkill(skillId, rank, targetId);
bot.sendZoneTransition(sceneName);
bot.sendRespawn(type);                      // 0=town, 1=map, 2=here
bot.sendChat(channel, message, target);
```

### Event Collection

```cpp
auto msg = bot.waitFor<SvPlayerStateMsg>(5.0f);  // blocks until received, FAILs on timeout
bot.pollFor(2.0f);                                // collect events for N seconds (no fail)
bot.pollEvents();                                 // single-pass drain

bot.hasEvent<SvCombatEventMsg>();                 // check if queue has events
bot.eventCount<SvCombatEventMsg>();               // count in queue
bot.getQueue<SvEntityUpdateMsg>();                // direct access to event vector
bot.entityEnters();                               // shortcut for entity enter queue
bot.entityEntersOfType(1);                        // filter by type (0=player, 1=mob, 2=npc, 3=item)

bot.clearEvents();                                // clear all queues
bot.clearEventsOf<SvMovementCorrectionMsg>();      // clear one queue
bot.isConnected();                                // UDP connection alive?
bot.authData();                                   // AuthResponse from login
```

## Writing New Scenario Tests

Create a new `.cpp` file in `tests/scenarios/`:

```cpp
#include "scenario_fixture.h"

using namespace fate;

TEST_CASE_FIXTURE(ScenarioFixture, "My scenario: description") {
    // Wait for server to send initial state
    bot.waitFor<SvPlayerStateMsg>(5.0f);

    // Do something
    bot.sendAttack(someTargetId);

    // Verify result
    auto event = bot.waitFor<SvCombatEventMsg>(3.0f);
    CHECK(event.damage > 0);
}
```

Rebuild after adding files (CMake glob requires reconfigure):
```bash
cmake -S . -B build
cmake --build build --target fate_scenario_tests
```

## Configuration

The zone transition tests use `ALTERNATE_ZONE` in `test_zone_transition.cpp`. Update this to a valid scene name from your database:
```sql
SELECT scene_name FROM scene_definitions;
```

## Build Target

`fate_scenario_tests` is a separate executable from `fate_tests` (unit tests). Unit tests run fast with no external dependencies. Scenario tests require a running server + database.

Both build inside the `if(NOT iOS AND NOT ANDROID)` CMake guard.
