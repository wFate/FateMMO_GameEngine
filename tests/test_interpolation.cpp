#include <doctest/doctest.h>
#include "engine/net/interpolation.h"

using namespace fate;

TEST_CASE("InterpolationState: lerp between positions") {
    InterpolationState state;
    state.onServerUpdate({0.0f, 0.0f});
    state.onServerUpdate({100.0f, 0.0f});

    // At t=0, should be at previous position
    Vec2 pos = state.interpolate(0.0f);
    CHECK(pos.x == doctest::Approx(0.0f));

    // At t=25ms (half interval), should be halfway
    pos = state.interpolate(0.025f);
    CHECK(pos.x == doctest::Approx(50.0f));

    // At t=25ms more (full interval), should be at target
    pos = state.interpolate(0.025f);
    CHECK(pos.x == doctest::Approx(100.0f));
}

TEST_CASE("InterpolationState: clamp at target") {
    InterpolationState state;
    state.onServerUpdate({0.0f, 0.0f});
    state.onServerUpdate({100.0f, 200.0f});

    // Overshoot — should clamp at target
    Vec2 pos = state.interpolate(0.1f); // 100ms > 50ms interval
    CHECK(pos.x == doctest::Approx(100.0f));
    CHECK(pos.y == doctest::Approx(200.0f));
}

TEST_CASE("InterpolationManager: track multiple entities") {
    InterpolationManager mgr;
    mgr.onEntityUpdate(1, {0.0f, 0.0f});
    mgr.onEntityUpdate(1, {100.0f, 0.0f});
    mgr.onEntityUpdate(2, {50.0f, 50.0f});
    mgr.onEntityUpdate(2, {150.0f, 50.0f});

    Vec2 p1 = mgr.getInterpolatedPosition(1, 0.025f); // half
    Vec2 p2 = mgr.getInterpolatedPosition(2, 0.05f);  // full

    CHECK(p1.x == doctest::Approx(50.0f));
    CHECK(p2.x == doctest::Approx(150.0f));

    mgr.removeEntity(1);
    // Entity 1 removed, should return default
    Vec2 p1b = mgr.getInterpolatedPosition(1, 0.0f);
    CHECK(p1b.x == doctest::Approx(0.0f));
}
