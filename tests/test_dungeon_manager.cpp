#include <doctest/doctest.h>
#include "server/dungeon_manager.h"

using namespace fate;

TEST_CASE("DungeonManager: create instance returns valid ID") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 42);
    CHECK(id != 0);
    CHECK(mgr.getInstance(id) != nullptr);
}

TEST_CASE("DungeonManager: separate instances have independent worlds") {
    DungeonManager mgr;
    uint32_t id1 = mgr.createInstance("TestDungeon", 1);
    uint32_t id2 = mgr.createInstance("TestDungeon", 2);
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
    uint32_t id = mgr.createInstance("TestDungeon", 1);
    CHECK(mgr.getInstance(id) != nullptr);
    mgr.destroyInstance(id);
    CHECK(mgr.getInstance(id) == nullptr);
}

TEST_CASE("DungeonManager: getInstanceForParty finds existing instance") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 42);
    CHECK(mgr.getInstanceForParty(42) == id);
    CHECK(mgr.getInstanceForParty(99) == 0);
}

TEST_CASE("DungeonManager: tick updates all instances") {
    DungeonManager mgr;
    mgr.createInstance("TestDungeon", 1);
    mgr.createInstance("TestDungeon", 2);
    mgr.tick(0.05f);
    CHECK(mgr.instanceCount() == 2);
}

TEST_CASE("DungeonManager: player tracking") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1);
    mgr.addPlayer(id, 100);
    mgr.addPlayer(id, 200);

    CHECK(mgr.getInstanceForClient(100) == id);
    CHECK(mgr.getInstanceForClient(200) == id);
    CHECK(mgr.getInstanceForClient(300) == 0);

    mgr.removePlayer(id, 100);
    CHECK(mgr.getInstanceForClient(100) == 0);
    CHECK(mgr.getInstanceForClient(200) == id);
}

TEST_CASE("DungeonManager: expired instances only when empty") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 1);
    auto* inst = mgr.getInstance(id);
    inst->timeoutSeconds = 1.0f;
    inst->elapsedTime = 2.0f;

    // Has players -- should NOT expire
    mgr.addPlayer(id, 100);
    CHECK(mgr.getExpiredInstances().empty());

    // Remove player -- NOW it should expire
    mgr.removePlayer(id, 100);
    auto expired = mgr.getExpiredInstances();
    CHECK(expired.size() == 1);
    CHECK(expired[0] == id);
}

TEST_CASE("DungeonManager: destroy cleans up party mapping") {
    DungeonManager mgr;
    uint32_t id = mgr.createInstance("TestDungeon", 42);
    CHECK(mgr.getInstanceForParty(42) == id);
    mgr.destroyInstance(id);
    CHECK(mgr.getInstanceForParty(42) == 0);
}
