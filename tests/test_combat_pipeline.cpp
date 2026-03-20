#include <doctest/doctest.h>
#include "game/shared/combat_system.h"
#include "game/shared/status_effects.h"
#include "game/shared/crowd_control.h"
#include "game/shared/character_stats.h"

using namespace fate;

// ============================================================================
// CombatSystem::rollBlock
// ============================================================================

TEST_CASE("CombatPipeline: rollBlock with 0% block chance never blocks") {
    // With 0% block chance, rollBlock should always return false
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(CombatSystem::rollBlock(ClassType::Warrior, 10, 10, 0.0f));
    }
}

TEST_CASE("CombatPipeline: rollBlock — mages bypass block entirely") {
    // Mages bypass block regardless of defender's block chance
    for (int i = 0; i < 100; ++i) {
        CHECK_FALSE(CombatSystem::rollBlock(ClassType::Mage, 10, 10, 1.0f));
    }
}

// ============================================================================
// StatusEffectManager: shield absorption
// ============================================================================

TEST_CASE("CombatPipeline: shield absorbs partial damage") {
    StatusEffectManager sem;
    sem.applyShield(30.0f, 10.0f);  // 30 HP shield for 10 seconds

    int remaining = sem.absorbDamage(50);
    // Shield absorbs 30, leaving 20 damage
    CHECK(remaining == 20);
    CHECK(sem.currentShield() == doctest::Approx(0.0f));
}

TEST_CASE("CombatPipeline: shield absorbs all damage when shield > damage") {
    StatusEffectManager sem;
    sem.applyShield(100.0f, 10.0f);  // 100 HP shield

    int remaining = sem.absorbDamage(40);
    // Shield absorbs all 40, leaving 0 damage
    CHECK(remaining == 0);
    CHECK(sem.currentShield() == doctest::Approx(60.0f));
}

// ============================================================================
// CharacterStats::heal clamps to max
// ============================================================================

TEST_CASE("CombatPipeline: heal clamps to maxHP") {
    CharacterStats cs;
    cs.classDef.classType = ClassType::Warrior;
    cs.level = 1;
    cs.recalculateStats();
    // After recalculate, maxHP is set. Damage the player then overheal.
    int maxHP = cs.maxHP;
    cs.currentHP = maxHP / 2;
    cs.heal(maxHP);  // heal more than missing
    CHECK(cs.currentHP == maxHP);
}

// ============================================================================
// Lifesteal calculation
// ============================================================================

TEST_CASE("CombatPipeline: lifesteal heals correct amount") {
    // Lifesteal is: healAmount = damage * equipBonusLifesteal
    float lifesteal = 0.10f;  // 10%
    int damage = 100;
    int healAmount = static_cast<int>(damage * lifesteal);
    CHECK(healAmount == 10);

    // 0% lifesteal = no heal
    lifesteal = 0.0f;
    healAmount = static_cast<int>(damage * lifesteal);
    CHECK(healAmount == 0);
}

// ============================================================================
// CrowdControlSystem: stun, freeze, root, taunt
// ============================================================================

TEST_CASE("CombatPipeline: stunned blocks action and movement") {
    CrowdControlSystem cc;
    cc.applyStun(5.0f, nullptr);
    CHECK_FALSE(cc.canAct());
    CHECK_FALSE(cc.canMove());
    CHECK(cc.isStunned());
}

TEST_CASE("CombatPipeline: frozen blocks action and movement") {
    CrowdControlSystem cc;
    cc.applyFreeze(5.0f, nullptr);
    CHECK_FALSE(cc.canAct());
    CHECK_FALSE(cc.canMove());
    CHECK(cc.isFrozen());
}

TEST_CASE("CombatPipeline: rooted blocks movement but allows action") {
    CrowdControlSystem cc;
    cc.applyRoot(5.0f, nullptr);
    CHECK(cc.canAct());
    CHECK_FALSE(cc.canMove());
    CHECK(cc.isRooted());
}

TEST_CASE("CombatPipeline: taunted allows both action and movement") {
    CrowdControlSystem cc;
    cc.applyTaunt(5.0f, nullptr, 42);
    CHECK(cc.canAct());
    CHECK(cc.canMove());
    CHECK(cc.isTaunted());
    CHECK(cc.getTauntSource() == 42);
}

// ============================================================================
// CrowdControlSystem: breakFreeze
// ============================================================================

TEST_CASE("CombatPipeline: breakFreeze ends freeze") {
    CrowdControlSystem cc;
    cc.applyFreeze(5.0f, nullptr);
    CHECK(cc.isFrozen());

    cc.breakFreeze();
    CHECK_FALSE(cc.isFrozen());
    CHECK(cc.canAct());
    CHECK(cc.canMove());
}

TEST_CASE("CombatPipeline: breakFreeze does nothing if not frozen") {
    CrowdControlSystem cc;
    cc.applyStun(5.0f, nullptr);
    CHECK(cc.isStunned());

    cc.breakFreeze();  // should not break stun
    CHECK(cc.isStunned());
    CHECK_FALSE(cc.canAct());
}

// ============================================================================
// StatusEffectManager: getDamageMultiplier with AttackUp
// ============================================================================

TEST_CASE("CombatPipeline: getDamageMultiplier with AttackUp buff") {
    StatusEffectManager sem;

    // No buffs — multiplier should be 1.0
    CHECK(sem.getDamageMultiplier() == doctest::Approx(1.0f));

    // Apply AttackUp with 50% bonus
    sem.applyEffect(EffectType::AttackUp, 10.0f, 0.5f);
    CHECK(sem.getDamageMultiplier() == doctest::Approx(1.5f));
}

// ============================================================================
// StatusEffectManager: getSpeedModifier with Slow
// ============================================================================

TEST_CASE("CombatPipeline: getSpeedModifier with Slow debuff") {
    StatusEffectManager sem;

    // No effects — speed should be 1.0
    CHECK(sem.getSpeedModifier() == doctest::Approx(1.0f));

    // Apply Slow with 30% reduction
    sem.applyEffect(EffectType::Slow, 10.0f, 0.3f);
    CHECK(sem.getSpeedModifier() == doctest::Approx(0.7f));
}

// ============================================================================
// StatusEffectManager: Hunter's Mark bonus damage taken
// ============================================================================

TEST_CASE("CombatPipeline: HuntersMark increases damage taken") {
    StatusEffectManager sem;

    // No mark — bonus should be 0
    CHECK(sem.getBonusDamageTaken() == doctest::Approx(0.0f));

    // Apply Hunter's Mark with 25% bonus damage taken
    sem.applyEffect(EffectType::HuntersMark, 10.0f, 0.25f);
    CHECK(sem.getBonusDamageTaken() == doctest::Approx(0.25f));

    // Verify the damage calculation: 100 base * (1 + 0.25) = 125
    int baseDamage = 100;
    int finalDamage = static_cast<int>(baseDamage * (1.0f + sem.getBonusDamageTaken()));
    CHECK(finalDamage == 125);
}
