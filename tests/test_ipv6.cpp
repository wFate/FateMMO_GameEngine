#include <doctest/doctest.h>
#include "engine/net/socket.h"

using namespace fate;

TEST_CASE("NetAddress: resolve resolves localhost") {
    NetAddress addr;
    bool ok = NetAddress::resolve("127.0.0.1", 7777, addr);
    CHECK(ok == true);
    CHECK(addr.port == 7777);
}

TEST_CASE("NetAddress: resolve handles invalid host") {
    NetAddress addr;
    bool ok = NetAddress::resolve("this.does.not.exist.invalid", 7777, addr);
    CHECK(ok == false);
}
