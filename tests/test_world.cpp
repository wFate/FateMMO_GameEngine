#include <doctest/doctest.h>
#include "engine/ecs/world.h"

// Test components using the new FATE_COMPONENT macro (archetype-compatible, no virtual dispatch)
namespace fate {

struct TestTransform {
    FATE_COMPONENT(TestTransform)
    float x = 0.0f;
    float y = 0.0f;

    TestTransform() = default;
    TestTransform(float x, float y) : x(x), y(y) {}
};

struct TestHealth {
    FATE_COMPONENT(TestHealth)
    float hp = 100.0f;
    float maxHp = 100.0f;

    TestHealth() = default;
    TestHealth(float startHp) : hp(startHp), maxHp(startHp) {}
};

struct TestVelocity {
    FATE_COMPONENT(TestVelocity)
    float vx = 0.0f;
    float vy = 0.0f;
};

} // namespace fate

using fate::TestTransform;
using fate::TestHealth;
using fate::TestVelocity;

TEST_CASE("World: create and retrieve entity") {
    fate::World world;
    auto* e = world.createEntity("TestEntity");
    REQUIRE(e != nullptr);
    CHECK(e->name() == "TestEntity");
    CHECK(e->isActive());
    CHECK(world.entityCount() == 1);

    // Retrieve by handle
    auto* same = world.getEntity(e->handle());
    CHECK(same == e);

    // Retrieve by ID
    auto* byId = world.getEntity(e->id());
    CHECK(byId == e);
}

TEST_CASE("World: entity component add/get via archetype") {
    fate::World world;
    auto* e = world.createEntity("Player");

    // Add a component
    auto* t = e->addComponent<TestTransform>(10.0f, 20.0f);
    REQUIRE(t != nullptr);
    CHECK(t->x == 10.0f);
    CHECK(t->y == 20.0f);

    // Get the component back
    auto* got = e->getComponent<TestTransform>();
    REQUIRE(got != nullptr);
    CHECK(got == t);
    CHECK(got->x == 10.0f);

    // Has component
    CHECK(e->hasComponent<TestTransform>());
    CHECK(!e->hasComponent<TestHealth>());
    CHECK(e->componentCount() == 1);
}

TEST_CASE("World: multiple components and archetype migration") {
    fate::World world;
    auto* e = world.createEntity("Player");

    // Add first component
    auto* t = e->addComponent<TestTransform>(5.0f, 15.0f);
    CHECK(t->x == 5.0f);

    // Add second component -- triggers migration to new archetype
    auto* h = e->addComponent<TestHealth>(200.0f);
    REQUIRE(h != nullptr);
    CHECK(h->hp == 200.0f);

    // Original component should still be accessible (data preserved during migration)
    auto* t2 = e->getComponent<TestTransform>();
    REQUIRE(t2 != nullptr);
    CHECK(t2->x == 5.0f);
    CHECK(t2->y == 15.0f);

    CHECK(e->componentCount() == 2);
}

TEST_CASE("World: remove component") {
    fate::World world;
    auto* e = world.createEntity("Player");
    e->addComponent<TestTransform>(1.0f, 2.0f);
    e->addComponent<TestHealth>(50.0f);
    CHECK(e->componentCount() == 2);

    e->removeComponent<TestHealth>();
    CHECK(e->componentCount() == 1);
    CHECK(e->hasComponent<TestTransform>());
    CHECK(!e->hasComponent<TestHealth>());

    // Transform data should be preserved
    auto* t = e->getComponent<TestTransform>();
    REQUIRE(t != nullptr);
    CHECK(t->x == 1.0f);
    CHECK(t->y == 2.0f);
}

TEST_CASE("World: destroy entity") {
    fate::World world;
    auto* e = world.createEntity("Victim");
    fate::EntityHandle h = e->handle();
    e->addComponent<TestTransform>(1.0f, 1.0f);

    CHECK(world.entityCount() == 1);
    CHECK(world.isAlive(h));

    world.destroyEntity(h);
    // Entity is queued but not yet destroyed
    CHECK(world.entityCount() == 1);

    world.processDestroyQueue();
    CHECK(world.entityCount() == 0);
    CHECK(!world.isAlive(h));
    CHECK(world.getEntity(h) == nullptr);
}

TEST_CASE("World: forEach single component") {
    fate::World world;

    auto* e1 = world.createEntity("E1");
    auto* t1 = e1->addComponent<TestTransform>(1.0f, 0.0f);

    auto* e2 = world.createEntity("E2");
    auto* t2 = e2->addComponent<TestTransform>(2.0f, 0.0f);

    auto* e3 = world.createEntity("E3");
    // e3 has no TestTransform, should not be visited

    int visited = 0;
    float sumX = 0.0f;
    world.forEach<TestTransform>([&](fate::Entity* entity, TestTransform* t) {
        visited++;
        sumX += t->x;
    });

    CHECK(visited == 2);
    CHECK(sumX == 3.0f);
}

TEST_CASE("World: forEach two components") {
    fate::World world;

    // Entity with both components
    auto* e1 = world.createEntity("Both");
    e1->addComponent<TestTransform>(10.0f, 0.0f);
    e1->addComponent<TestHealth>(100.0f);

    // Entity with only Transform
    auto* e2 = world.createEntity("TransformOnly");
    e2->addComponent<TestTransform>(20.0f, 0.0f);

    // Entity with only Health
    auto* e3 = world.createEntity("HealthOnly");
    e3->addComponent<TestHealth>(50.0f);

    int visited = 0;
    world.forEach<TestTransform, TestHealth>([&](fate::Entity* entity, TestTransform* t, TestHealth* h) {
        visited++;
        CHECK(entity->name() == "Both");
        CHECK(t->x == 10.0f);
        CHECK(h->hp == 100.0f);
    });

    CHECK(visited == 1);
}

TEST_CASE("World: pointer stability across migrations") {
    fate::World world;

    auto* e1 = world.createEntity("E1");
    auto* e2 = world.createEntity("E2");
    auto* e3 = world.createEntity("E3");

    // Store pointers
    fate::Entity* ptr1 = e1;
    fate::Entity* ptr2 = e2;
    fate::Entity* ptr3 = e3;

    // Add components to e1 (causes migration, but pointer should remain stable)
    e1->addComponent<TestTransform>(1.0f, 1.0f);
    e1->addComponent<TestHealth>(100.0f);
    e1->addComponent<TestVelocity>();

    // All pointers should still be valid
    CHECK(ptr1 == world.getEntity(e1->handle()));
    CHECK(ptr2 == world.getEntity(e2->handle()));
    CHECK(ptr3 == world.getEntity(e3->handle()));

    CHECK(ptr1->name() == "E1");
    CHECK(ptr2->name() == "E2");
    CHECK(ptr3->name() == "E3");
}

TEST_CASE("World: slot reuse after destroy") {
    fate::World world;

    auto h1 = world.createEntityH("First");
    world.destroyEntity(h1);
    world.processDestroyQueue();

    auto h2 = world.createEntityH("Second");

    // Slot should be reused but generation bumped
    CHECK(h1.index() == h2.index());
    CHECK(h1.generation() != h2.generation());

    // Old handle should not resolve
    CHECK(world.getEntity(h1) == nullptr);
    // New handle should resolve
    auto* e = world.getEntity(h2);
    REQUIRE(e != nullptr);
    CHECK(e->name() == "Second");
}

TEST_CASE("World: multiple entities with same archetype") {
    fate::World world;

    constexpr int COUNT = 100;
    for (int i = 0; i < COUNT; ++i) {
        auto* e = world.createEntity("Entity" + std::to_string(i));
        e->addComponent<TestTransform>(static_cast<float>(i), static_cast<float>(i * 2));
        e->addComponent<TestHealth>(static_cast<float>(i + 10));
    }

    CHECK(world.entityCount() == COUNT);

    int visited = 0;
    world.forEach<TestTransform, TestHealth>([&](fate::Entity* entity, TestTransform* t, TestHealth* h) {
        visited++;
    });
    CHECK(visited == COUNT);
}

TEST_CASE("World: destroy with swap-and-pop correctness") {
    fate::World world;

    // Create 3 entities with same archetype
    auto* e1 = world.createEntity("E1");
    e1->addComponent<TestTransform>(1.0f, 1.0f);

    auto* e2 = world.createEntity("E2");
    e2->addComponent<TestTransform>(2.0f, 2.0f);

    auto* e3 = world.createEntity("E3");
    e3->addComponent<TestTransform>(3.0f, 3.0f);

    fate::EntityHandle h1 = e1->handle();
    fate::EntityHandle h2 = e2->handle();

    // Destroy middle entity
    world.destroyEntity(h1);
    world.processDestroyQueue();

    CHECK(world.entityCount() == 2);

    // Remaining entities should still have correct data
    auto* re2 = world.getEntity(h2);
    REQUIRE(re2 != nullptr);
    auto* t2 = re2->getComponent<TestTransform>();
    REQUIRE(t2 != nullptr);
    CHECK(t2->x == 2.0f);
    CHECK(t2->y == 2.0f);

    auto* re3 = world.getEntity(e3->handle());
    REQUIRE(re3 != nullptr);
    auto* t3 = re3->getComponent<TestTransform>();
    REQUIRE(t3 != nullptr);
    CHECK(t3->x == 3.0f);
    CHECK(t3->y == 3.0f);
}

TEST_CASE("World: findByName and findByTag") {
    fate::World world;

    auto* e = world.createEntity("Hero");
    e->setTag("player");
    e->addComponent<TestTransform>();

    CHECK(world.findByName("Hero") == e);
    CHECK(world.findByName("NonExistent") == nullptr);

    CHECK(world.findByTag("player") == e);
    CHECK(world.findByTag("enemy") == nullptr);
}

TEST_CASE("World: forEachEntity iterates all") {
    fate::World world;
    world.createEntity("A");
    world.createEntity("B");
    world.createEntity("C");

    int count = 0;
    world.forEachEntity([&](fate::Entity* e) {
        count++;
    });
    CHECK(count == 3);
}

TEST_CASE("World: inactive entities skipped by forEach") {
    fate::World world;

    auto* e1 = world.createEntity("Active");
    e1->addComponent<TestTransform>();

    auto* e2 = world.createEntity("Inactive");
    e2->addComponent<TestTransform>();
    e2->setActive(false);

    int visited = 0;
    world.forEach<TestTransform>([&](fate::Entity* entity, TestTransform* t) {
        visited++;
    });
    CHECK(visited == 1);
}

TEST_CASE("World: disabled components skipped by forEach") {
    fate::World world;

    auto* e = world.createEntity("E");
    auto* t = e->addComponent<TestTransform>();
    t->enabled = false;

    int visited = 0;
    world.forEach<TestTransform>([&](fate::Entity* entity, TestTransform* tc) {
        visited++;
    });
    CHECK(visited == 0);
}
