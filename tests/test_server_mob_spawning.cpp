#include <doctest/doctest.h>
#include "engine/net/protocol.h"
using namespace fate;

TEST_CASE("SvEntityEnterMsg mob fields round-trip") {
    SvEntityEnterMsg src;
    src.persistentId = 12345;
    src.entityType = 1;
    src.position = {100.0f, 200.0f};
    src.name = "Timber Wolf";
    src.level = 5;
    src.currentHP = 80;
    src.maxHP = 100;
    src.faction = 0;
    src.mobDefId = "timber_wolf";
    src.isBoss = 0;

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvEntityEnterMsg::read(r);

    CHECK(dst.entityType == 1);
    CHECK(dst.name == "Timber Wolf");
    CHECK(dst.level == 5);
    CHECK(dst.currentHP == 80);
    CHECK(dst.maxHP == 100);
    CHECK(dst.mobDefId == "timber_wolf");
    CHECK(dst.isBoss == 0);
}

TEST_CASE("SvEntityEnterMsg boss mob round-trip") {
    SvEntityEnterMsg src;
    src.entityType = 1;
    src.name = "Timber Alpha";
    src.mobDefId = "timber_alpha";
    src.isBoss = 1;

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvEntityEnterMsg::read(r);

    CHECK(dst.mobDefId == "timber_alpha");
    CHECK(dst.isBoss == 1);
}

TEST_CASE("SvEntityEnterMsg player has no mob fields") {
    SvEntityEnterMsg src;
    src.entityType = 0;
    src.name = "TestPlayer";
    src.level = 10;
    src.currentHP = 500;
    src.maxHP = 500;

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvEntityEnterMsg::read(r);

    CHECK(dst.entityType == 0);
    CHECK(dst.mobDefId.empty());
    CHECK(dst.isBoss == 0);
}
