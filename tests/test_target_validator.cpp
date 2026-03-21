#include <doctest/doctest.h>
#include "server/target_validator.h"
#include "engine/net/aoi.h"
#include "engine/ecs/entity_handle.h"

using namespace fate;

TEST_CASE("TargetValidator: rejects entity not in AOI") {
    VisibilitySet aoi;
    aoi.current.push_back(EntityHandle(100));
    aoi.current.push_back(EntityHandle(200));
    std::sort(aoi.current.begin(), aoi.current.end());

    CHECK(TargetValidator::isInAOI(aoi, 100) == true);
    CHECK(TargetValidator::isInAOI(aoi, 200) == true);
    CHECK(TargetValidator::isInAOI(aoi, 300) == false);
}

TEST_CASE("TargetValidator: range check with tolerance") {
    Vec2 playerPos{100.0f, 100.0f};
    Vec2 targetPos{148.0f, 100.0f}; // 48px away
    float maxRange = 64.0f;
    float latencyTolerance = 16.0f;

    CHECK(TargetValidator::isInRange(playerPos, targetPos, maxRange, latencyTolerance) == true);

    Vec2 farTarget{300.0f, 100.0f}; // 200px away
    CHECK(TargetValidator::isInRange(playerPos, farTarget, maxRange, latencyTolerance) == false);
}
