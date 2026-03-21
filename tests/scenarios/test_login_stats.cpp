#include "scenario_fixture.h"

using namespace fate;

TEST_CASE_FIXTURE(ScenarioFixture, "Login: auth response contains valid character data") {
    CHECK(bot.authData().level > 0);
    CHECK(bot.authData().currentHP > 0);
    CHECK(bot.authData().maxHP >= bot.authData().currentHP);
    CHECK(bot.authData().maxMP >= bot.authData().currentMP);
    CHECK(!bot.authData().characterName.empty());
    CHECK(!bot.authData().className.empty());
    CHECK(!bot.authData().sceneName.empty());
}

TEST_CASE_FIXTURE(ScenarioFixture, "Login: SvPlayerState matches auth snapshot") {
    auto state = bot.waitFor<SvPlayerStateMsg>(5.0f);
    CHECK(state.level == bot.authData().level);
    CHECK(state.currentHP == bot.authData().currentHP);
    CHECK(state.maxHP == bot.authData().maxHP);
    CHECK(state.maxMP == bot.authData().maxMP);
    CHECK(state.gold == bot.authData().gold);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Login: server sends initial sync messages") {
    // Server should send SvPlayerState, SvSkillSync, SvInventorySync on connect
    bot.pollFor(3.0f);
    CHECK(bot.hasEvent<SvPlayerStateMsg>());
}
