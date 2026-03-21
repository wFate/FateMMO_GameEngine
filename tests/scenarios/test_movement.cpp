#include "scenario_fixture.h"

using namespace fate;

TEST_CASE_FIXTURE(ScenarioFixture, "Movement: bot moves and receives entity enters") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);

    // Move to a position — should trigger entity visibility updates
    bot.sendMove({100.0f, 200.0f}, {0.0f, 0.0f});
    bot.pollFor(1.0f);

    // We should have received at least our own entity or nearby entities
    CHECK(bot.entityEnters().size() > 0);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Movement: no rubber-band for valid movement") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);
    bot.clearEventsOf<SvMovementCorrectionMsg>();

    // Small, valid move — should not trigger rubber-banding
    bot.sendMove({50.0f, 50.0f}, {0.0f, 0.0f});
    bot.pollFor(0.5f);

    CHECK(bot.eventCount<SvMovementCorrectionMsg>() == 0);
}
