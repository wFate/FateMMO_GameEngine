#include <doctest/doctest.h>
#include "server/target_validator.h"
#include "engine/net/aoi.h"
#include "engine/ecs/entity_handle.h"

using namespace fate;

// isInAOI requires a ReplicationManager (for PersistentId → EntityHandle resolution)
// which is hard to mock in a unit test. The range check tests below validate the
// core spatial logic. Full AOI validation is covered by integration tests.

TEST_CASE("TargetValidator: range check with tolerance") {
    Vec2 playerPos{100.0f, 100.0f};
    Vec2 targetPos{148.0f, 100.0f}; // 48px away
    float maxRange = 64.0f;
    float latencyTolerance = 16.0f;

    CHECK(TargetValidator::isInRange(playerPos, targetPos, maxRange, latencyTolerance) == true);

    Vec2 farTarget{300.0f, 100.0f}; // 200px away
    CHECK(TargetValidator::isInRange(playerPos, farTarget, maxRange, latencyTolerance) == false);
}
