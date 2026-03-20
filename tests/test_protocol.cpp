#include <doctest/doctest.h>
#include "engine/net/protocol.h"

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
