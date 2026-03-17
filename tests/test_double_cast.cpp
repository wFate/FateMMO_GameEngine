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
