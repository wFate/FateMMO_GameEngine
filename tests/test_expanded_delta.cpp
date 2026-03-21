#include <doctest/doctest.h>
#include "engine/net/protocol.h"
#include "engine/net/byte_stream.h"

using namespace fate;

TEST_CASE("Expanded SvEntityUpdateMsg round-trips all fields") {
    SvEntityUpdateMsg msg;
    msg.persistentId = 42;
    msg.fieldMask = 0x3FFF; // all 14 bits
    msg.position = {100.0f, 200.0f};
    msg.animFrame = 3;
    msg.flipX = 1;
    msg.currentHP = 500;
    msg.maxHP = 1000;
    msg.moveState = 1;
    msg.animId = 5;
    msg.statusEffectMask = 0x000F;
    msg.deathState = 0;
    msg.castingSkillId = 10;
    msg.castingProgress = 128;
    msg.targetEntityId = 99;
    msg.level = 25;
    msg.faction = 2;
    msg.equipVisuals = 0x12345678;
    msg.updateSeq = 7;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvEntityUpdateMsg::read(r);

    CHECK(decoded.persistentId == 42);
    CHECK(decoded.position.x == doctest::Approx(100.0f));
    CHECK(decoded.position.y == doctest::Approx(200.0f));
    CHECK(decoded.animFrame == 3);
    CHECK(decoded.flipX == 1);
    CHECK(decoded.currentHP == 500);
    CHECK(decoded.maxHP == 1000);
    CHECK(decoded.moveState == 1);
    CHECK(decoded.animId == 5);
    CHECK(decoded.statusEffectMask == 0x000F);
    CHECK(decoded.deathState == 0);
    CHECK(decoded.castingSkillId == 10);
    CHECK(decoded.castingProgress == 128);
    CHECK(decoded.targetEntityId == 99);
    CHECK(decoded.level == 25);
    CHECK(decoded.faction == 2);
    CHECK(decoded.equipVisuals == 0x12345678);
    CHECK(decoded.updateSeq == 7);
}

TEST_CASE("Delta only sends dirty fields - compact") {
    SvEntityUpdateMsg msg;
    msg.persistentId = 1;
    msg.fieldMask = (1 << 0) | (1 << 4); // only position + maxHP
    msg.position = {50.0f, 60.0f};
    msg.maxHP = 800;
    msg.updateSeq = 1;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);

    // seq(1) + pid(8) + mask(2) + pos(8) + maxHP(4) = 23 bytes
    CHECK(w.size() == 23);

    ByteReader r(buf, w.size());
    auto decoded = SvEntityUpdateMsg::read(r);
    CHECK(decoded.position.x == doctest::Approx(50.0f));
    CHECK(decoded.maxHP == 800);
    // Fields not in mask should remain default
    CHECK(decoded.animFrame == 0);
    CHECK(decoded.currentHP == 0);
}

TEST_CASE("Original 4 fields still work") {
    SvEntityUpdateMsg msg;
    msg.persistentId = 10;
    msg.fieldMask = 0x000F; // original 4 bits
    msg.position = {1.0f, 2.0f};
    msg.animFrame = 5;
    msg.flipX = 1;
    msg.currentHP = 100;
    msg.updateSeq = 3;

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvEntityUpdateMsg::read(r);
    CHECK(decoded.position.x == doctest::Approx(1.0f));
    CHECK(decoded.animFrame == 5);
    CHECK(decoded.flipX == 1);
    CHECK(decoded.currentHP == 100);
}
