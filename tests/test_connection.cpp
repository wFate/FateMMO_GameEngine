#include <doctest/doctest.h>
#include "engine/net/connection.h"

using namespace fate;

TEST_CASE("ConnectionManager: assign client ID") {
    ConnectionManager mgr;
    NetAddress a1{0x7F000001, 5000};
    NetAddress a2{0x7F000001, 5001};
    uint16_t id1 = mgr.addClient(a1);
    uint16_t id2 = mgr.addClient(a2);
    CHECK(id1 != 0);
    CHECK(id2 != 0);
    CHECK(id1 != id2);
    CHECK(mgr.clientCount() == 2);
}

TEST_CASE("ConnectionManager: find by address") {
    ConnectionManager mgr;
    NetAddress addr{0x7F000001, 5000};
    mgr.addClient(addr);
    auto* c = mgr.findByAddress(addr);
    REQUIRE(c != nullptr);
    CHECK(c->address == addr);
    CHECK(c->sessionToken != 0);
}

TEST_CASE("ConnectionManager: timeout detection") {
    ConnectionManager mgr;
    NetAddress addr{0x7F000001, 5000};
    uint16_t id = mgr.addClient(addr);
    auto* c = mgr.findById(id);
    REQUIRE(c != nullptr);
    c->lastHeartbeat = -11.0f;
    auto timedOut = mgr.getTimedOutClients(0.0f, 10.0f);
    CHECK(timedOut.size() == 1);
    CHECK(timedOut[0] == id);
}

TEST_CASE("ConnectionManager: remove client") {
    ConnectionManager mgr;
    NetAddress addr{0x7F000001, 5000};
    uint16_t id = mgr.addClient(addr);
    CHECK(mgr.clientCount() == 1);
    mgr.removeClient(id);
    CHECK(mgr.clientCount() == 0);
    CHECK(mgr.findById(id) == nullptr);
}

TEST_CASE("ConnectionManager: session token validation") {
    ConnectionManager mgr;
    NetAddress addr{0x7F000001, 5000};
    uint16_t id = mgr.addClient(addr);
    auto* c = mgr.findById(id);
    REQUIRE(c != nullptr);
    CHECK(mgr.validateToken(addr, c->sessionToken));
    CHECK(!mgr.validateToken(addr, c->sessionToken + 1));
}
