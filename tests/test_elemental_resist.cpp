#include <doctest/doctest.h>
#include "game/shared/character_stats.h"
#include "game/shared/game_types.h"
#include <cmath>

using namespace fate;

TEST_CASE("ElementalResist: getElementalResist returns correct resist for each damage type") {
    CharacterStats stats;
    stats.equipResistFire = 0.25f;
    stats.equipResistWater = 0.15f;
    stats.equipResistPoison = 0.10f;
    stats.equipResistLightning = 0.30f;
    stats.equipResistVoid = 0.05f;
    stats.equipResistMagic = 0.20f;

    CHECK(stats.getElementalResist(DamageType::Fire) == doctest::Approx(0.25f));
    CHECK(stats.getElementalResist(DamageType::Water) == doctest::Approx(0.15f));
    CHECK(stats.getElementalResist(DamageType::Poison) == doctest::Approx(0.10f));
    CHECK(stats.getElementalResist(DamageType::Lightning) == doctest::Approx(0.30f));
    CHECK(stats.getElementalResist(DamageType::Void) == doctest::Approx(0.05f));
    CHECK(stats.getElementalResist(DamageType::Magic) == doctest::Approx(0.20f));
    CHECK(stats.getElementalResist(DamageType::Physical) == doctest::Approx(0.0f));
    CHECK(stats.getElementalResist(DamageType::True) == doctest::Approx(0.0f));
}

TEST_CASE("ElementalResist: resist caps at 75%") {
    CharacterStats stats;
    stats.equipResistFire = 0.90f;

    CHECK(stats.getElementalResist(DamageType::Fire) == doctest::Approx(0.75f));
}

TEST_CASE("ElementalResist: damage reduction math") {
    int damage = 100;
    float resist = 0.25f;
    int reduced = static_cast<int>(std::round(damage * (1.0f - resist)));
    CHECK(reduced == 75);
}

TEST_CASE("ElementalResist: zero resist means no reduction") {
    CharacterStats stats;
    // All defaults are 0.0f
    for (int i = 0; i < 8; i++) {
        CHECK(stats.getElementalResist(static_cast<DamageType>(i)) == doctest::Approx(0.0f));
    }
}
