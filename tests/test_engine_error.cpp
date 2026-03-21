#include <doctest/doctest.h>
#include "engine/core/engine_error.h"
#include "engine/core/circuit_breaker.h"

using namespace fate;

TEST_CASE("EngineError categories") {
    EngineError err{ErrorCategory::Transient, 201, "DB timeout"};
    CHECK(err.category == ErrorCategory::Transient);
    CHECK(err.code == 201);
    CHECK(err.message == "DB timeout");
}

TEST_CASE("Result with expected value") {
    Result<int> r = 42;
    CHECK(r.has_value());
    CHECK(r.value() == 42);
}

TEST_CASE("Result with error") {
    Result<int> r = std::unexpected(transientError(100, "timeout"));
    CHECK_FALSE(r.has_value());
    CHECK(r.error().category == ErrorCategory::Transient);
    CHECK(r.error().code == 100);
}

TEST_CASE("CircuitBreaker starts closed") {
    CircuitBreaker cb(3, 5.0f);
    CHECK(cb.state() == CircuitState::Closed);
    CHECK(cb.allowRequest());
}

TEST_CASE("CircuitBreaker opens after N failures") {
    CircuitBreaker cb(3, 5.0f);
    cb.recordFailure();
    cb.recordFailure();
    cb.recordFailure();
    CHECK(cb.state() == CircuitState::Open);
    CHECK_FALSE(cb.allowRequest());
}

TEST_CASE("CircuitBreaker resets on success") {
    CircuitBreaker cb(3, 5.0f);
    cb.recordFailure();
    cb.recordFailure();
    cb.recordSuccess();
    CHECK(cb.state() == CircuitState::Closed);
    CHECK(cb.consecutiveFailures() == 0);
}
