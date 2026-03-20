#include <doctest/doctest.h>
#include "game/shared/character_stats.h"
using namespace fate;

TEST_CASE("addXP awards XP and triggers level-up with overflow") {
    CharacterStats s;
    s.level = 1;
    s.currentXP = 0;
    s.xpToNextLevel = 100;
    s.maxHP = 100;
    s.currentHP = 100;

    s.addXP(150); // 50 more than needed

    CHECK(s.level == 2);
    CHECK(s.currentXP == 50); // overflow carries
}

TEST_CASE("addXP handles multi-level gain") {
    CharacterStats s;
    s.level = 1;
    s.currentXP = 0;
    s.xpToNextLevel = 100;
    s.maxHP = 100;
    s.currentHP = 100;

    s.addXP(500); // should gain multiple levels

    CHECK(s.level > 2);
}

TEST_CASE("addXP with zero does nothing") {
    CharacterStats s;
    s.level = 5;
    s.currentXP = 50;
    s.xpToNextLevel = 200;

    s.addXP(0);

    CHECK(s.level == 5);
    CHECK(s.currentXP == 50);
}
