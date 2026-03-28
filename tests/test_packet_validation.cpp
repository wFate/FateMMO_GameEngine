#include <doctest/doctest.h>
#include "engine/net/byte_stream.h"
#include "engine/net/protocol.h"
#include "engine/net/game_messages.h"

using namespace fate;

// =============================================================================
// Verify that truncated payloads set overflow on ByteReader after Msg::read()
// =============================================================================

TEST_CASE("CmdMove::read with empty payload overflows") {
    uint8_t buf[1] = {0};
    ByteReader r(buf, 0);
    auto msg = CmdMove::read(r);
    CHECK_FALSE(r.ok());
    // All fields should be zero-initialized
    CHECK(msg.position.x == 0.0f);
    CHECK(msg.position.y == 0.0f);
    CHECK(msg.timestamp == 0.0f);
}

TEST_CASE("CmdMove::read with partial payload overflows") {
    // CmdMove needs 20 bytes (2xVec2 + float). Give it only 8.
    uint8_t buf[8];
    ByteWriter w(buf, sizeof(buf));
    w.writeFloat(100.0f); // position.x
    w.writeFloat(200.0f); // position.y
    // velocity and timestamp missing

    ByteReader r(buf, w.size());
    auto msg = CmdMove::read(r);
    CHECK_FALSE(r.ok());
}

TEST_CASE("CmdChat::read with empty payload overflows") {
    uint8_t buf[1] = {0};
    ByteReader r(buf, 0);
    auto msg = CmdChat::read(r);
    CHECK_FALSE(r.ok());
    CHECK(msg.message.empty());
    CHECK(msg.targetName.empty());
}

TEST_CASE("CmdUseSkillMsg::read with truncated payload overflows") {
    // Only write the skill ID string, omit rank and targetId
    uint8_t buf[32];
    ByteWriter w(buf, sizeof(buf));
    w.writeString("fireball");
    // rank (U8) and targetId (U64) missing

    ByteReader r(buf, w.size());
    auto msg = CmdUseSkillMsg::read(r);
    CHECK_FALSE(r.ok());
}

TEST_CASE("CmdEquipMsg::read with empty payload overflows") {
    uint8_t buf[1] = {0};
    ByteReader r(buf, 0);
    auto msg = CmdEquipMsg::read(r);
    CHECK_FALSE(r.ok());
}

TEST_CASE("CmdAction::read with partial payload overflows") {
    // CmdAction needs 1 + 8 + 2 = 11 bytes. Give it 3.
    uint8_t buf[3] = {0x01, 0x00, 0x00};
    ByteReader r(buf, 3);
    auto msg = CmdAction::read(r);
    CHECK_FALSE(r.ok());
}

TEST_CASE("Trade sub-action AddItem with only sub-action byte overflows") {
    // AddItem needs: U8(slot) + I32(sourceSlot) + String(instanceId) + I32(quantity)
    // Give only the sub-action byte
    uint8_t buf[1] = {0x02}; // TradeAction::AddItem
    ByteReader r(buf, 1);
    uint8_t subAction = r.readU8();
    CHECK(r.ok()); // sub-action byte itself succeeds
    // But reading AddItem fields should overflow
    uint8_t slotIdx = r.readU8();
    CHECK_FALSE(r.ok());
    (void)slotIdx;
    (void)subAction;
}

TEST_CASE("Valid CmdMove::read succeeds") {
    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    w.writeVec2(Vec2(100.0f, 200.0f));
    w.writeVec2(Vec2(1.0f, 0.0f));
    w.writeFloat(1.5f);

    ByteReader r(buf, w.size());
    auto msg = CmdMove::read(r);
    CHECK(r.ok());
    CHECK(msg.position.x == doctest::Approx(100.0f));
    CHECK(msg.position.y == doctest::Approx(200.0f));
    CHECK(msg.timestamp == doctest::Approx(1.5f));
}

TEST_CASE("Valid CmdChat::read succeeds") {
    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    w.writeU8(1); // channel
    w.writeString("hello");
    w.writeString("player1");

    ByteReader r(buf, w.size());
    auto msg = CmdChat::read(r);
    CHECK(r.ok());
    CHECK(msg.channel == 1);
    CHECK(msg.message == "hello");
    CHECK(msg.targetName == "player1");
}
