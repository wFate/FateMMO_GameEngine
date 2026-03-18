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
