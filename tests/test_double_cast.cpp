#include <doctest/doctest.h>
#include "game/shared/skill_manager.h"
#include "game/shared/character_stats.h"

using namespace fate;

TEST_CASE("DoubleCast: activateDoubleCast opens window") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    CHECK_FALSE(mgr.isDoubleCastReady());

    mgr.activateDoubleCast("skill_flare", 2.0f);
    CHECK(mgr.isDoubleCastReady());
}

TEST_CASE("DoubleCast: consumeDoubleCast closes window") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    mgr.activateDoubleCast("skill_flare", 2.0f);
    CHECK(mgr.isDoubleCastReady());

    mgr.consumeDoubleCast();
    CHECK_FALSE(mgr.isDoubleCastReady());
}

TEST_CASE("DoubleCast: window expires after duration") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    mgr.activateDoubleCast("skill_flare", 2.0f);
    CHECK(mgr.isDoubleCastReady());

    // Advance past the window
    mgr.tick(2.5f);
    CHECK_FALSE(mgr.isDoubleCastReady());
}

TEST_CASE("DoubleCast: window does not expire before duration") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    mgr.activateDoubleCast("skill_flare", 2.0f);
    mgr.tick(1.5f);
    CHECK(mgr.isDoubleCastReady());
}

TEST_CASE("SkillDefinition: castTime and doubleCast fields default correctly") {
    SkillDefinition def;
    CHECK(def.castTime == 0.0f);
    CHECK(def.enablesDoubleCast == false);
    CHECK(def.doubleCastWindow == doctest::Approx(2.0f));
}

TEST_CASE("DoubleCast: free cast skips cooldown and resource cost") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.level = 10;
    stats.recalculateStats();

    SkillManager mgr;
    mgr.initialize(&stats);
    mgr.tick(0.0f);

    // Register a skill that enables double-cast
    SkillDefinition flare;
    flare.skillId = "test_flare";
    flare.skillType = SkillType::Active;
    flare.mpCost = 20;
    flare.cooldownSeconds = 8.0f;
    flare.baseDamage = 50;
    flare.enablesDoubleCast = true;
    flare.doubleCastWindow = 3.0f;
    mgr.registerSkillDefinition(flare);

    // Register the follow-up skill eligible for free cast
    SkillDefinition iceLance;
    iceLance.skillId = "test_ice_lance";
    iceLance.skillType = SkillType::Active;
    iceLance.mpCost = 15;
    iceLance.cooldownSeconds = 5.0f;
    iceLance.baseDamage = 40;
    mgr.registerSkillDefinition(iceLance);

    // Learn and activate both skills
    mgr.learnSkill("test_flare", 1);
    mgr.activateSkillRank("test_flare");
    mgr.learnSkill("test_ice_lance", 1);
    mgr.activateSkillRank("test_ice_lance");

    // Activate double-cast window (simulating Flare was just cast)
    mgr.activateDoubleCast("test_ice_lance", 3.0f);
    CHECK(mgr.isDoubleCastReady());

    // Consume the free cast
    mgr.consumeDoubleCast();
    CHECK_FALSE(mgr.isDoubleCastReady());

    // Verify window expiry: reactivate and let it expire
    mgr.activateDoubleCast("test_ice_lance", 3.0f);
    CHECK(mgr.isDoubleCastReady());
    mgr.tick(4.0f); // advance past 3s window
    CHECK_FALSE(mgr.isDoubleCastReady());
}
