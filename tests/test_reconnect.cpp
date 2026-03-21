#include <doctest/doctest.h>
#include "engine/net/reconnect_state.h"

using namespace fate;

TEST_CASE("ReconnectState: starts idle") {
    ReconnectState rs;
    CHECK(rs.state() == ReconnectPhase::Idle);
}

TEST_CASE("ReconnectState: transitions through phases") {
    ReconnectState rs;
    rs.beginReconnect(0.0);
    CHECK(rs.state() == ReconnectPhase::Reconnecting);

    CHECK(rs.shouldAttemptNow(0.5) == false); // too soon
    CHECK(rs.shouldAttemptNow(1.1) == true);  // past 1s backoff

    rs.onAttemptFailed(1.1);
    CHECK(rs.shouldAttemptNow(2.0) == false); // next backoff is 2s from 1.1
    CHECK(rs.shouldAttemptNow(3.2) == true);  // past 1.1 + 2.0 = 3.1
}

TEST_CASE("ReconnectState: succeeds and resets") {
    ReconnectState rs;
    rs.beginReconnect(0.0);
    rs.onSuccess();
    CHECK(rs.state() == ReconnectPhase::Idle);
}

TEST_CASE("ReconnectState: gives up after timeout") {
    ReconnectState rs;
    rs.beginReconnect(0.0);
    CHECK(rs.isTimedOut(59.0) == false);
    CHECK(rs.isTimedOut(61.0) == true);
}

TEST_CASE("ReconnectState: backoff caps at 30s") {
    ReconnectState rs;
    rs.beginReconnect(0.0);
    double t = 1.0;
    // Fail many times to hit the cap: 1, 2, 4, 8, 16, 30, 30...
    for (int i = 0; i < 10; ++i) {
        rs.onAttemptFailed(t);
        t += 50.0; // jump ahead
    }
    // After many failures, backoff should be capped at 30
    rs.onAttemptFailed(t);
    // Next attempt should be t + 30
    CHECK(rs.shouldAttemptNow(t + 29.0) == false);
    CHECK(rs.shouldAttemptNow(t + 31.0) == true);
}
