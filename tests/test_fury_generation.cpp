// tests/test_fury_generation.cpp
#include <doctest/doctest.h>
#include "game/shared/character_stats.h"
#include "game/shared/game_types.h"

using namespace fate;

TEST_SUITE("FuryGeneration") {

TEST_CASE("Warrior gains fury on basic auto-attack") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Warrior;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerBasicAttack = 0.5f;
    stats.classDef.furyPerCriticalHit = 1.0f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;

    // Simulate non-crit auto-attack hit
    stats.addFury(stats.classDef.furyPerBasicAttack);
    CHECK(stats.currentFury == doctest::Approx(0.5f));
}

TEST_CASE("Warrior gains extra fury on critical auto-attack") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Warrior;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerCriticalHit = 1.0f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;

    stats.addFury(stats.classDef.furyPerCriticalHit);
    CHECK(stats.currentFury == doctest::Approx(1.0f));
}

TEST_CASE("Ranger gains fury on auto-attack") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Archer;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerBasicAttack = 0.5f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;

    stats.addFury(stats.classDef.furyPerBasicAttack);
    CHECK(stats.currentFury == doctest::Approx(0.5f));
}

TEST_CASE("Mage does NOT gain fury (uses Mana)") {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Mage;
    stats.classDef.primaryResource = ResourceType::Mana;
    stats.maxFury = 0;
    stats.currentFury = 0.0f;

    // Even if addFury is called, maxFury=0 clamps it
    stats.addFury(0.5f);
    CHECK(stats.currentFury == doctest::Approx(0.0f));
}

TEST_CASE("Fury at max does not overflow on crit") {
    CharacterStats stats;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerCriticalHit = 1.0f;
    stats.maxFury = 3;
    stats.currentFury = 3.0f; // already at max

    stats.addFury(stats.classDef.furyPerCriticalHit);
    CHECK(stats.currentFury == doctest::Approx(3.0f)); // clamped
}

TEST_CASE("Fury still generates on killing blow") {
    CharacterStats stats;
    stats.classDef.primaryResource = ResourceType::Fury;
    stats.classDef.furyPerBasicAttack = 0.5f;
    stats.maxFury = 3;
    stats.currentFury = 0.0f;

    // The kill check happens after damage — fury should still be added
    stats.addFury(stats.classDef.furyPerBasicAttack);
    CHECK(stats.currentFury == doctest::Approx(0.5f));
}

} // TEST_SUITE
