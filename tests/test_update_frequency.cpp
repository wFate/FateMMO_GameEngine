#include <doctest/doctest.h>
#include "engine/net/update_frequency.h"

using namespace fate;

TEST_CASE("Update tier from distance") {
    CHECK(getUpdateTier(5.0f * 32) == UpdateTier::Near);    // 5 tiles
    CHECK(getUpdateTier(10.0f * 32) == UpdateTier::Near);   // exactly 10 tiles
    CHECK(getUpdateTier(15.0f * 32) == UpdateTier::Mid);    // 15 tiles
    CHECK(getUpdateTier(25.0f * 32) == UpdateTier::Mid);    // exactly 25 tiles
    CHECK(getUpdateTier(30.0f * 32) == UpdateTier::Far);    // 30 tiles
    CHECK(getUpdateTier(40.0f * 32) == UpdateTier::Far);    // exactly 40 tiles
    CHECK(getUpdateTier(45.0f * 32) == UpdateTier::Edge);   // 45 tiles
}

TEST_CASE("Tick interval per tier") {
    CHECK(getTickInterval(UpdateTier::Near) == 1);   // every tick (20Hz)
    CHECK(getTickInterval(UpdateTier::Mid) == 3);    // every 3rd (~7Hz)
    CHECK(getTickInterval(UpdateTier::Far) == 5);    // every 5th (4Hz)
    CHECK(getTickInterval(UpdateTier::Edge) == 10);  // every 10th (2Hz)
}

TEST_CASE("shouldSendUpdate respects tick counter") {
    // Near: every tick
    CHECK(shouldSendUpdate(UpdateTier::Near, 0));
    CHECK(shouldSendUpdate(UpdateTier::Near, 1));
    CHECK(shouldSendUpdate(UpdateTier::Near, 99));

    // Mid: every 3rd tick
    CHECK(shouldSendUpdate(UpdateTier::Mid, 0));
    CHECK_FALSE(shouldSendUpdate(UpdateTier::Mid, 1));
    CHECK_FALSE(shouldSendUpdate(UpdateTier::Mid, 2));
    CHECK(shouldSendUpdate(UpdateTier::Mid, 3));

    // Far: every 5th tick
    CHECK(shouldSendUpdate(UpdateTier::Far, 0));
    CHECK_FALSE(shouldSendUpdate(UpdateTier::Far, 1));
    CHECK(shouldSendUpdate(UpdateTier::Far, 5));

    // Edge: every 10th tick
    CHECK(shouldSendUpdate(UpdateTier::Edge, 0));
    CHECK_FALSE(shouldSendUpdate(UpdateTier::Edge, 1));
    CHECK(shouldSendUpdate(UpdateTier::Edge, 10));
}
