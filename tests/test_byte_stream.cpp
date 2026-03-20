#include <doctest/doctest.h>
#include "engine/net/byte_stream.h"
#include "engine/net/packet.h"

TEST_CASE("ByteStream primitive round-trip") {
    uint8_t buf[64];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeU8(0xAB);
    w.writeU16(0x1234);
    w.writeU32(0xDEADBEEF);
    w.writeFloat(3.14f);
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    CHECK(r.readU8() == 0xAB);
    CHECK(r.readU16() == 0x1234);
    CHECK(r.readU32() == 0xDEADBEEF);
    CHECK(r.readFloat() == doctest::Approx(3.14f));
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);
}

TEST_CASE("ByteWriter overflow detection") {
    uint8_t buf[4];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeU32(1);
    CHECK_FALSE(w.overflowed());
    w.writeU8(0xFF);
    CHECK(w.overflowed());
    CHECK(w.size() == 4);  // did not advance past capacity
}

TEST_CASE("ByteReader overflow detection") {
    uint8_t buf[2];
    buf[0] = 0x01;
    buf[1] = 0x02;
    fate::ByteReader r(buf, 2);
    CHECK(r.readU8() == 0x01);
    CHECK_FALSE(r.overflowed());
    uint32_t val = r.readU32();  // needs 4 bytes, only 1 left
    CHECK(val == 0);
    CHECK(r.overflowed());
}

TEST_CASE("ByteStream string round-trip") {
    uint8_t buf[256];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeString("hello world");
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString();
    CHECK(s == "hello world");
    CHECK_FALSE(r.overflowed());
}

TEST_CASE("ByteStream Vec2 round-trip") {
    uint8_t buf[16];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeVec2(fate::Vec2(1.5f, -2.25f));
    CHECK_FALSE(w.overflowed());

    fate::ByteReader r(buf, w.size());
    fate::Vec2 v = r.readVec2();
    CHECK(v.x == doctest::Approx(1.5f));
    CHECK(v.y == doctest::Approx(-2.25f));
    CHECK_FALSE(r.overflowed());
}

TEST_CASE("PacketHeader write/read round-trip") {
    fate::PacketHeader hdr;
    hdr.protocolId   = fate::PROTOCOL_ID;
    hdr.sessionToken = 0xCAFEBABE;
    hdr.sequence     = 1001;
    hdr.ack          = 1000;
    hdr.ackBits      = 0xFFFF;
    hdr.channel      = fate::Channel::ReliableOrdered;
    hdr.packetType   = fate::PacketType::CmdMove;
    hdr.payloadSize  = 42;

    uint8_t buf[64];
    fate::ByteWriter w(buf, sizeof(buf));
    hdr.write(w);
    CHECK_FALSE(w.overflowed());
    CHECK(w.size() == fate::PACKET_HEADER_SIZE);

    fate::ByteReader r(buf, w.size());
    fate::PacketHeader h2 = fate::PacketHeader::read(r);
    CHECK_FALSE(r.overflowed());
    CHECK(r.remaining() == 0);

    CHECK(h2.protocolId   == fate::PROTOCOL_ID);
    CHECK(h2.sessionToken == 0xCAFEBABE);
    CHECK(h2.sequence     == 1001);
    CHECK(h2.ack          == 1000);
    CHECK(h2.ackBits      == 0xFFFF);
    CHECK(h2.channel      == fate::Channel::ReliableOrdered);
    CHECK(h2.packetType   == fate::PacketType::CmdMove);
    CHECK(h2.payloadSize  == 42);
}

TEST_CASE("ByteReader rejects NaN float") {
    uint8_t buf[4];
    uint32_t nan_bits = 0x7FC00000;
    std::memcpy(buf, &nan_bits, 4);

    fate::ByteReader r(buf, 4);
    float val = r.readFloat();
    CHECK(val == 0.0f);
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader rejects Inf float") {
    uint8_t buf[4];
    uint32_t inf_bits = 0x7F800000;
    std::memcpy(buf, &inf_bits, 4);

    fate::ByteReader r(buf, 4);
    float val = r.readFloat();
    CHECK(val == 0.0f);
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader rejects oversized string") {
    uint8_t buf[12];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeU16(60000);
    w.writeU8(0x41);

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString();
    CHECK(s.empty());
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader readString with maxLen rejects long strings") {
    uint8_t buf[512];
    fate::ByteWriter w(buf, sizeof(buf));
    std::string longStr(300, 'A');
    w.writeString(longStr);

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString(256);
    CHECK(s.empty());
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader readString with maxLen accepts valid strings") {
    uint8_t buf[512];
    fate::ByteWriter w(buf, sizeof(buf));
    w.writeString("hello");

    fate::ByteReader r(buf, w.size());
    std::string s = r.readString(256);
    CHECK(s == "hello");
    CHECK_FALSE(r.overflowed());
}

TEST_CASE("ByteReader readEnum rejects out-of-range value") {
    enum class TestEnum : uint8_t { A = 0, B = 1, C = 2, MAX = C };

    uint8_t buf[1] = { 5 };
    fate::ByteReader r(buf, 1);
    auto val = r.readEnum<TestEnum>(TestEnum::MAX);
    CHECK(val == TestEnum::A);
    CHECK(r.overflowed());
}

TEST_CASE("ByteReader readEnum accepts valid value") {
    enum class TestEnum : uint8_t { A = 0, B = 1, C = 2, MAX = C };

    uint8_t buf[1] = { 2 };
    fate::ByteReader r(buf, 1);
    auto val = r.readEnum<TestEnum>(TestEnum::MAX);
    CHECK(val == TestEnum::C);
    CHECK_FALSE(r.overflowed());
}

TEST_CASE("ByteReader ok() returns false after overflow") {
    uint8_t buf[1] = { 0x42 };
    fate::ByteReader r(buf, 1);
    CHECK(r.ok());
    r.readU32();
    CHECK_FALSE(r.ok());
}

TEST_CASE("ByteReader rejects NaN in Vec2") {
    uint8_t buf[8];
    uint32_t nan_bits = 0x7FC00000;
    float good = 1.0f;
    std::memcpy(buf, &good, 4);
    std::memcpy(buf + 4, &nan_bits, 4);

    fate::ByteReader r(buf, 8);
    fate::Vec2 v = r.readVec2();
    CHECK(r.overflowed());
}
