#include <doctest/doctest.h>
#include "engine/net/protocol.h"
#include "engine/net/game_messages.h"

TEST_CASE("CmdMove round-trip") {
    uint8_t buf[256];
    fate::CmdMove src;
    src.position  = {10.5f, -20.25f};
    src.velocity  = {1.0f, -0.5f};
    src.timestamp = 123.456f;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::CmdMove::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.position.x == doctest::Approx(10.5f));
    CHECK(dst.position.y == doctest::Approx(-20.25f));
    CHECK(dst.velocity.x == doctest::Approx(1.0f));
    CHECK(dst.velocity.y == doctest::Approx(-0.5f));
    CHECK(dst.timestamp  == doctest::Approx(123.456f));
}

TEST_CASE("CmdChat round-trip with strings") {
    uint8_t buf[512];
    fate::CmdChat src;
    src.channel    = 2;
    src.message    = "Hello, world! Testing unicode-safe ASCII path.";
    src.targetName = "SomePlayer";

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::CmdChat::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.channel    == 2);
    CHECK(dst.message    == "Hello, world! Testing unicode-safe ASCII path.");
    CHECK(dst.targetName == "SomePlayer");
}

TEST_CASE("SvEntityEnterMsg round-trip") {
    uint8_t buf[512];
    fate::SvEntityEnterMsg src;
    src.persistentId = 0xDEADBEEFCAFEull;
    src.entityType   = 1; // mob
    src.position     = {100.0f, 200.0f};
    src.name         = "GoblinWarrior";
    src.level        = 42;
    src.currentHP    = 350;
    src.maxHP        = 500;
    src.faction      = 3;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityEnterMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.persistentId == 0xDEADBEEFCAFEull);
    CHECK(dst.entityType   == 1);
    CHECK(dst.position.x   == doctest::Approx(100.0f));
    CHECK(dst.position.y   == doctest::Approx(200.0f));
    CHECK(dst.name         == "GoblinWarrior");
    CHECK(dst.level        == 42);
    CHECK(dst.currentHP    == 350);
    CHECK(dst.maxHP        == 500);
    CHECK(dst.faction      == 3);
}

TEST_CASE("SvEntityUpdateMsg partial fields (position + HP only)") {
    uint8_t buf[256];
    fate::SvEntityUpdateMsg src;
    src.persistentId = 9999;
    src.fieldMask    = (1 << 0) | (1 << 3); // position + currentHP
    src.position     = {55.5f, -12.0f};
    src.currentHP    = 123;
    // animFrame and flipX are NOT set in fieldMask

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    // Expected size: 1 (updateSeq) + 8 (persistentId) + 2 (fieldMask) + 8 (Vec2) + 4 (i32) = 23
    CHECK(w.size() == 23);

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityUpdateMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.persistentId == 9999);
    CHECK(dst.fieldMask    == ((1 << 0) | (1 << 3)));
    CHECK(dst.position.x   == doctest::Approx(55.5f));
    CHECK(dst.position.y   == doctest::Approx(-12.0f));
    CHECK(dst.currentHP    == 123);
    // Fields not in mask should remain default
    CHECK(dst.animFrame == 0);
    CHECK(dst.flipX     == 0);
}

TEST_CASE("SvCombatEventMsg round-trip") {
    uint8_t buf[256];
    fate::SvCombatEventMsg src;
    src.attackerId = 1001;
    src.targetId   = 2002;
    src.damage     = 9999;
    src.skillId    = 42;
    src.isCrit     = 1;
    src.isKill     = 0;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvCombatEventMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.attackerId == 1001);
    CHECK(dst.targetId   == 2002);
    CHECK(dst.damage     == 9999);
    CHECK(dst.skillId    == 42);
    CHECK(dst.isCrit     == 1);
    CHECK(dst.isKill     == 0);
}

TEST_CASE("SvPlayerStateMsg round-trip with large int64 values") {
    uint8_t buf[256];
    fate::SvPlayerStateMsg src;
    src.currentHP   = 500;
    src.maxHP       = 1000;
    src.currentMP   = 200;
    src.maxMP       = 400;
    src.currentFury = 75.5f;
    src.currentXP   = 1000000000LL;
    src.gold        = 9876543210LL;
    src.level       = 99;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvPlayerStateMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.currentHP   == 500);
    CHECK(dst.maxHP       == 1000);
    CHECK(dst.currentMP   == 200);
    CHECK(dst.maxMP       == 400);
    CHECK(dst.currentFury == doctest::Approx(75.5f));
    CHECK(dst.currentXP   == 1000000000LL);
    CHECK(dst.gold        == 9876543210LL);
    CHECK(dst.level       == 99);
}

TEST_CASE("SvPlayerStateMsg honor fields round-trip") {
    fate::SvPlayerStateMsg src;
    src.currentHP = 100;
    src.maxHP = 200;
    src.level = 50;
    src.honor = 1500;
    src.pvpKills = 42;
    src.pvpDeaths = 10;
    src.currentXP = 99999;
    src.gold = 50000;

    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvPlayerStateMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.honor == 1500);
    CHECK(dst.pvpKills == 42);
    CHECK(dst.pvpDeaths == 10);
    CHECK(dst.level == 50);
    CHECK(dst.currentXP == 99999);
}

TEST_CASE("SvEntityUpdateMsg round-trip with updateSeq") {
    uint8_t buf[256];
    fate::SvEntityUpdateMsg src;
    src.persistentId = 0xCAFE;
    src.fieldMask    = 0x000F;
    src.position     = {10.0f, 20.0f};
    src.animFrame    = 3;
    src.flipX        = 1;
    src.currentHP    = 500;
    src.updateSeq    = 42;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvEntityUpdateMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(dst.updateSeq == 42);
    CHECK(dst.position.x == doctest::Approx(10.0f));
    CHECK(dst.currentHP == 500);
}

TEST_CASE("SvBossLootOwnerMsg serialization round-trip") {
    fate::SvBossLootOwnerMsg original;
    original.bossId = "goblin_king";
    original.winnerName = "TestWarrior";
    original.topDamage = 12500;
    original.wasParty = 1;

    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);

    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvBossLootOwnerMsg::read(r);

    CHECK(decoded.bossId == "goblin_king");
    CHECK(decoded.winnerName == "TestWarrior");
    CHECK(decoded.topDamage == 12500);
    CHECK(decoded.wasParty == 1);
}

TEST_CASE("SvEntityEnterMsg includes pkStatus for players") {
    fate::SvEntityEnterMsg original;
    original.entityType = 0; // player
    original.persistentId = 12345;
    original.name = "TestPlayer";
    original.pkStatus = 2; // Red

    uint8_t buf[512];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);

    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvEntityEnterMsg::read(r);

    CHECK(decoded.entityType == 0);
    CHECK(decoded.pkStatus == 2);
}

TEST_CASE("SvEntityEnterMsg mob does not carry pkStatus") {
    fate::SvEntityEnterMsg original;
    original.entityType = 1; // mob
    original.persistentId = 99;
    original.name = "Goblin";
    original.pkStatus = 0;

    uint8_t buf[512];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);

    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvEntityEnterMsg::read(r);

    CHECK(decoded.entityType == 1);
    CHECK(decoded.pkStatus == 0); // default, not serialized for mobs
}

TEST_CASE("SvEntityUpdateMsg bit 14 carries pkStatus") {
    fate::SvEntityUpdateMsg original;
    original.persistentId = 555;
    original.fieldMask = (1 << 14); // only pkStatus
    original.pkStatus = 3; // Black

    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);

    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvEntityUpdateMsg::read(r);

    CHECK((decoded.fieldMask & (1 << 14)) != 0);
    CHECK(decoded.pkStatus == 3);
}

TEST_CASE("SvBossLootOwnerMsg empty winner name") {
    fate::SvBossLootOwnerMsg original;
    original.bossId = "boss_99";
    original.winnerName = "";
    original.topDamage = 0;
    original.wasParty = 0;

    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);

    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvBossLootOwnerMsg::read(r);

    CHECK(decoded.bossId == "boss_99");
    CHECK(decoded.winnerName == "");
    CHECK(decoded.topDamage == 0);
}

TEST_CASE("CmdEnchantMsg serialization round-trip") {
    fate::CmdEnchantMsg original;
    original.inventorySlot = 5;
    original.useProtectionStone = 1;
    uint8_t buf[64];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);
    fate::ByteReader r(buf, w.size());
    auto decoded = fate::CmdEnchantMsg::read(r);
    CHECK(decoded.inventorySlot == 5);
    CHECK(decoded.useProtectionStone == 1);
}

TEST_CASE("SvEnchantResultMsg serialization round-trip") {
    fate::SvEnchantResultMsg original;
    original.success = 1;
    original.newLevel = 9;
    original.broke = 0;
    original.message = "Enchant succeeded! +9";
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);
    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvEnchantResultMsg::read(r);
    CHECK(decoded.success == 1);
    CHECK(decoded.newLevel == 9);
    CHECK(decoded.broke == 0);
    CHECK(decoded.message == "Enchant succeeded! +9");
}

TEST_CASE("CmdRepairMsg serialization round-trip") {
    fate::CmdRepairMsg original;
    original.inventorySlot = 3;
    uint8_t buf[64];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);
    fate::ByteReader r(buf, w.size());
    auto decoded = fate::CmdRepairMsg::read(r);
    CHECK(decoded.inventorySlot == 3);
}

TEST_CASE("SvRepairResultMsg serialization round-trip") {
    fate::SvRepairResultMsg original;
    original.success = 1;
    original.newLevel = 4;
    original.message = "Item repaired to +4";
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);
    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvRepairResultMsg::read(r);
    CHECK(decoded.success == 1);
    CHECK(decoded.newLevel == 4);
    CHECK(decoded.message == "Item repaired to +4");
}

TEST_CASE("CmdExtractCoreMsg serialization round-trip") {
    fate::CmdExtractCoreMsg original;
    original.itemSlot = 7;
    original.scrollSlot = 2;
    uint8_t buf[64];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);
    fate::ByteReader r(buf, w.size());
    auto decoded = fate::CmdExtractCoreMsg::read(r);
    CHECK(decoded.itemSlot == 7);
    CHECK(decoded.scrollSlot == 2);
}

TEST_CASE("SvExtractResultMsg serialization round-trip") {
    fate::SvExtractResultMsg original;
    original.success = 1;
    original.coreItemId = "mat_core_3rd";
    original.coreQuantity = 4;
    original.message = "Extracted 4x 3rd Core";
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);
    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvExtractResultMsg::read(r);
    CHECK(decoded.success == 1);
    CHECK(decoded.coreItemId == "mat_core_3rd");
    CHECK(decoded.coreQuantity == 4);
    CHECK(decoded.message == "Extracted 4x 3rd Core");
}

TEST_CASE("CmdCraftMsg serialization round-trip") {
    fate::CmdCraftMsg original;
    original.recipeId = "recipe_iron_sword";
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);
    fate::ByteReader r(buf, w.size());
    auto decoded = fate::CmdCraftMsg::read(r);
    CHECK(decoded.recipeId == "recipe_iron_sword");
}

TEST_CASE("SvCraftResultMsg serialization round-trip") {
    fate::SvCraftResultMsg original;
    original.success = 1;
    original.resultItemId = "item_iron_sword";
    original.resultQuantity = 1;
    original.message = "Crafted Iron Sword";
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    original.write(w);
    fate::ByteReader r(buf, w.size());
    auto decoded = fate::SvCraftResultMsg::read(r);
    CHECK(decoded.success == 1);
    CHECK(decoded.resultItemId == "item_iron_sword");
    CHECK(decoded.resultQuantity == 1);
    CHECK(decoded.message == "Crafted Iron Sword");
}

TEST_CASE("CmdBattlefieldMsg round-trip") {
    fate::CmdBattlefieldMsg orig; orig.action = 0;
    uint8_t buf[16]; fate::ByteWriter w(buf, sizeof(buf)); orig.write(w);
    fate::ByteReader r(buf, w.size()); auto d = fate::CmdBattlefieldMsg::read(r);
    CHECK(d.action == 0);
}

TEST_CASE("SvBattlefieldUpdateMsg round-trip with 2 factions") {
    fate::SvBattlefieldUpdateMsg orig;
    orig.state = 2; orig.timeRemaining = 450;
    orig.factionCount = 2;
    orig.factionIds = {1, 2};
    orig.factionKills = {15, 23};
    orig.personalKills = 5;
    orig.result = 0;
    uint8_t buf[64]; fate::ByteWriter w(buf, sizeof(buf)); orig.write(w);
    fate::ByteReader r(buf, w.size()); auto d = fate::SvBattlefieldUpdateMsg::read(r);
    CHECK(d.state == 2);
    CHECK(d.timeRemaining == 450);
    CHECK(d.factionCount == 2);
    CHECK(d.factionIds.size() == 2);
    CHECK(d.factionIds[0] == 1);
    CHECK(d.factionIds[1] == 2);
    CHECK(d.factionKills[0] == 15);
    CHECK(d.factionKills[1] == 23);
    CHECK(d.personalKills == 5);
}

TEST_CASE("CmdArenaMsg round-trip") {
    fate::CmdArenaMsg orig; orig.action = 0; orig.mode = 3;
    uint8_t buf[16]; fate::ByteWriter w(buf, sizeof(buf)); orig.write(w);
    fate::ByteReader r(buf, w.size()); auto d = fate::CmdArenaMsg::read(r);
    CHECK(d.action == 0);
    CHECK(d.mode == 3);
}

TEST_CASE("SvArenaUpdateMsg round-trip") {
    fate::SvArenaUpdateMsg orig;
    orig.state = 2; orig.timeRemaining = 120;
    orig.teamAlive = 2; orig.enemyAlive = 1;
    orig.result = 0; orig.honorReward = 30;
    uint8_t buf[32]; fate::ByteWriter w(buf, sizeof(buf)); orig.write(w);
    fate::ByteReader r(buf, w.size()); auto d = fate::SvArenaUpdateMsg::read(r);
    CHECK(d.state == 2);
    CHECK(d.timeRemaining == 120);
    CHECK(d.teamAlive == 2);
    CHECK(d.enemyAlive == 1);
    CHECK(d.honorReward == 30);
}

TEST_CASE("CmdPetMsg round-trip") {
    fate::CmdPetMsg orig; orig.action = 0; orig.petDbId = 42;
    uint8_t buf[16]; fate::ByteWriter w(buf, sizeof(buf)); orig.write(w);
    fate::ByteReader r(buf, w.size()); auto d = fate::CmdPetMsg::read(r);
    CHECK(d.action == 0); CHECK(d.petDbId == 42);
}

TEST_CASE("SvPetUpdateMsg round-trip") {
    fate::SvPetUpdateMsg orig;
    orig.equipped = 1; orig.petDefId = "pet_wolf"; orig.petName = "Fang";
    orig.level = 5; orig.currentXP = 1200; orig.xpToNextLevel = 1250;
    uint8_t buf[256]; fate::ByteWriter w(buf, sizeof(buf)); orig.write(w);
    fate::ByteReader r(buf, w.size()); auto d = fate::SvPetUpdateMsg::read(r);
    CHECK(d.equipped == 1); CHECK(d.petDefId == "pet_wolf");
    CHECK(d.petName == "Fang"); CHECK(d.level == 5);
    CHECK(d.currentXP == 1200); CHECK(d.xpToNextLevel == 1250);
}

TEST_CASE("CmdBankMsg round-trip") {
    fate::CmdBankMsg orig;
    orig.action = 2; orig.goldAmount = 50000; orig.itemId = ""; orig.itemCount = 0;
    uint8_t buf[256]; fate::ByteWriter w(buf, sizeof(buf)); orig.write(w);
    CHECK_FALSE(w.overflowed());
    fate::ByteReader r(buf, w.size()); auto d = fate::CmdBankMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);
    CHECK(d.action == 2);
    CHECK(d.goldAmount == 50000);
    CHECK(d.itemId == "");
    CHECK(d.itemCount == 0);
}

TEST_CASE("SvBankResultMsg round-trip") {
    fate::SvBankResultMsg orig;
    orig.action = 2; orig.success = 1; orig.bankGold = 999999;
    orig.message = "Deposited 50000 gold";
    uint8_t buf[256]; fate::ByteWriter w(buf, sizeof(buf)); orig.write(w);
    CHECK_FALSE(w.overflowed());
    fate::ByteReader r(buf, w.size()); auto d = fate::SvBankResultMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);
    CHECK(d.action == 2);
    CHECK(d.success == 1);
    CHECK(d.bankGold == 999999);
    CHECK(d.message == "Deposited 50000 gold");
}
