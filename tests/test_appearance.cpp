#include <doctest/doctest.h>
#include "game/components/game_components.h"
#include "engine/ecs/world.h"
#include "engine/net/protocol.h"

using namespace fate;

TEST_CASE("AppearanceComponent: default values") {
    AppearanceComponent a;
    CHECK(a.gender == 0);
    CHECK(a.hairstyle == 0);
    CHECK(a.armorStyle.empty());
    CHECK(a.hatStyle.empty());
    CHECK(a.weaponStyle.empty());
    CHECK(a.body.front == nullptr);
    CHECK(a.hair.front == nullptr);
    CHECK(a.armor.front == nullptr);
    CHECK(a.hat.front == nullptr);
    CHECK(a.weapon.front == nullptr);
    CHECK(a.dirty == true);
}

TEST_CASE("AppearanceComponent: add to entity") {
    World world;
    auto* e = world.createEntity("test");
    auto* a = e->addComponent<AppearanceComponent>();
    a->gender = 1;
    a->hairstyle = 2;
    CHECK(e->getComponent<AppearanceComponent>()->gender == 1);
    CHECK(e->getComponent<AppearanceComponent>()->hairstyle == 2);
}

TEST_CASE("SvEntityEnterMsg: player appearance round-trips") {
    SvEntityEnterMsg original;
    original.persistentId = 42;
    original.entityType = 0; // player
    original.position = {100.0f, 200.0f};
    original.name = "TestPlayer";
    original.level = 5;
    original.currentHP = 80;
    original.maxHP = 100;
    original.faction = 2;
    original.pkStatus = 1;
    original.honorRank = 3;
    original.gender = 1;
    original.hairstyle = 2;

    uint8_t buf[4096];
    ByteWriter writer(buf, sizeof(buf));
    original.write(writer);

    ByteReader reader(writer.data(), writer.size());
    auto result = SvEntityEnterMsg::read(reader);

    CHECK(result.persistentId == 42);
    CHECK(result.entityType == 0);
    CHECK(result.name == "TestPlayer");
    CHECK(result.gender == 1);
    CHECK(result.hairstyle == 2);
}

TEST_CASE("SvEntityEnterMsg: non-player has no appearance") {
    SvEntityEnterMsg original;
    original.persistentId = 99;
    original.entityType = 1; // mob
    original.position = {50.0f, 50.0f};
    original.name = "Slime";
    original.mobDefId = "mob_slime";

    uint8_t buf[4096];
    ByteWriter writer(buf, sizeof(buf));
    original.write(writer);

    ByteReader reader(writer.data(), writer.size());
    auto result = SvEntityEnterMsg::read(reader);

    CHECK(result.entityType == 1);
    CHECK(result.gender == 0);
    CHECK(result.hairstyle == 0);
}
