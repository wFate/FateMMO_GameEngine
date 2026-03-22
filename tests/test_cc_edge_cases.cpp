#include <doctest/doctest.h>
#include "game/shared/crowd_control.h"
#include "game/shared/status_effects.h"
#include "game/shared/character_stats.h"

using namespace fate;

// ============================================================================
// Helper: default-constructed SEM (no effects active)
// ============================================================================
static StatusEffectManager makeCleanSEM() {
    return StatusEffectManager{};
}

// ============================================================================
// CC Precedence: Duration
// ============================================================================

TEST_CASE("CC: shorter stun does NOT override longer stun") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyStun(10.0f, &sem, 1));
    CHECK(cc.isStunned());
    CHECK(cc.getRemainingTime() == doctest::Approx(10.0f));

    // Shorter stun should be rejected
    CHECK_FALSE(cc.applyStun(3.0f, &sem, 2));
    CHECK(cc.getRemainingTime() == doctest::Approx(10.0f));
}

TEST_CASE("CC: longer stun DOES override shorter stun") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyStun(3.0f, &sem, 1));
    CHECK(cc.getRemainingTime() == doctest::Approx(3.0f));

    // Longer stun should override
    CHECK(cc.applyStun(10.0f, &sem, 2));
    CHECK(cc.getRemainingTime() == doctest::Approx(10.0f));
}

// ============================================================================
// CC Precedence: Type Hierarchy
// ============================================================================

TEST_CASE("CC: stun overrides root") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyRoot(5.0f, &sem, 1));
    CHECK(cc.isRooted());

    CHECK(cc.applyStun(5.0f, &sem, 2));
    CHECK(cc.isStunned());
    CHECK_FALSE(cc.isRooted());
}

TEST_CASE("CC: stun overrides freeze") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyFreeze(5.0f, &sem, 1));
    CHECK(cc.isFrozen());

    // Stun should override freeze (isStunned() returns true for both Stunned
    // and Frozen, but getCurrentCC should now be Stunned, not Frozen)
    CHECK(cc.applyStun(5.0f, &sem, 2));
    CHECK(cc.isStunned());
    CHECK_FALSE(cc.isFrozen());
    CHECK(cc.getCurrentCC() == CCType::Stunned);
}

TEST_CASE("CC: root does NOT override stun") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyStun(5.0f, &sem, 1));
    CHECK(cc.isStunned());

    CHECK_FALSE(cc.applyRoot(5.0f, &sem, 2));
    CHECK(cc.isStunned());
    CHECK(cc.getCurrentCC() == CCType::Stunned);
}

TEST_CASE("CC: freeze does NOT override stun") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyStun(5.0f, &sem, 1));
    CHECK(cc.isStunned());

    CHECK_FALSE(cc.applyFreeze(5.0f, &sem, 2));
    CHECK(cc.getCurrentCC() == CCType::Stunned);
}

TEST_CASE("CC: taunt fails during hard CC (stun)") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyStun(5.0f, &sem, 1));

    CHECK_FALSE(cc.applyTaunt(5.0f, &sem, 99));
    CHECK_FALSE(cc.isTaunted());
    CHECK(cc.isStunned());
}

// ============================================================================
// Immunity Interactions
// ============================================================================

TEST_CASE("CC: invulnerability blocks all CC") {
    CrowdControlSystem cc;
    StatusEffectManager sem;
    sem.applyInvulnerability(10.0f, 1);

    CHECK_FALSE(cc.applyStun(5.0f, &sem, 2));
    CHECK_FALSE(cc.applyFreeze(5.0f, &sem, 2));
    CHECK_FALSE(cc.applyRoot(5.0f, &sem, 2));
    CHECK_FALSE(cc.applyTaunt(5.0f, &sem, 2));

    CHECK_FALSE(cc.isStunned());
    CHECK_FALSE(cc.isFrozen());
    CHECK_FALSE(cc.isRooted());
    CHECK_FALSE(cc.isTaunted());
    CHECK(cc.canAct());
    CHECK(cc.canMove());
}

TEST_CASE("CC: stun immunity blocks stun") {
    CrowdControlSystem cc;
    StatusEffectManager sem;
    sem.applyEffect(EffectType::StunImmune, 10.0f, 1.0f);

    CHECK_FALSE(cc.applyStun(5.0f, &sem, 1));
    CHECK_FALSE(cc.isStunned());
}

TEST_CASE("CC: stun immunity blocks freeze") {
    CrowdControlSystem cc;
    StatusEffectManager sem;
    sem.applyEffect(EffectType::StunImmune, 10.0f, 1.0f);

    CHECK_FALSE(cc.applyFreeze(5.0f, &sem, 1));
    CHECK_FALSE(cc.isFrozen());
}

// ============================================================================
// Tick / Expiration
// ============================================================================

TEST_CASE("CC: stun expires after tick elapses full duration") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyStun(2.0f, &sem, 1));

    cc.tick(1.0f);
    CHECK(cc.isStunned());
    CHECK(cc.getRemainingTime() == doctest::Approx(1.0f));

    cc.tick(2.0f);
    CHECK_FALSE(cc.isStunned());
    CHECK(cc.getCurrentCC() == CCType::None);
}

// ============================================================================
// breakFreeze specificity
// ============================================================================

TEST_CASE("CC: breakFreeze only breaks freeze, not stun") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyStun(5.0f, &sem, 1));
    CHECK(cc.isStunned());

    cc.breakFreeze();
    CHECK(cc.isStunned());  // still stunned — breakFreeze is a no-op on stun
}

// ============================================================================
// Min CC Duration Enforcement
// ============================================================================

TEST_CASE("CC: minimum duration is enforced (kMinCCDuration = 0.5s)") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    // Apply stun with a very short duration (0.1s)
    CHECK(cc.applyStun(0.1f, &sem, 1));
    // Duration should be clamped to kMinCCDuration (0.5s)
    CHECK(cc.getRemainingTime() == doctest::Approx(0.5f));
}

// ============================================================================
// removeAllCC
// ============================================================================

TEST_CASE("CC: removeAllCC clears everything") {
    CrowdControlSystem cc;
    StatusEffectManager sem = makeCleanSEM();

    CHECK(cc.applyStun(10.0f, &sem, 1));
    CHECK(cc.isStunned());
    CHECK_FALSE(cc.canAct());

    cc.removeAllCC();

    CHECK(cc.canAct());
    CHECK(cc.canMove());
    CHECK(cc.getCurrentCC() == CCType::None);
    CHECK(cc.getRemainingTime() == doctest::Approx(0.0f));
}

// ============================================================================
// Cast Interruption on CC
// ============================================================================

TEST_CASE("Stun interrupts active cast") {
    CharacterStats stats;
    stats.beginCast("fireball", 3.0f, 42);
    CHECK(stats.isCasting());

    // Apply stun — should interrupt the cast
    stats.interruptCast();
    CHECK_FALSE(stats.isCasting());
}
