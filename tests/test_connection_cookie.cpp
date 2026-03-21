#include <doctest/doctest.h>
#include "engine/net/connection_cookie.h"

using namespace fate;

TEST_CASE("ConnectionCookie: generate and validate") {
    ConnectionCookieGenerator gen("test_server_secret_key_32bytes!");
    uint32_t clientIp = 0x7F000001;
    uint16_t clientPort = 12345;
    uint64_t clientNonce = 0xDEADBEEF;
    double timestamp = 1000.0;

    auto cookie = gen.generate(clientIp, clientPort, clientNonce, timestamp);
    CHECK(cookie != 0);

    bool valid = gen.validate(cookie, clientIp, clientPort, clientNonce, timestamp);
    CHECK(valid == true);
}

TEST_CASE("ConnectionCookie: rejects wrong IP") {
    ConnectionCookieGenerator gen("test_server_secret_key_32bytes!");
    uint64_t nonce = 0x1234;
    auto cookie = gen.generate(0x7F000001, 100, nonce, 0.0);
    CHECK(gen.validate(cookie, 0x7F000002, 100, nonce, 0.0) == false);
}

TEST_CASE("ConnectionCookie: expires after 20 seconds") {
    ConnectionCookieGenerator gen("test_server_secret_key_32bytes!");
    auto cookie = gen.generate(1, 1, 1, 5.0);  // bucket 0
    CHECK(gen.validate(cookie, 1, 1, 1, 5.0) == true);   // same bucket
    CHECK(gen.validate(cookie, 1, 1, 1, 15.0) == true);   // next bucket checks prev
    CHECK(gen.validate(cookie, 1, 1, 1, 25.0) == false);  // two buckets later = expired
}

TEST_CASE("ConnectionCookie: different secrets produce different cookies") {
    ConnectionCookieGenerator gen1("secret_one");
    ConnectionCookieGenerator gen2("secret_two");
    auto c1 = gen1.generate(1, 1, 1, 0.0);
    auto c2 = gen2.generate(1, 1, 1, 0.0);
    CHECK(c1 != c2);
}

TEST_CASE("ConnectionCookie: rejects wrong nonce") {
    ConnectionCookieGenerator gen("mysecret");
    auto cookie = gen.generate(1, 1, 42, 0.0);
    CHECK(gen.validate(cookie, 1, 1, 99, 0.0) == false);
}
