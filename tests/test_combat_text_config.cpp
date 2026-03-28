#include <doctest/doctest.h>
#include "game/systems/combat_text_config.h"
using namespace fate;

TEST_CASE("CombatTextConfig loadDefaults populates all styles") {
    CombatTextConfig cfg;
    cfg.loadDefaults();

    CHECK(cfg.damage.color.r == doctest::Approx(1.0f));
    CHECK(cfg.damage.color.g == doctest::Approx(1.0f));
    CHECK(cfg.damage.fontSize == doctest::Approx(11.0f));

    CHECK(cfg.crit.color.r == doctest::Approx(1.0f));
    CHECK(cfg.crit.color.g == doctest::Approx(0.6f));
    CHECK(cfg.crit.scale == doctest::Approx(1.3f));

    CHECK(cfg.miss.text == "Miss");
    CHECK(cfg.miss.color.r == doctest::Approx(0.5f));

    CHECK(cfg.resist.text == "Resist");
    CHECK(cfg.resist.color.b == doctest::Approx(0.9f));

    CHECK(cfg.xp.text == "+{amount} XP");

    CHECK(cfg.levelUp.text == "LEVEL UP!");
    CHECK(cfg.levelUp.lifetime == doctest::Approx(1.8f));
    CHECK(cfg.levelUp.scale == doctest::Approx(1.3f));

    CHECK(cfg.heal.color.g == doctest::Approx(0.9f));

    CHECK(cfg.block.text == "Block");
}

TEST_CASE("CombatTextConfig loadFromJsonString applies values") {
    CombatTextConfig cfg;
    cfg.loadDefaults();

    std::string json = R"({
        "miss": {
            "text": "DODGE!",
            "color": [1.0, 0.0, 0.0, 1.0],
            "fontSize": 20.0,
            "lifetime": 2.5
        }
    })";
    cfg.loadFromJsonString(json);

    CHECK(cfg.miss.text == "DODGE!");
    CHECK(cfg.miss.color.r == doctest::Approx(1.0f));
    CHECK(cfg.miss.color.g == doctest::Approx(0.0f));
    CHECK(cfg.miss.fontSize == doctest::Approx(20.0f));
    CHECK(cfg.miss.lifetime == doctest::Approx(2.5f));

    // Unmodified styles keep defaults
    CHECK(cfg.damage.fontSize == doctest::Approx(11.0f));
    CHECK(cfg.levelUp.text == "LEVEL UP!");
}

TEST_CASE("CombatTextConfig partial JSON preserves defaults") {
    CombatTextConfig cfg;
    cfg.loadDefaults();

    std::string json = R"({ "damage": { "fontSize": 18.0 } })";
    cfg.loadFromJsonString(json);

    CHECK(cfg.damage.fontSize == doctest::Approx(18.0f));
    CHECK(cfg.damage.color.r == doctest::Approx(1.0f));
    CHECK(cfg.damage.lifetime == doctest::Approx(1.2f));
    CHECK(cfg.damage.floatSpeed == doctest::Approx(30.0f));
}

TEST_CASE("CombatTextConfig invalid JSON does not crash") {
    CombatTextConfig cfg;
    cfg.loadDefaults();

    cfg.loadFromJsonString("not json at all {{{");

    CHECK(cfg.miss.text == "Miss");
    CHECK(cfg.damage.lifetime == doctest::Approx(1.2f));
}

TEST_CASE("CombatTextConfig round-trip save and load") {
    CombatTextConfig cfg;
    cfg.loadDefaults();
    cfg.miss.text = "Whiff";
    cfg.crit.popScale = 2.5f;
    cfg.heal.color = Color(0.0f, 1.0f, 0.5f, 0.9f);

    std::string path = "test_combat_text_roundtrip.json";
    CHECK(cfg.save(path));

    CombatTextConfig cfg2;
    cfg2.loadDefaults();
    CHECK(cfg2.load(path));

    CHECK(cfg2.miss.text == "Whiff");
    CHECK(cfg2.crit.popScale == doctest::Approx(2.5f));
    CHECK(cfg2.heal.color.g == doctest::Approx(1.0f));
    CHECK(cfg2.heal.color.a == doctest::Approx(0.9f));

    std::remove(path.c_str());
}
