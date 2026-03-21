#include <doctest/doctest.h>
#include "engine/net/packet.h"
#include "engine/net/byte_stream.h"

using namespace fate;

TEST_CASE("Protocol version constant exists and is positive") {
    CHECK(PROTOCOL_VERSION >= 1);
}

TEST_CASE("Connect payload format: version byte + auth token") {
    // Simulate client building connect payload
    uint8_t buf[17]; // 1 version + 16 token
    buf[0] = PROTOCOL_VERSION;

    // Fake auth token
    for (int i = 0; i < 16; ++i) buf[i + 1] = static_cast<uint8_t>(i + 1);

    // Simulate server reading
    uint8_t version = buf[0];
    CHECK(version == PROTOCOL_VERSION);

    // Auth token starts at offset 1
    CHECK(buf[1] == 1);
    CHECK(buf[16] == 16);
}
