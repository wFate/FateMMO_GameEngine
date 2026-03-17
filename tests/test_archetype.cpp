#include <doctest/doctest.h>
#include "engine/ecs/archetype.h"
#include "engine/ecs/command_buffer.h"

namespace {

struct Position {
    FATE_COMPONENT(Position)
    float x = 0.0f;
    float y = 0.0f;
};

struct Health {
    FATE_COMPONENT(Health)
    float hp = 100.0f;
    float maxHp = 100.0f;
};

} // anonymous namespace

TEST_CASE("Archetype creation and entity add") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::ArchetypeStorage storage(arena);
    storage.registerType<Position>();
    storage.registerType<Health>();

    std::vector<fate::CompId> sig = {
        fate::componentId<Position>(),
        fate::componentId<Health>()
    };
    fate::ArchetypeId aid = storage.findOrCreateArchetype(sig);

    fate::EntityHandle e1(1, 1);
    fate::RowIndex row = storage.addEntity(aid, e1);
    CHECK(row == 0);
    CHECK(storage.entityCount(aid) == 1);

    // Verify columns exist and are accessible
    Position* positions = storage.getColumn<Position>(aid);
    Health* healths = storage.getColumn<Health>(aid);
    REQUIRE(positions != nullptr);
    REQUIRE(healths != nullptr);

    // Verify zero-initialized
    CHECK(positions[0].x == 0.0f);
    CHECK(positions[0].y == 0.0f);
    CHECK(healths[0].hp == 0.0f);  // zero-init, not default-init

    // Write and verify
    positions[0].x = 10.0f;
    positions[0].y = 20.0f;
    CHECK(positions[0].x == 10.0f);
    CHECK(positions[0].y == 20.0f);

    // Verify handle stored
    fate::EntityHandle* handles = storage.getHandles(aid);
    CHECK(handles[0] == e1);

    storage.destroyAll();
}

TEST_CASE("Archetype swap-and-pop on entity remove") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::ArchetypeStorage storage(arena);
    storage.registerType<Position>();

    std::vector<fate::CompId> sig = { fate::componentId<Position>() };
    fate::ArchetypeId aid = storage.findOrCreateArchetype(sig);

    fate::EntityHandle e1(1, 1);
    fate::EntityHandle e2(2, 1);
    fate::EntityHandle e3(3, 1);

    storage.addEntity(aid, e1);
    storage.addEntity(aid, e2);
    storage.addEntity(aid, e3);
    CHECK(storage.entityCount(aid) == 3);

    // Set positions so we can verify swap
    Position* pos = storage.getColumn<Position>(aid);
    pos[0].x = 1.0f; pos[0].y = 1.0f;
    pos[1].x = 2.0f; pos[1].y = 2.0f;
    pos[2].x = 3.0f; pos[2].y = 3.0f;

    // Remove middle entity (row 1) — last entity (row 2) should swap in
    fate::EntityHandle swapped = storage.removeEntity(aid, 1);
    CHECK(storage.entityCount(aid) == 2);
    CHECK(swapped == e3);

    // Row 1 now has the data from old row 2
    pos = storage.getColumn<Position>(aid);
    CHECK(pos[1].x == 3.0f);
    CHECK(pos[1].y == 3.0f);

    // Handle at row 1 is now e3
    fate::EntityHandle* handles = storage.getHandles(aid);
    CHECK(handles[0] == e1);
    CHECK(handles[1] == e3);

    storage.destroyAll();
}

TEST_CASE("Archetype migration on addComponent") {
    fate::Arena arena(4 * 1024 * 1024);
    fate::ArchetypeStorage storage(arena);
    storage.registerType<Position>();
    storage.registerType<Health>();

    // Create archetype with Position only
    std::vector<fate::CompId> sigA = { fate::componentId<Position>() };
    fate::ArchetypeId aidA = storage.findOrCreateArchetype(sigA);

    fate::EntityHandle e1(1, 1);
    fate::RowIndex rowA = storage.addEntity(aidA, e1);

    // Set position data
    Position* posA = storage.getColumn<Position>(aidA);
    posA[rowA].x = 42.0f;
    posA[rowA].y = 84.0f;

    // Migrate: add Health component
    fate::ArchetypeId aidB = storage.migrateEntity(
        aidA, rowA, e1, fate::componentId<Health>(), true);

    // Old archetype should be empty
    CHECK(storage.entityCount(aidA) == 0);

    // New archetype should have the entity
    CHECK(storage.entityCount(aidB) == 1);

    // Position data should be preserved
    Position* posB = storage.getColumn<Position>(aidB);
    REQUIRE(posB != nullptr);
    CHECK(posB[0].x == 42.0f);
    CHECK(posB[0].y == 84.0f);

    // Health column should exist (zero-initialized)
    Health* healthB = storage.getColumn<Health>(aidB);
    REQUIRE(healthB != nullptr);

    // Handle should be correct
    fate::EntityHandle* handles = storage.getHandles(aidB);
    CHECK(handles[0] == e1);

    storage.destroyAll();
}

TEST_CASE("Archetype grow") {
    fate::Arena arena(16 * 1024 * 1024);
    fate::ArchetypeStorage storage(arena);
    storage.registerType<Position>();

    std::vector<fate::CompId> sig = { fate::componentId<Position>() };
    fate::ArchetypeId aid = storage.findOrCreateArchetype(sig);

    // Add more entities than initial capacity (64)
    constexpr uint32_t ENTITY_COUNT = 200;
    for (uint32_t i = 1; i <= ENTITY_COUNT; ++i) {
        fate::EntityHandle eh(i, 1);
        storage.addEntity(aid, eh);
    }

    CHECK(storage.entityCount(aid) == ENTITY_COUNT);

    // Verify all positions are accessible
    Position* pos = storage.getColumn<Position>(aid);
    REQUIRE(pos != nullptr);

    // Write to all and verify
    for (uint32_t i = 0; i < ENTITY_COUNT; ++i) {
        pos[i].x = static_cast<float>(i);
        pos[i].y = static_cast<float>(i * 2);
    }
    for (uint32_t i = 0; i < ENTITY_COUNT; ++i) {
        CHECK(pos[i].x == static_cast<float>(i));
        CHECK(pos[i].y == static_cast<float>(i * 2));
    }

    // Verify handles
    fate::EntityHandle* handles = storage.getHandles(aid);
    for (uint32_t i = 0; i < ENTITY_COUNT; ++i) {
        CHECK(handles[i].index() == i + 1);
    }

    storage.destroyAll();
}

TEST_CASE("CommandBuffer deferred execution") {
    int counter = 0;
    fate::CommandBuffer buf;
    CHECK(buf.empty());
    CHECK(buf.size() == 0);

    buf.push([&]() { counter += 1; });
    buf.push([&]() { counter += 10; });
    buf.push([&]() { counter += 100; });
    CHECK(buf.size() == 3);
    CHECK(!buf.empty());

    // Commands not yet executed
    CHECK(counter == 0);

    buf.execute();
    CHECK(counter == 111);
    CHECK(buf.empty());
}
