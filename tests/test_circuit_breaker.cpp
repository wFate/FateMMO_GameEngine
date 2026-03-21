#include <doctest/doctest.h>
#include "server/db/circuit_breaker.h"

using namespace fate;
using State = DbCircuitBreaker::State;

TEST_CASE("DbCircuitBreaker: starts closed") {
    DbCircuitBreaker cb(5, 30.0);
    CHECK(cb.state() == State::Closed);
    CHECK(cb.allowRequest() == true);
}

TEST_CASE("DbCircuitBreaker: opens after N failures") {
    DbCircuitBreaker cb(3, 10.0);
    cb.recordFailure(0.0);
    cb.recordFailure(0.0);
    CHECK(cb.state() == State::Closed);
    cb.recordFailure(0.0);
    CHECK(cb.state() == State::Open);
    CHECK(cb.allowRequest() == false);
}

TEST_CASE("DbCircuitBreaker: transitions to half-open after cooldown") {
    DbCircuitBreaker cb(3, 10.0);
    cb.recordFailure(0.0);
    cb.recordFailure(0.0);
    cb.recordFailure(0.0);
    CHECK(cb.allowRequest() == false);

    cb.updateTime(15.0);
    CHECK(cb.state() == State::Open); // still open until allowRequest checks
    CHECK(cb.allowRequest() == true);  // transitions to half-open
    CHECK(cb.state() == State::HalfOpen);
}

TEST_CASE("DbCircuitBreaker: closes on success after half-open") {
    DbCircuitBreaker cb(3, 10.0);
    for (int i = 0; i < 3; ++i) cb.recordFailure(0.0);
    cb.updateTime(15.0);
    cb.allowRequest(); // transitions to half-open
    cb.recordSuccess();
    CHECK(cb.state() == State::Closed);
}

TEST_CASE("DbCircuitBreaker: re-opens on failure in half-open") {
    DbCircuitBreaker cb(3, 10.0);
    for (int i = 0; i < 3; ++i) cb.recordFailure(0.0);
    cb.updateTime(15.0);
    cb.allowRequest(); // transitions to half-open
    cb.recordFailure(15.0);
    CHECK(cb.state() == State::Open);
}

TEST_CASE("DbCircuitBreaker: success resets consecutive failures") {
    DbCircuitBreaker cb(5, 10.0);
    cb.recordFailure(0.0);
    cb.recordFailure(0.0);
    cb.recordSuccess();
    CHECK(cb.consecutiveFailures() == 0);
    // Should need 5 more failures to open, not 3
    cb.recordFailure(1.0);
    cb.recordFailure(1.0);
    cb.recordFailure(1.0);
    CHECK(cb.state() == State::Closed);
}
