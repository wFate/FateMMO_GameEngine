#include <doctest/doctest.h>
#include "game/shared/combat_system.h"

using namespace fate;

TEST_CASE("CombatConfig loads from JSON string") {
    std::string json = R"({
        "pvpDamageMultiplier": 0.10,
        "hitRate": {"sameLevel": 0.80, "lowerLevel": 0.90}
    })";

    CombatConfig cfg;
    cfg.loadFromJsonString(json);

    CHECK(cfg.pvpDamageMultiplier == doctest::Approx(0.10f));
    CHECK(cfg.hitChanceSameLevel == doctest::Approx(0.80f));
    CHECK(cfg.hitChanceLowerLevel == doctest::Approx(0.90f));
}

TEST_CASE("CombatConfig defaults survive partial JSON") {
    std::string json = R"({"pvpDamageMultiplier": 0.07})";

    CombatConfig cfg;
    cfg.loadFromJsonString(json);

    CHECK(cfg.pvpDamageMultiplier == doctest::Approx(0.07f));
    CHECK(cfg.baseCritRate == doctest::Approx(0.05f));
    CHECK(cfg.hitChanceSameLevel == doctest::Approx(0.95f));
}

TEST_CASE("CombatConfig class advantage matrix") {
    std::string json = R"({
        "classAdvantageMatrix": {
            "warrior_vs_mage": 1.20
        }
    })";

    CombatConfig cfg;
    cfg.loadFromJsonString(json);

    CHECK(cfg.getClassAdvantage(ClassType::Warrior, ClassType::Mage) == doctest::Approx(1.20f));
    CHECK(cfg.getClassAdvantage(ClassType::Mage, ClassType::Warrior) == doctest::Approx(1.0f));
}

TEST_CASE("CombatConfig skill PvP multiplier") {
    std::string json = R"({"skillPvpDamageMultiplier": 0.45})";

    CombatConfig cfg;
    cfg.loadFromJsonString(json);

    CHECK(cfg.skillPvpDamageMultiplier == doctest::Approx(0.45f));
}

TEST_CASE("CombatConfig handles invalid JSON gracefully") {
    CombatConfig cfg;
    cfg.loadFromJsonString("not json at all{{{");

    // Should keep all defaults
    CHECK(cfg.pvpDamageMultiplier == doctest::Approx(0.05f));
}
