#include <doctest/doctest.h>
#include "server/dungeon_manager.h"

using namespace fate;

TEST_CASE("DungeonManager: create instance returns valid ID") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 42, 1);
    CHECK(id != 0);
    CHECK(mgr.getInstance(id) != nullptr);
}

TEST_CASE("DungeonManager: separate instances have independent worlds") {
    DungeonManager mgr;
    uint32_t id1 = mgr.createInstance("TestDungeon", 1, 1);
    uint32_t id2 = mgr.createInstance("TestDungeon", 2, 1);
    CHECK(id1 != id2);

    auto* inst1 = mgr.getInstance(id1);
    auto* inst2 = mgr.getInstance(id2);
    REQUIRE(inst1 != nullptr);
    REQUIRE(inst2 != nullptr);

    // Worlds are separate objects
    CHECK(&inst1->world != &inst2->world);
}

TEST_CASE("DungeonManager: destroy instance cleans up") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1, 1);
    CHECK(mgr.getInstance(id) != nullptr);
    mgr.destroyInstance(id);
    CHECK(mgr.getInstance(id) == nullptr);
}

TEST_CASE("DungeonManager: getInstanceForParty finds existing instance") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 42, 1);
    CHECK(mgr.getInstanceForParty(42) == id);
    CHECK(mgr.getInstanceForParty(99) == 0);
}

TEST_CASE("DungeonManager: tick updates all instances") {
    DungeonManager mgr;
    mgr.createInstance("TestDungeon", 1, 1);
    mgr.createInstance("TestDungeon", 2, 1);
    mgr.tick(0.05f);
    CHECK(mgr.instanceCount() == 2);
}

TEST_CASE("DungeonManager: player tracking") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1, 1);
    mgr.addPlayer(id, 100);
    mgr.addPlayer(id, 200);

    CHECK(mgr.getInstanceForClient(100) == id);
    CHECK(mgr.getInstanceForClient(200) == id);
    CHECK(mgr.getInstanceForClient(300) == 0);

    mgr.removePlayer(id, 100);
    CHECK(mgr.getInstanceForClient(100) == 0);
    CHECK(mgr.getInstanceForClient(200) == id);
}

TEST_CASE("DungeonManager: empty active instances detected") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->elapsedTime = 5.0f;

    // No players, elapsed > 0 -- should be empty active
    CHECK(mgr.getEmptyActiveInstances().size() == 1);

    // Add a player -- no longer empty
    mgr.addPlayer(id, 100);
    CHECK(mgr.getEmptyActiveInstances().empty());

    // Remove player -- empty again
    mgr.removePlayer(id, 100);
    CHECK(mgr.getEmptyActiveInstances().size() == 1);
}

TEST_CASE("DungeonManager: destroy cleans up party mapping") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 42, 1);
    CHECK(mgr.getInstanceForParty(42) == id);
    mgr.destroyInstance(id);
    CHECK(mgr.getInstanceForParty(42) == 0);
}

TEST_CASE("DungeonManager: timed out instance detected") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("Test", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->timeLimitSeconds = 10.0f;
    inst->elapsedTime = 11.0f;
    auto timedOut = mgr.getTimedOutInstances();
    CHECK(timedOut.size() == 1);
}

TEST_CASE("DungeonManager: completed instance not in timed out list") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("Test", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->timeLimitSeconds = 10.0f;
    inst->elapsedTime = 11.0f;
    inst->completed = true;
    CHECK(mgr.getTimedOutInstances().empty());
}

TEST_CASE("DungeonManager: celebration timer counts down") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("Test", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->completed = true;
    inst->celebrationTimer = 15.0f;
    mgr.tick(5.0f);
    CHECK(inst->celebrationTimer == doctest::Approx(10.0f));
}

TEST_CASE("DungeonManager: celebration finished detected") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("Test", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->completed = true;
    inst->celebrationTimer = 0.5f;
    mgr.tick(1.0f);
    CHECK(mgr.getCelebrationFinishedInstances().size() == 1);
}

TEST_CASE("DungeonManager: empty active instance detected") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("Test", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->elapsedTime = 5.0f;
    CHECK(mgr.getEmptyActiveInstances().size() == 1);
}

TEST_CASE("DungeonManager: return point tracking") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("Test", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->returnPoints[100] = {"WhisperingWoods", 50.0f, 75.0f};
    CHECK(inst->returnPoints[100].scene == "WhisperingWoods");
    CHECK(inst->returnPoints[100].x == doctest::Approx(50.0f));
}

TEST_CASE("DungeonManager: invite accept tracking") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("Test", 1, 1);
    auto* inst = mgr.getInstance(id);
    inst->pendingAccepts.insert(100);
    inst->pendingAccepts.insert(200);
    CHECK_FALSE(inst->allAccepted());
    inst->pendingAccepts.erase(100);
    inst->pendingAccepts.erase(200);
    CHECK(inst->allAccepted());
}
