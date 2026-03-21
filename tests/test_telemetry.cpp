#include <doctest/doctest.h>
#include "engine/telemetry/telemetry.h"

TEST_SUITE("Telemetry") {

TEST_CASE("can record metrics") {
    TelemetryCollector collector;
    collector.record("fps_avg", 58.5f);
    collector.record("session_length_s", 1200.0f);
    CHECK(collector.pendingCount() == 2);
}

TEST_CASE("flush clears pending metrics") {
    TelemetryCollector collector;
    collector.record("fps_avg", 60.0f);
    CHECK(collector.pendingCount() == 1);
    auto json = collector.flushToJson();
    CHECK(collector.pendingCount() == 0);
    CHECK(json.contains("metrics"));
    CHECK(json["metrics"].size() == 1);
}

TEST_CASE("serialized JSON has correct structure") {
    TelemetryCollector collector;
    collector.setSessionId("test-session-123");
    collector.record("fps_p99", 16.2f);
    auto json = collector.flushToJson();
    CHECK(json["session_id"] == "test-session-123");
    CHECK(json["metrics"][0]["name"] == "fps_p99");
    CHECK(json["metrics"][0]["value"] == doctest::Approx(16.2f));
    CHECK(json["metrics"][0].contains("timestamp"));
}

TEST_CASE("empty flush returns empty metrics array") {
    TelemetryCollector collector;
    auto json = collector.flushToJson();
    CHECK(json["metrics"].empty());
}

TEST_CASE("trySend without endpoint returns false") {
    TelemetryCollector collector;
    collector.record("test", 1.0f);
    CHECK_FALSE(collector.trySend());
}

} // TEST_SUITE
