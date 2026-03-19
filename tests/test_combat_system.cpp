#include <doctest/doctest.h>
#include "game/shared/combat_system.h"

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
