# Batch C: PvP Events & Rankings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement event scheduler FSM, MVP battlefield, arena (1v1/2v2/3v3), and honor ranking with badges.

**Architecture:** Event scheduler provides generic timer infrastructure. Battlefield registers as a scheduled event with kill tracking per faction. Arena is queue-based with matchmaking. Honor ranking is a derived display system replicated via entity messages. All systems use a central PlayerEventLock to prevent double-registration.

**Tech Stack:** C++23, doctest, custom UDP networking, ECS.

**IMPORTANT BUILD NOTE:** Touch ALL edited `.cpp` files before building. After server changes, restart FateServer.exe.

**Spec:** `Docs/superpowers/specs/2026-03-21-batch-c-pvp-events-design.md`

**Pattern reference:** `GauntletManager` in `game/shared/gauntlet.h` — follow its architecture for event lifecycle, player tracking, and server callbacks.

---

### Task 1: Event Scheduler FSM + Tests

**Files:**
- Create: `game/shared/event_scheduler.h`
- Create: `tests/test_event_scheduler.cpp`

- [ ] **Step 1: Create test file**

```cpp
#include <doctest/doctest.h>
#include "game/shared/event_scheduler.h"

using namespace fate;

TEST_SUITE("EventScheduler") {

TEST_CASE("Event starts in Idle state") {
    EventScheduler scheduler;
    auto id = scheduler.registerEvent({"battlefield", 7200.0f, 300.0f, 900.0f});
    CHECK(scheduler.getState(id) == EventState::Idle);
}

TEST_CASE("Transitions Idle → Signup after interval") {
    EventScheduler scheduler;
    bool signupFired = false;
    auto id = scheduler.registerEvent({"bf", 100.0f, 30.0f, 60.0f});
    scheduler.setCallback(id, EventCallback::OnSignupStart, [&]{ signupFired = true; });

    // Advance past interval
    scheduler.tick(101.0f);
    CHECK(scheduler.getState(id) == EventState::Signup);
    CHECK(signupFired);
}

TEST_CASE("Transitions Signup → Active after signup duration") {
    EventScheduler scheduler;
    bool startFired = false;
    auto id = scheduler.registerEvent({"bf", 10.0f, 5.0f, 15.0f});
    scheduler.setCallback(id, EventCallback::OnEventStart, [&]{ startFired = true; });

    scheduler.tick(11.0f); // → Signup
    scheduler.tick(16.0f); // → Active (11 + 5 = 16)
    CHECK(scheduler.getState(id) == EventState::Active);
    CHECK(startFired);
}

TEST_CASE("Transitions Active → Idle after active duration") {
    EventScheduler scheduler;
    bool endFired = false;
    auto id = scheduler.registerEvent({"bf", 10.0f, 5.0f, 15.0f});
    scheduler.setCallback(id, EventCallback::OnEventEnd, [&]{ endFired = true; });

    scheduler.tick(11.0f); // → Signup
    scheduler.tick(16.0f); // → Active
    scheduler.tick(31.0f); // → Idle (16 + 15 = 31)
    CHECK(scheduler.getState(id) == EventState::Idle);
    CHECK(endFired);
}

TEST_CASE("Cycles repeat") {
    EventScheduler scheduler;
    int signupCount = 0;
    auto id = scheduler.registerEvent({"bf", 10.0f, 2.0f, 3.0f});
    scheduler.setCallback(id, EventCallback::OnSignupStart, [&]{ signupCount++; });

    scheduler.tick(11.0f);  // cycle 1 signup
    scheduler.tick(13.0f);  // cycle 1 active
    scheduler.tick(16.0f);  // cycle 1 idle
    scheduler.tick(27.0f);  // cycle 2 signup (16 + 10 = 26, past it)
    CHECK(signupCount == 2);
}

TEST_CASE("getTimeRemaining returns correct value") {
    EventScheduler scheduler;
    auto id = scheduler.registerEvent({"bf", 10.0f, 5.0f, 15.0f});

    scheduler.tick(11.0f); // → Signup at t=11, ends at t=16
    CHECK(scheduler.getTimeRemaining(id) == doctest::Approx(5.0f));

    scheduler.tick(14.0f); // still Signup
    CHECK(scheduler.getTimeRemaining(id) == doctest::Approx(2.0f));
}

TEST_CASE("Multiple events run independently") {
    EventScheduler scheduler;
    auto bf = scheduler.registerEvent({"bf", 100.0f, 10.0f, 20.0f});
    auto ad = scheduler.registerEvent({"ad", 50.0f, 5.0f, 10.0f});

    scheduler.tick(51.0f);
    CHECK(scheduler.getState(bf) == EventState::Idle);
    CHECK(scheduler.getState(ad) == EventState::Signup);
}

} // TEST_SUITE
```

- [ ] **Step 2: Implement event_scheduler.h**

```cpp
#pragma once
#include <string>
#include <unordered_map>
#include <functional>

namespace fate {

enum class EventState : uint8_t { Idle = 0, Signup = 1, Active = 2 };
enum class EventCallback : uint8_t { OnSignupStart, OnEventStart, OnEventEnd, OnTick };

struct EventConfig {
    std::string eventId;
    float intervalSeconds = 7200.0f;
    float signupDuration = 300.0f;
    float activeDuration = 900.0f;
};

class EventScheduler {
public:
    std::string registerEvent(const EventConfig& config) {
        ScheduledEvent evt;
        evt.config = config;
        evt.state = EventState::Idle;
        evt.nextTransitionTime = config.intervalSeconds;
        events_[config.eventId] = evt;
        return config.eventId;
    }

    void setCallback(const std::string& eventId, EventCallback type, std::function<void()> cb) {
        auto it = events_.find(eventId);
        if (it == events_.end()) return;
        it->second.callbacks[static_cast<int>(type)] = std::move(cb);
    }

    void tick(float currentTime) {
        for (auto& [id, evt] : events_) {
            if (currentTime < evt.nextTransitionTime) continue;
            switch (evt.state) {
                case EventState::Idle:
                    evt.state = EventState::Signup;
                    evt.nextTransitionTime = currentTime + evt.config.signupDuration;
                    if (evt.callbacks[0]) evt.callbacks[0](); // OnSignupStart
                    break;
                case EventState::Signup:
                    evt.state = EventState::Active;
                    evt.nextTransitionTime = currentTime + evt.config.activeDuration;
                    if (evt.callbacks[1]) evt.callbacks[1](); // OnEventStart
                    break;
                case EventState::Active:
                    evt.state = EventState::Idle;
                    evt.nextTransitionTime = currentTime + evt.config.intervalSeconds;
                    if (evt.callbacks[2]) evt.callbacks[2](); // OnEventEnd
                    break;
            }
        }
    }

    EventState getState(const std::string& eventId) const {
        auto it = events_.find(eventId);
        return it != events_.end() ? it->second.state : EventState::Idle;
    }

    float getTimeRemaining(const std::string& eventId) const {
        auto it = events_.find(eventId);
        if (it == events_.end()) return 0.0f;
        return it->second.nextTransitionTime - lastTickTime_;
    }

private:
    struct ScheduledEvent {
        EventConfig config;
        EventState state = EventState::Idle;
        float nextTransitionTime = 0.0f;
        std::function<void()> callbacks[4]; // indexed by EventCallback
    };
    std::unordered_map<std::string, ScheduledEvent> events_;
    float lastTickTime_ = 0.0f;
};

} // namespace fate
```

Note: The `getTimeRemaining()` implementation needs `lastTickTime_` tracked in `tick()`. Add `lastTickTime_ = currentTime;` at the start of `tick()`.

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests --test-suite="EventScheduler"
```

- [ ] **Step 4: Commit**

```bash
git add game/shared/event_scheduler.h tests/test_event_scheduler.cpp
git commit -m "feat: add EventScheduler FSM with configurable intervals and callbacks"
```

---

### Task 2: Honor Ranking + Replication + Tests

**Files:**
- Modify: `game/shared/honor_system.h` — Add `HonorRank` enum, `getHonorRank()`, `getHonorRankName()`
- Modify: `engine/net/protocol.h` — Add `honorRank` to `SvPlayerStateMsg`, `SvEntityEnterMsg`, bit 15 to `SvEntityUpdateMsg`
- Modify: `engine/net/replication.cpp` — Populate honorRank in enter/update/diff
- Modify: `server/server_app.cpp` — Populate honorRank in `sendPlayerState()`
- Create: `tests/test_honor_ranking.cpp`

- [ ] **Step 1: Write honor ranking tests**

```cpp
#include <doctest/doctest.h>
#include "game/shared/honor_system.h"

using namespace fate;

TEST_SUITE("HonorRanking") {

TEST_CASE("Honor 0 = Recruit") {
    CHECK(HonorSystem::getHonorRank(0) == HonorRank::Recruit);
}

TEST_CASE("Honor 99 = Recruit") {
    CHECK(HonorSystem::getHonorRank(99) == HonorRank::Recruit);
}

TEST_CASE("Honor 100 = Scout") {
    CHECK(HonorSystem::getHonorRank(100) == HonorRank::Scout);
}

TEST_CASE("Honor 500 = CombatSoldier") {
    CHECK(HonorSystem::getHonorRank(500) == HonorRank::CombatSoldier);
}

TEST_CASE("Honor 99999 = General") {
    CHECK(HonorSystem::getHonorRank(99999) == HonorRank::General);
}

TEST_CASE("Honor 500000 = General (above threshold)") {
    CHECK(HonorSystem::getHonorRank(500000) == HonorRank::General);
}

TEST_CASE("All rank names are non-empty") {
    for (int i = 0; i <= 9; ++i) {
        auto name = HonorSystem::getHonorRankName(static_cast<HonorRank>(i));
        CHECK(std::string(name).length() > 0);
    }
}

TEST_CASE("Rank thresholds are monotonically increasing") {
    int prevHonor = -1;
    for (int honor : {0, 100, 500, 2000, 5000, 10000, 25000, 50000, 75000, 99999}) {
        CHECK(honor > prevHonor);
        prevHonor = honor;
    }
}

} // TEST_SUITE
```

- [ ] **Step 2: Add HonorRank to honor_system.h**

Add to `game/shared/honor_system.h`:

```cpp
enum class HonorRank : uint8_t {
    Recruit = 0,
    Scout = 1,
    CombatSoldier = 2,
    VeteranSoldier = 3,
    ApprenticeKnight = 4,
    Fighter = 5,
    EliteFighter = 6,
    FieldCommander = 7,
    Commander = 8,
    General = 9
};

// Add these static methods to HonorSystem class:
static HonorRank getHonorRank(int honor) {
    if (honor >= 99999) return HonorRank::General;
    if (honor >= 75000) return HonorRank::Commander;
    if (honor >= 50000) return HonorRank::FieldCommander;
    if (honor >= 25000) return HonorRank::EliteFighter;
    if (honor >= 10000) return HonorRank::Fighter;
    if (honor >= 5000)  return HonorRank::ApprenticeKnight;
    if (honor >= 2000)  return HonorRank::VeteranSoldier;
    if (honor >= 500)   return HonorRank::CombatSoldier;
    if (honor >= 100)   return HonorRank::Scout;
    return HonorRank::Recruit;
}

static const char* getHonorRankName(HonorRank rank) {
    switch (rank) {
        case HonorRank::Recruit:          return "Recruit";
        case HonorRank::Scout:            return "Scout";
        case HonorRank::CombatSoldier:    return "Combat Soldier";
        case HonorRank::VeteranSoldier:   return "Veteran Soldier";
        case HonorRank::ApprenticeKnight: return "Apprentice Knight";
        case HonorRank::Fighter:          return "Fighter";
        case HonorRank::EliteFighter:     return "Elite Fighter";
        case HonorRank::FieldCommander:   return "Field Commander";
        case HonorRank::Commander:        return "Commander";
        case HonorRank::General:          return "General";
        default:                          return "Unknown";
    }
}
```

- [ ] **Step 3: Add honorRank to protocol messages**

In `engine/net/protocol.h`:
- `SvPlayerStateMsg`: Add `uint8_t honorRank = 0;` after `pkStatus`. Add to write/read.
- `SvEntityEnterMsg`: Add `uint8_t honorRank = 0;` after `pkStatus`. In write/read, inside `if (entityType == 0)` block, after `pkStatus`.
- `SvEntityUpdateMsg`: Add `uint8_t honorRank = 0;` field. Add bit 15: `if (fieldMask & (1 << 15)) w.writeU8(honorRank);` and matching read. Update `buildCurrentState()` fieldMask from `0x7FFF` to `0xFFFF` (all 16 bits).

- [ ] **Step 4: Wire into replication.cpp**

In `engine/net/replication.cpp`:
- `buildEnterMessage()`: Add `msg.honorRank = static_cast<uint8_t>(HonorSystem::getHonorRank(charStats->stats.honor));` after pkStatus line.
- `buildCurrentState()`: Same, and update fieldMask to `0xFFFF`.
- `sendDiffs()`: Add `if (current.honorRank != last.honorRank) dirtyMask |= (1 << 15);` after bit 14 check.

- [ ] **Step 5: Wire into sendPlayerState()**

In `server/server_app.cpp`, in `sendPlayerState()`, add after `msg.pkStatus`:
```cpp
msg.honorRank = static_cast<uint8_t>(HonorSystem::getHonorRank(s.honor));
```

Include `game/shared/honor_system.h` if not already included.

- [ ] **Step 6: Build and run tests**

```bash
touch game/shared/honor_system.h engine/net/protocol.h engine/net/replication.cpp server/server_app.cpp
cmake --build build --target fate_tests && ./build/Debug/fate_tests --test-suite="HonorRanking"
```

- [ ] **Step 7: Run full test suite**

Existing protocol serialization tests may need updating for the new field. Fix any failures.

- [ ] **Step 8: Commit**

```bash
git add game/shared/honor_system.h engine/net/protocol.h engine/net/replication.cpp server/server_app.cpp tests/test_honor_ranking.cpp
git commit -m "feat: add honor ranking with badges replicated to all players"
```

---

### Task 3: Infrastructure (DeathSource, PlayerEventLock, Packet Types, Protocol Messages)

**Files:**
- Modify: `game/shared/game_types.h` — Add `DeathSource::Battlefield`, `DeathSource::Arena`
- Modify: `engine/net/packet.h` — Add 4 packet types
- Modify: `engine/net/game_messages.h` — Add 4 message structs
- Modify: `server/server_app.h` — Add `PlayerEventLock` map
- Modify: `tests/test_protocol.cpp` — Serialization tests

- [ ] **Step 1: Add DeathSource entries**

In `game/shared/game_types.h`, add to the `DeathSource` enum:
```cpp
    Battlefield = 4,
    Arena = 5
```

- [ ] **Step 2: Add packet types**

In `engine/net/packet.h`:
```cpp
    constexpr uint8_t CmdBattlefield     = 0x22;
    constexpr uint8_t CmdArena           = 0x23;
    constexpr uint8_t SvBattlefieldUpdate = 0xAC;
    constexpr uint8_t SvArenaUpdate       = 0xAD;
```

- [ ] **Step 3: Add message structs**

In `engine/net/game_messages.h`:

```cpp
struct CmdBattlefieldMsg {
    uint8_t action = 0; // 0=Register, 1=Unregister
    void write(ByteWriter& w) const { w.writeU8(action); }
    static CmdBattlefieldMsg read(ByteReader& r) {
        CmdBattlefieldMsg m; m.action = r.readU8(); return m;
    }
};

struct SvBattlefieldUpdateMsg {
    uint8_t state = 0;           // EventState enum
    uint16_t timeRemaining = 0;
    uint8_t factionCount = 0;
    std::vector<uint8_t> factionIds;
    std::vector<uint16_t> factionKills;
    uint16_t personalKills = 0;
    uint8_t result = 0;          // 0=ongoing, 1=win, 2=loss, 3=tie

    void write(ByteWriter& w) const {
        w.writeU8(state);
        w.writeU16(timeRemaining);
        w.writeU8(factionCount);
        for (uint8_t i = 0; i < factionCount; ++i) {
            w.writeU8(factionIds[i]);
            w.writeU16(factionKills[i]);
        }
        w.writeU16(personalKills);
        w.writeU8(result);
    }
    static SvBattlefieldUpdateMsg read(ByteReader& r) {
        SvBattlefieldUpdateMsg m;
        m.state = r.readU8();
        m.timeRemaining = r.readU16();
        m.factionCount = r.readU8();
        for (uint8_t i = 0; i < m.factionCount; ++i) {
            m.factionIds.push_back(r.readU8());
            m.factionKills.push_back(r.readU16());
        }
        m.personalKills = r.readU16();
        m.result = r.readU8();
        return m;
    }
};

struct CmdArenaMsg {
    uint8_t action = 0; // 0=Register, 1=Unregister
    uint8_t mode = 1;   // 1=Solo, 2=Duo, 3=Team
    void write(ByteWriter& w) const { w.writeU8(action); w.writeU8(mode); }
    static CmdArenaMsg read(ByteReader& r) {
        CmdArenaMsg m; m.action = r.readU8(); m.mode = r.readU8(); return m;
    }
};

struct SvArenaUpdateMsg {
    uint8_t state = 0;           // 0=Queued, 1=Countdown, 2=Active, 3=Ended
    uint16_t timeRemaining = 0;
    uint8_t teamAlive = 0;
    uint8_t enemyAlive = 0;
    uint8_t result = 0;          // 0=ongoing, 1=win, 2=loss, 3=tie
    int32_t honorReward = 0;

    void write(ByteWriter& w) const {
        w.writeU8(state);
        w.writeU16(timeRemaining);
        w.writeU8(teamAlive);
        w.writeU8(enemyAlive);
        w.writeU8(result);
        w.writeI32(honorReward);
    }
    static SvArenaUpdateMsg read(ByteReader& r) {
        SvArenaUpdateMsg m;
        m.state = r.readU8();
        m.timeRemaining = r.readU16();
        m.teamAlive = r.readU8();
        m.enemyAlive = r.readU8();
        m.result = r.readU8();
        m.honorReward = r.readI32();
        return m;
    }
};
```

- [ ] **Step 4: Add PlayerEventLock to ServerApp**

In `server/server_app.h`:
```cpp
std::unordered_map<uint32_t, std::string> playerEventLocks_; // entityId → eventType
```

- [ ] **Step 5: Add serialization tests**

Add round-trip tests for all 4 message types to `tests/test_protocol.cpp`. Include a test for `SvBattlefieldUpdateMsg` with 2 factions (variable-length arrays).

- [ ] **Step 6: Build and run tests**

```bash
touch game/shared/game_types.h engine/net/packet.h engine/net/game_messages.h server/server_app.h tests/test_protocol.cpp
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 7: Commit**

```bash
git add game/shared/game_types.h engine/net/packet.h engine/net/game_messages.h server/server_app.h tests/test_protocol.cpp
git commit -m "feat: add battlefield/arena infrastructure (DeathSource, packets, event locks)"
```

---

### Task 4: Battlefield Manager + Tests

**Files:**
- Create: `game/shared/battlefield_manager.h`
- Create: `tests/test_battlefield.cpp`

- [ ] **Step 1: Write battlefield tests**

```cpp
#include <doctest/doctest.h>
#include "game/shared/battlefield_manager.h"

using namespace fate;

TEST_SUITE("BattlefieldManager") {

TEST_CASE("Can register player") {
    BattlefieldManager bf;
    CHECK(bf.registerPlayer(1, "char1", Faction::Xyros, Vec2{100,100}, "zone_village"));
    CHECK(bf.playerCount() == 1);
}

TEST_CASE("Cannot register same player twice") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "char1", Faction::Xyros, Vec2{}, "zone1");
    CHECK_FALSE(bf.registerPlayer(1, "char1", Faction::Xyros, Vec2{}, "zone1"));
}

TEST_CASE("Unregister removes player") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "char1", Faction::Xyros, Vec2{}, "zone1");
    bf.unregisterPlayer(1);
    CHECK(bf.playerCount() == 0);
}

TEST_CASE("Kill increments faction counter") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "char1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "char2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2); // Xyros kills Fenor
    CHECK(bf.getFactionKills(Faction::Xyros) == 1);
    CHECK(bf.getFactionKills(Faction::Fenor) == 0);
}

TEST_CASE("Personal kills tracked") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    bf.onPlayerKill(1, 2);
    CHECK(bf.getPersonalKills(1) == 2);
}

TEST_CASE("Winning faction determined by most kills") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    bf.onPlayerKill(1, 2);
    bf.onPlayerKill(2, 1);
    auto winner = bf.getWinningFaction();
    CHECK(winner == Faction::Xyros);
}

TEST_CASE("Tie when equal kills") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    bf.onPlayerKill(2, 1);
    auto winner = bf.getWinningFaction();
    CHECK(winner == Faction::None); // tie
}

TEST_CASE("hasMinimumPlayers requires 2+ factions") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    CHECK_FALSE(bf.hasMinimumPlayers());
    bf.registerPlayer(2, "c2", Faction::Xyros, Vec2{}, "z"); // same faction
    CHECK_FALSE(bf.hasMinimumPlayers());
    bf.registerPlayer(3, "c3", Faction::Fenor, Vec2{}, "z"); // different faction
    CHECK(bf.hasMinimumPlayers());
}

TEST_CASE("removePlayer keeps existing kills") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    bf.removePlayer(1); // disconnect
    CHECK(bf.getFactionKills(Faction::Xyros) == 1); // kills preserved
}

} // TEST_SUITE
```

- [ ] **Step 2: Implement battlefield_manager.h**

```cpp
#pragma once
#include "game/shared/faction.h"
#include "engine/math/vec2.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fate {

struct BattlefieldPlayer {
    uint32_t entityId = 0;
    std::string characterId;
    Faction faction = Faction::None;
    Vec2 returnPosition;
    std::string returnScene;
    int personalKills = 0;
    int personalDeaths = 0;
};

class BattlefieldManager {
public:
    bool registerPlayer(uint32_t entityId, const std::string& charId, Faction faction,
                        Vec2 returnPos, const std::string& returnScene) {
        if (players_.count(entityId)) return false;
        players_[entityId] = {entityId, charId, faction, returnPos, returnScene, 0, 0};
        factionKills_[faction]; // ensure entry exists
        return true;
    }

    void unregisterPlayer(uint32_t entityId) { players_.erase(entityId); }
    void removePlayer(uint32_t entityId) { players_.erase(entityId); } // disconnect

    void onPlayerKill(uint32_t killerId, uint32_t victimId) {
        auto kit = players_.find(killerId);
        if (kit != players_.end()) {
            kit->second.personalKills++;
            factionKills_[kit->second.faction]++;
        }
        auto vit = players_.find(victimId);
        if (vit != players_.end()) vit->second.personalDeaths++;
    }

    int getFactionKills(Faction f) const {
        auto it = factionKills_.find(f);
        return it != factionKills_.end() ? it->second : 0;
    }

    int getPersonalKills(uint32_t entityId) const {
        auto it = players_.find(entityId);
        return it != players_.end() ? it->second.personalKills : 0;
    }

    Faction getWinningFaction() const {
        Faction winner = Faction::None;
        int maxKills = 0;
        bool tied = false;
        for (const auto& [f, kills] : factionKills_) {
            if (kills > maxKills) { maxKills = kills; winner = f; tied = false; }
            else if (kills == maxKills && kills > 0) tied = true;
        }
        return tied ? Faction::None : winner;
    }

    bool hasMinimumPlayers() const {
        std::unordered_set<uint8_t> factions;
        for (const auto& [id, p] : players_) factions.insert(static_cast<uint8_t>(p.faction));
        return factions.size() >= 2;
    }

    size_t playerCount() const { return players_.size(); }
    const auto& players() const { return players_; }

    void reset() { players_.clear(); factionKills_.clear(); }

private:
    std::unordered_map<uint32_t, BattlefieldPlayer> players_;
    std::unordered_map<Faction, int> factionKills_;
};

} // namespace fate
```

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests --test-suite="BattlefieldManager"
```

- [ ] **Step 4: Commit**

```bash
git add game/shared/battlefield_manager.h tests/test_battlefield.cpp
git commit -m "feat: add BattlefieldManager with faction kill tracking"
```

---

### Task 5: Battlefield Server + Client Integration

**Files:**
- Modify: `server/server_app.h` — Add `BattlefieldManager`, declare handler
- Modify: `server/server_app.cpp` — Register with scheduler, handle CmdBattlefield, hook kills, disconnect cleanup
- Modify: `engine/net/net_client.h/cpp` — Add callbacks

- [ ] **Step 1: Add BattlefieldManager to ServerApp**

In `server/server_app.h`, add member and handler:
```cpp
#include "game/shared/battlefield_manager.h"
BattlefieldManager battlefieldManager_;
void processBattlefield(uint16_t clientId, const CmdBattlefieldMsg& msg);
```

- [ ] **Step 2: Register battlefield with event scheduler**

In server startup (near `initGauntlet()`), register the battlefield event:
```cpp
scheduler_.registerEvent({"battlefield", 7200.0f, 300.0f, 900.0f});
scheduler_.setCallback("battlefield", EventCallback::OnSignupStart, [this]{
    // Broadcast system chat
});
scheduler_.setCallback("battlefield", EventCallback::OnEventStart, [this]{
    if (!battlefieldManager_.hasMinimumPlayers()) {
        // Cancel, notify players
        battlefieldManager_.reset();
        return;
    }
    // Teleport all registered players to battlefield scene
});
scheduler_.setCallback("battlefield", EventCallback::OnEventEnd, [this]{
    // Determine winner, distribute rewards, teleport back
    auto winner = battlefieldManager_.getWinningFaction();
    // ... reward distribution ...
    battlefieldManager_.reset();
});
```

Add `scheduler_.tick(gameTime_);` to the main tick loop.

- [ ] **Step 3: Handle CmdBattlefield**

Add dispatch in packet switch + `processBattlefield()`:
- Register: validate not in event (check `playerEventLocks_`), validate signup phase, store return position, add to `playerEventLocks_`
- Unregister: remove from battlefield and event lock

- [ ] **Step 4: Hook PvP kills in battlefield scene**

In the PvP kill handling code (where `SvDeathNotifyMsg` is sent for PvP deaths), check if both players are in the battlefield scene. If so, use `DeathSource::Battlefield` (no XP/honor loss) and call `battlefieldManager_.onPlayerKill(killerId, victimId)`.

- [ ] **Step 5: Add disconnect cleanup**

In `onClientDisconnected()`:
```cpp
battlefieldManager_.removePlayer(entityId);
playerEventLocks_.erase(entityId);
```

- [ ] **Step 6: Add client callbacks**

In `engine/net/net_client.h`:
```cpp
std::function<void(const SvBattlefieldUpdateMsg&)> onBattlefieldUpdate;
```
In `engine/net/net_client.cpp`, add the case for `PacketType::SvBattlefieldUpdate`.

- [ ] **Step 7: Build and run tests**

```bash
touch server/server_app.cpp server/server_app.h engine/net/net_client.cpp engine/net/net_client.h
cmake --build build --target FateServer && cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 8: Commit**

```bash
git add server/server_app.cpp server/server_app.h engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: integrate battlefield with event scheduler, kills, rewards, and teleport"
```

---

### Task 6: Arena Manager + Tests

**Files:**
- Create: `game/shared/arena_manager.h`
- Create: `tests/test_arena.cpp`

- [ ] **Step 1: Write arena tests**

Cover: registration validation (party size for mode), cross-faction matching, level range check, AFK detection on living players only, dead exempt from AFK, 0 damage = 0 rewards, auto-unregister on party change, queue timeout.

Key test cases:
```cpp
TEST_SUITE("ArenaManager") {
    // Solo registration (no party needed)
    // Duo registration requires party of 2
    // Team registration requires party of 3
    // Wrong party size rejected
    // Cross-faction matching succeeds
    // Same-faction matching rejected
    // Level range > 5 rejected
    // AFK check: living player 30s no action → forfeit
    // AFK check: dead player NOT flagged
    // 0 damage dealt = 0 rewards
    // Match ends when all opponents dead
    // Match timer expiry = tie if both alive
    // Queue timeout after 5 minutes
}
```

- [ ] **Step 2: Implement arena_manager.h**

Core structures:
- `ArenaGroup` — registered group (playerIds, faction, mode, queuedAt)
- `ArenaMatch` — active match (two groups, timer, per-player stats: damageDealt, lastActionTime, isAlive)
- `ArenaManager` — queue management, `tryMatchmaking()` (called every 20 ticks), match lifecycle

Key methods:
- `registerGroup(playerIds, faction, mode, levels)` → bool
- `unregisterGroup(groupId)` → void
- `tryMatchmaking()` → creates matches when valid pairs found
- `tickMatches(float currentTime)` → check timer, AFK on living players
- `onPlayerKill(matchId, killerId, victimId)` → mark dead, check win
- `onPlayerAction(matchId, playerId)` → update lastActionTime
- `isPlayerInArena(entityId)` → bool

AFK detection: for each living player in active match, if `currentTime - lastActionTime > 30.0f` → auto-forfeit (set isAlive=false, 0 rewards).

Damage gate: on match end, any player with `damageDealt == 0` gets `honorReward = 0`.

- [ ] **Step 3: Build and run tests**

```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests --test-suite="ArenaManager"
```

- [ ] **Step 4: Commit**

```bash
git add game/shared/arena_manager.h tests/test_arena.cpp
git commit -m "feat: add ArenaManager with queue, matchmaking, AFK detection"
```

---

### Task 7: Arena Server + Client Integration

**Files:**
- Modify: `server/server_app.h` — Add `ArenaManager`, declare handler
- Modify: `server/server_app.cpp` — Handle CmdArena, tick matches, hook kills/actions, party change listener, disconnect
- Modify: `engine/net/net_client.h/cpp` — Add callbacks

- [ ] **Step 1: Add ArenaManager to ServerApp**

```cpp
#include "game/shared/arena_manager.h"
ArenaManager arenaManager_;
void processArena(uint16_t clientId, const CmdArenaMsg& msg);
```

- [ ] **Step 2: Handle CmdArena**

Register: validate party size for mode, validate not in event lock, validate not dead, add to queue + event lock.
Unregister: remove from queue, clear event lock.

- [ ] **Step 3: Tick ArenaManager**

In main loop, every 20 ticks:
```cpp
if (tickCounter_ % 20 == 0) {
    arenaManager_.tryMatchmaking();
}
arenaManager_.tickMatches(gameTime_);
```

When matchmaking creates a match → teleport players to "arena" scene, send `SvArenaUpdate` with `state=Countdown`.

- [ ] **Step 4: Hook PvP kills in arena**

In PvP kill path: if both players are in an arena match, use `DeathSource::Arena`, call `arenaManager_.onPlayerKill()`. Dead players stay dead (no respawn). Check if match is over (all opponents dead → instant win).

- [ ] **Step 5: Hook player actions for AFK tracking**

In `processAction()` and `processUseSkill()`: if player is in arena, call `arenaManager_.onPlayerAction(matchId, entityId)`.

- [ ] **Step 6: Listen for party changes**

When processing party leave/kick/disband: check if any party member was in an arena queue → auto-unregister the group.

- [ ] **Step 7: Disconnect cleanup**

In `onClientDisconnected()`:
```cpp
arenaManager_.onPlayerDisconnect(entityId); // treat as dead in match
playerEventLocks_.erase(entityId);
```

- [ ] **Step 8: Add client callback**

```cpp
std::function<void(const SvArenaUpdateMsg&)> onArenaUpdate;
```

- [ ] **Step 9: Build and run tests**

```bash
touch server/server_app.cpp server/server_app.h engine/net/net_client.cpp engine/net/net_client.h
cmake --build build --target FateServer && cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 10: Commit**

```bash
git add server/server_app.cpp server/server_app.h engine/net/net_client.h engine/net/net_client.cpp
git commit -m "feat: integrate arena with matchmaking, AFK detection, and party sync"
```

---

### Task 8: Final Integration Test

- [ ] **Step 1: Run full test suite**

```bash
cmake --build build --target fate_tests && ./build/Debug/fate_tests
```

- [ ] **Step 2: Build both targets**

```bash
cmake --build build --target FateServer && cmake --build build --target FateEngine
```

- [ ] **Step 3: Verify test count**

```bash
./build/Debug/fate_tests -c
```

Record final count.

**REMINDER:** Restart FateServer.exe after deploying server changes.
