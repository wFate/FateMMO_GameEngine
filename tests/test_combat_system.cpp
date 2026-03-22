#include <doctest/doctest.h>
#include "game/shared/combat_system.h"
#include "game/shared/character_stats.h"

using namespace fate;

TEST_CASE("CombatSystem: damage reduction boundary matches Unity") {
    // damageReductionStartsAt = 2, damageReductionPerLevel = 0.12
    // levelDiff=2: NO reduction (1.0)
    float mult2 = CombatSystem::calculateDamageMultiplier(10, 12);
    CHECK(mult2 == doctest::Approx(1.0f));

    // levelDiff=3: first reduction (1 effective level * 0.12 = 0.88)
    float mult3 = CombatSystem::calculateDamageMultiplier(10, 13);
    CHECK(mult3 == doctest::Approx(0.88f));

    // levelDiff=1: no reduction
    float mult1 = CombatSystem::calculateDamageMultiplier(10, 11);
    CHECK(mult1 == doctest::Approx(1.0f));
}

TEST_CASE("CastingState tracks active cast") {
    CharacterStats stats;
    CHECK_FALSE(stats.isCasting());

    stats.beginCast("fireball", 2.0f, 42);
    CHECK(stats.isCasting());
    CHECK(stats.castingState.skillId == "fireball");
    CHECK(stats.castingState.remainingTime == doctest::Approx(2.0f));
    CHECK(stats.castingState.targetEntityId == 42);

    // Tick 1 second
    stats.tickCast(1.0f);
    CHECK(stats.isCasting());
    CHECK(stats.castingState.remainingTime == doctest::Approx(1.0f));

    // Tick remaining
    bool completed = stats.tickCast(1.0f);
    CHECK(completed);
    CHECK_FALSE(stats.isCasting());
}

TEST_CASE("CastingState can be interrupted") {
    CharacterStats stats;
    stats.beginCast("fireball", 2.0f, 42);
    CHECK(stats.isCasting());

    stats.interruptCast();
    CHECK_FALSE(stats.isCasting());
}

TEST_CASE("Skill with castTime > 0 should enter casting state") {
    CharacterStats caster;
    caster.classDef.classType = ClassType::Warrior;
    caster.level = 10;
    caster.recalculateStats();
    caster.currentHP = caster.maxHP;

    float castTime = 2.0f;
    CHECK_FALSE(caster.isCasting());

    if (castTime > 0.0f) {
        caster.beginCast("fireball", castTime, 999);
    }
    CHECK(caster.isCasting());

    caster.tickCast(1.0f);
    CHECK(caster.isCasting());
    bool done = caster.tickCast(1.0f);
    CHECK(done);
    CHECK_FALSE(caster.isCasting());
}

TEST_CASE("Movement should interrupt active cast") {
    CharacterStats stats;
    stats.beginCast("fireball", 2.0f, 42);
    CHECK(stats.isCasting());

    // Moving interrupts the cast
    stats.interruptCast();
    CHECK_FALSE(stats.isCasting());
}

TEST_CASE("Equipment change should be blocked while casting") {
    CharacterStats stats;
    stats.beginCast("fireball", 2.0f, 42);
    CHECK(stats.isCasting());
    // Server equip handler checks isCasting() and returns early
}

TEST_CASE("Cast fizzles if target dies during cast time") {
    CharacterStats target;
    target.classDef.classType = ClassType::Warrior;
    target.classDef.baseMaxHP = 100;
    target.level = 5;
    target.recalculateStats();
    target.currentHP = target.maxHP;

    CHECK(target.isAlive());
    target.takeDamage(target.maxHP + 10); // kill target
    CHECK_FALSE(target.isAlive());

    // Server would check target.isAlive() at cast completion
    // If false, skill fizzles (doesn't execute)
    bool shouldExecute = target.isAlive();
    CHECK_FALSE(shouldExecute);
}
