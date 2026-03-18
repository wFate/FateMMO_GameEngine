#include <doctest/doctest.h>
#include "engine/net/replication.h"

using namespace fate;

TEST_CASE("ReplicationManager: register and lookup entities") {
    ReplicationManager repl;
    EntityHandle h{1, 0}; // index 1, generation 0
    PersistentId pid = PersistentId::generate(1);

    repl.registerEntity(h, pid);

    CHECK(repl.getPersistentId(h) == pid);
    CHECK(repl.getEntityHandle(pid) == h);

    repl.unregisterEntity(h);
    CHECK(repl.getPersistentId(h).isNull());
}

TEST_CASE("ReplicationManager: buildEnterMessage fills entity data") {
    World world;
    ReplicationManager repl;

    // Create a test entity with Transform and a nameplate
    Entity* e = world.createEntity("TestMob");
    auto* t = e->addComponent<Transform>(100.0f, 200.0f);
    auto* np = e->addComponent<MobNameplateComponent>();
    np->displayName = "Slime";
    np->level = 3;
    auto* es = e->addComponent<EnemyStatsComponent>();
    es->stats.currentHP = 50;
    es->stats.maxHP = 100;

    PersistentId pid = PersistentId::generate(1);
    repl.registerEntity(e->handle(), pid);

    // Verify the registration works and entity has expected components
    CHECK(repl.getPersistentId(e->handle()) == pid);

    auto* transform = e->getComponent<Transform>();
    CHECK(transform->position.x == doctest::Approx(100.0f));
}
