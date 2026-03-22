#include <doctest/doctest.h>
#include "engine/net/connection.h"

using namespace fate;

TEST_CASE("Heartbeat: new client initialized with currentTime") {
    ConnectionManager mgr;
    uint16_t id = mgr.addClient(NetAddress::makeIPv4(0x7F000001, 12345), 5.0f);
    auto* client = mgr.findById(id);
    REQUIRE(client != nullptr);
    CHECK(client->lastHeartbeat == 5.0f);
    // New client should NOT time out within first 10 seconds
    CHECK(mgr.getTimedOutClients(10.0f, 10.0f).empty());
}

TEST_CASE("Heartbeat: client times out after inactivity") {
    ConnectionManager mgr;
    uint16_t id = mgr.addClient(NetAddress::makeIPv4(0x7F000001, 12345), 1.0f);
    mgr.heartbeat(id, 1.0f);

    // At t=8 (7s since last heartbeat), 10s timeout -- should not time out
    CHECK(mgr.getTimedOutClients(8.0f, 10.0f).empty());

    // At t=12 (11s since last heartbeat) -- should time out
    auto timedOut = mgr.getTimedOutClients(12.0f, 10.0f);
    CHECK(timedOut.size() == 1);
    CHECK(timedOut[0] == id);
}

TEST_CASE("Heartbeat: heartbeat resets timeout") {
    ConnectionManager mgr;
    uint16_t id = mgr.addClient(NetAddress::makeIPv4(0x7F000001, 12345), 0.0f);
    mgr.heartbeat(id, 5.0f);
    mgr.heartbeat(id, 10.0f);

    // At t=15, only 5s since last heartbeat -- should not time out with 10s timeout
    CHECK(mgr.getTimedOutClients(15.0f, 10.0f).empty());

    // At t=21, 11s since last heartbeat -- should time out
    auto timedOut = mgr.getTimedOutClients(21.0f, 10.0f);
    CHECK(timedOut.size() == 1);
}

TEST_CASE("Heartbeat: default currentTime preserves backward compat") {
    ConnectionManager mgr;
    // Calling addClient without currentTime should default to 0.0f
    uint16_t id = mgr.addClient(NetAddress::makeIPv4(0x7F000001, 12345));
    auto* client = mgr.findById(id);
    REQUIRE(client != nullptr);
    CHECK(client->lastHeartbeat == 0.0f);
}
