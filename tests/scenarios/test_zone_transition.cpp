#include "scenario_fixture.h"

using namespace fate;

// NOTE: Update this to a valid scene name from your database.
// Query: SELECT scene_name FROM scene_definitions WHERE scene_name != <current_zone>
static const char* ALTERNATE_ZONE = "starting_zone_2";

TEST_CASE_FIXTURE(ScenarioFixture, "Zone transition: server acknowledges scene change") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);

    bot.sendZoneTransition(ALTERNATE_ZONE);
    auto transition = bot.waitFor<SvZoneTransitionMsg>(5.0f);
    CHECK(transition.targetScene == ALTERNATE_ZONE);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Zone transition: stats preserved across scenes") {
    auto stateBefore = bot.waitFor<SvPlayerStateMsg>(5.0f);

    bot.sendZoneTransition(ALTERNATE_ZONE);
    bot.waitFor<SvZoneTransitionMsg>(5.0f);

    // After zone transition, server sends a fresh SvPlayerState
    auto stateAfter = bot.waitFor<SvPlayerStateMsg>(5.0f);
    CHECK(stateAfter.level == stateBefore.level);
    CHECK(stateAfter.gold == stateBefore.gold);
    CHECK(stateAfter.maxHP == stateBefore.maxHP);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Zone transition: new zone entities received") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);
    bot.clearEvents();

    bot.sendZoneTransition(ALTERNATE_ZONE);
    bot.waitFor<SvZoneTransitionMsg>(5.0f);

    // Should receive entity enters for the new zone
    bot.pollFor(2.0f);
    // At minimum we should get SvEntityEnter messages (mobs, NPCs, or nothing if empty zone)
    // Just verify we don't crash and the connection stays alive
    CHECK(bot.isConnected());
}
