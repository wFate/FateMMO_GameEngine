#include "scenario_fixture.h"

using namespace fate;

TEST_CASE_FIXTURE(ScenarioFixture, "Combat: attack mob and receive combat event") {
    // Wait for initial state sync
    bot.waitFor<SvPlayerStateMsg>(5.0f);

    // Collect entity enters to find mobs in the zone
    bot.pollFor(2.0f);
    auto mobs = bot.entityEntersOfType(1); // entityType 1 = mob
    REQUIRE_MESSAGE(!mobs.empty(),
        "No mobs found in zone — ensure test account is in a zone with mobs");

    uint64_t mobId = mobs[0].persistentId;
    bot.sendAttack(mobId);

    auto combat = bot.waitFor<SvCombatEventMsg>(3.0f);
    CHECK(combat.targetId == mobId);
    // damage may be 0 on miss/dodge; assert attack was processed
    CHECK(combat.attackerId != 0);
}

TEST_CASE_FIXTURE(ScenarioFixture, "Combat: entity update reflects HP change after attack") {
    bot.waitFor<SvPlayerStateMsg>(5.0f);
    bot.pollFor(2.0f);
    auto mobs = bot.entityEntersOfType(1);
    if (mobs.empty()) {
        WARN("No mobs in zone — skipping HP update test");
        return;
    }

    uint64_t mobId = mobs[0].persistentId;
    int32_t initialHP = mobs[0].currentHP;
    bot.sendAttack(mobId);

    // Wait for combat event first
    auto combat = bot.waitFor<SvCombatEventMsg>(3.0f);

    // If damage landed, check for entity update with reduced HP
    if (combat.damage > 0) {
        bot.pollFor(1.0f);
        // Look for an entity update for this mob with HP field set
        bool foundHPUpdate = false;
        for (const auto& upd : bot.getQueue<SvEntityUpdateMsg>()) {
            if (upd.persistentId == mobId && (upd.fieldMask & (1 << 3))) {
                CHECK(upd.currentHP < initialHP);
                foundHPUpdate = true;
                break;
            }
        }
        CHECK_MESSAGE(foundHPUpdate, "Expected entity update with HP change for attacked mob");
    }
}
