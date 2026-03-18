#include <doctest/doctest.h>
#include "engine/net/protocol.h"

TEST_CASE("SvEntityEnterMsg round-trip for dropped item (entityType=3)") {
    uint8_t buf[512];
    fate::SvEntityEnterMsg src;
    src.persistentId = 12345;
    src.entityType   = 3;
    src.position     = {100.5f, 200.25f};
    src.name         = "Iron Sword";
    src.level        = 0;
    src.currentHP    = 0;
    src.maxHP        = 0;
    src.faction      = 0;
    src.itemId       = "sword_iron_01";
    src.quantity     = 1;
    src.isGold       = 0;
    src.goldAmount   = 0;
    src.enchantLevel = 3;
    src.rarity       = "Rare";

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityEnterMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.entityType == 3);
    CHECK(dst.itemId == "sword_iron_01");
    CHECK(dst.quantity == 1);
    CHECK(dst.isGold == 0);
    CHECK(dst.goldAmount == 0);
    CHECK(dst.enchantLevel == 3);
    CHECK(dst.rarity == "Rare");
}

TEST_CASE("SvEntityEnterMsg round-trip for gold drop") {
    uint8_t buf[512];
    fate::SvEntityEnterMsg src;
    src.persistentId = 99999;
    src.entityType   = 3;
    src.position     = {50.0f, 75.0f};
    src.name         = "Gold";
    src.itemId       = "";
    src.quantity     = 0;
    src.isGold       = 1;
    src.goldAmount   = 250;
    src.enchantLevel = 0;
    src.rarity       = "";

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityEnterMsg::read(r);
    CHECK(dst.isGold == 1);
    CHECK(dst.goldAmount == 250);
}

TEST_CASE("SvEntityEnterMsg round-trip for player (entityType=0) unchanged") {
    uint8_t buf[512];
    fate::SvEntityEnterMsg src;
    src.persistentId = 555;
    src.entityType   = 0;
    src.position     = {10.0f, 20.0f};
    src.name         = "TestPlayer";
    src.level        = 42;
    src.currentHP    = 500;
    src.maxHP        = 1000;
    src.faction      = 1;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityEnterMsg::read(r);
    CHECK(r.remaining() == 0);
    CHECK(dst.entityType == 0);
    CHECK(dst.name == "TestPlayer");
    CHECK(dst.level == 42);
    CHECK(dst.itemId.empty());
    CHECK(dst.isGold == 0);
}

TEST_CASE("SvLootPickupMsg round-trip") {
    uint8_t buf[512];
    fate::SvLootPickupMsg src;
    src.itemId      = "armor_plate_02";
    src.displayName = "Plate Armor +2";
    src.quantity    = 1;
    src.isGold      = 0;
    src.goldAmount  = 0;
    src.rarity      = "Epic";

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvLootPickupMsg::read(r);
    CHECK(dst.itemId == "armor_plate_02");
    CHECK(dst.displayName == "Plate Armor +2");
    CHECK(dst.quantity == 1);
    CHECK(dst.rarity == "Epic");
}
