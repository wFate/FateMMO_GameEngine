#include <doctest/doctest.h>
#include "engine/net/byte_stream.h"
#include "engine/net/protocol.h"
#include "engine/net/game_messages.h"
#include "game/shared/game_types.h"

TEST_SUITE("Network Robustness") {

// ============================================================================
// ByteReader from empty buffer
// ============================================================================

TEST_CASE("ByteReader from empty buffer does not crash") {
    uint8_t dummy = 0;
    fate::ByteReader r(&dummy, 0);

    CHECK(r.readU8() == 0);
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader empty buffer readFloat returns 0") {
    uint8_t dummy = 0;
    fate::ByteReader r(&dummy, 0);

    float f = r.readFloat();
    CHECK(f == 0.0f);
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader empty buffer readString returns empty") {
    uint8_t dummy = 0;
    fate::ByteReader r(&dummy, 0);

    std::string s = r.readString();
    CHECK(s.empty());
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader empty buffer readVec2 returns zeroes") {
    uint8_t dummy = 0;
    fate::ByteReader r(&dummy, 0);

    fate::Vec2 v = r.readVec2();
    CHECK(v.x == 0.0f);
    CHECK(v.y == 0.0f);
    CHECK(r.overflowed());
}

// ============================================================================
// Truncated CmdMove
// ============================================================================

TEST_CASE("Truncated CmdMove does not crash") {
    // CmdMove needs 2 Vec2 (16 bytes) + 1 float (4 bytes) = 20 bytes
    // Write only 2 bytes
    uint8_t buf[2] = {0xAB, 0xCD};
    fate::ByteReader r(buf, 2);

    auto msg = fate::CmdMove::read(r);
    CHECK(r.overflowed());
    // Values should be zeroed out due to overflow
    CHECK(msg.position.x == 0.0f);
    CHECK(msg.position.y == 0.0f);
}

// ============================================================================
// Truncated SvEntityEnterMsg
// ============================================================================

TEST_CASE("Truncated SvEntityEnterMsg does not crash") {
    // Write only the persistentId portion (8 bytes), nothing else
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    fate::detail::writeU64(w, 12345);
    // Stop here - much shorter than a full SvEntityEnterMsg

    fate::ByteReader r(buf, w.size());
    auto msg = fate::SvEntityEnterMsg::read(r);
    CHECK(r.overflowed());
    CHECK(msg.persistentId == 12345);
    // All remaining fields should be defaults/zeroes
}

// ============================================================================
// Truncated string field
// ============================================================================

TEST_CASE("String with length header but insufficient data") {
    // Write a string length of 100 but only supply 5 bytes of actual data
    uint8_t buf[16];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeU16(100);  // claims 100 bytes of string follow
    // Write only 5 bytes
    uint8_t fiveBytes[5] = {'H', 'e', 'l', 'l', 'o'};
    w.writeBytes(fiveBytes, 5);

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString();
    CHECK(s.empty());       // readString should detect insufficient data
    CHECK(r.overflowed());  // and set overflow flag
}

// ============================================================================
// SvCombatEventMsg with garbage values
// ============================================================================

TEST_CASE("SvCombatEventMsg with extreme values does not crash") {
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));

    // Write max uint64 for attackerId
    fate::detail::writeU64(w, UINT64_MAX);
    // Write max uint64 for targetId
    fate::detail::writeU64(w, UINT64_MAX);
    // Negative damage
    w.writeI32(-999);
    // Max skillId
    w.writeU16(UINT16_MAX);
    // isCrit and isKill
    w.writeU8(255);
    w.writeU8(255);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto msg = fate::SvCombatEventMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(msg.attackerId == UINT64_MAX);
    CHECK(msg.targetId == UINT64_MAX);
    CHECK(msg.damage == -999);
    CHECK(msg.skillId == UINT16_MAX);
    CHECK(msg.isCrit == 255);
    CHECK(msg.isKill == 255);
}

// ============================================================================
// CmdUseSkillMsg round-trip
// ============================================================================

TEST_CASE("CmdUseSkillMsg round-trip") {
    uint8_t buf[256];
    fate::CmdUseSkillMsg src;
    src.skillId  = "fireball_rank3";
    src.rank     = 3;
    src.targetId = 42001;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::CmdUseSkillMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.skillId == "fireball_rank3");
    CHECK(dst.rank == 3);
    CHECK(dst.targetId == 42001);
}

// ============================================================================
// CmdUseSkillMsg truncated
// ============================================================================

TEST_CASE("CmdUseSkillMsg truncated after skillId") {
    // Write only the skillId portion, then truncate
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeString("fireball");
    // Stop here - don't write rank or targetId

    fate::ByteReader r(buf, w.size());
    auto msg = fate::CmdUseSkillMsg::read(r);
    CHECK(r.overflowed());
    CHECK(msg.skillId == "fireball");  // string was complete
    CHECK(msg.rank == 0);              // zeroed due to overflow
}

// ============================================================================
// SvSkillResultMsg round-trip
// ============================================================================

TEST_CASE("SvSkillResultMsg round-trip") {
    uint8_t buf[256];
    fate::SvSkillResultMsg src;
    src.casterId     = 1001;
    src.targetId     = 2002;
    src.skillId      = "power_strike";
    src.damage       = 450;
    src.overkill     = 50;
    src.targetNewHP  = 0;
    src.hitFlags     = fate::HitFlags::HIT | fate::HitFlags::CRIT | fate::HitFlags::KILLED;
    src.resourceCost = 30;
    src.cooldownMs   = 5000;
    src.casterNewMP  = 170;

    fate::ByteWriter w(buf, sizeof(buf));
    src.write(w);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    auto dst = fate::SvSkillResultMsg::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(dst.casterId == 1001);
    CHECK(dst.targetId == 2002);
    CHECK(dst.skillId == "power_strike");
    CHECK(dst.damage == 450);
    CHECK(dst.overkill == 50);
    CHECK(dst.targetNewHP == 0);
    CHECK(dst.hitFlags == (fate::HitFlags::HIT | fate::HitFlags::CRIT | fate::HitFlags::KILLED));
    CHECK(dst.resourceCost == 30);
    CHECK(dst.cooldownMs == 5000);
    CHECK(dst.casterNewMP == 170);
}

// ============================================================================
// Double-read past end
// ============================================================================

TEST_CASE("Double-read past end stays overflowed and returns 0") {
    uint8_t buf[4];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeU16(0x1234);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    // First read succeeds
    CHECK(r.readU16() == 0x1234);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    // Second read goes past end
    uint16_t v1 = r.readU16();
    CHECK(v1 == 0);
    CHECK(r.overflowed());

    // Third read still overflowed, still returns 0
    uint32_t v2 = r.readU32();
    CHECK(v2 == 0);
    CHECK(r.overflowed());

    float v3 = r.readFloat();
    CHECK(v3 == 0.0f);
    CHECK(r.overflowed());

    std::string s = r.readString();
    CHECK(s.empty());
    CHECK(r.overflowed());
}

// ============================================================================
// Additional: readVec2 past end
// ============================================================================

TEST_CASE("readVec2 partially past end") {
    // Only 4 bytes available, Vec2 needs 8
    uint8_t buf[4];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeFloat(1.5f);

    fate::ByteReader r(buf, w.size());
    fate::Vec2 v = r.readVec2();
    // x reads ok but y overflows
    CHECK(r.overflowed());
    // x should have read 1.5, but once overflow triggers y zeroes it out
    // After first readFloat (succeeds -> 1.5), second readFloat overflows -> 0
    CHECK(v.y == 0.0f);
}

// ============================================================================
// Additional: zero-length string round-trip
// ============================================================================

TEST_CASE("Zero-length string round-trip") {
    uint8_t buf[64];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeString("");
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString();
    CHECK(s.empty());
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);
}

} // TEST_SUITE
