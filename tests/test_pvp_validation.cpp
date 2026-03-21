#include <doctest/doctest.h>
#include "server/target_validator.h"
#include "game/shared/character_stats.h"

using namespace fate;

TEST_SUITE("PvP Target Validation") {

TEST_CASE("same faction cannot attack each other (both white)") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    attacker.faction = Faction::Xyros;
    target.faction = Faction::Xyros;
    target.pkStatus = PKStatus::White;
    bool result = TargetValidator::canAttackPlayer(attacker, target, false, false);
    CHECK_FALSE(result);
}

TEST_CASE("different factions can attack") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    attacker.faction = Faction::Xyros;
    target.faction = Faction::Fenor;
    bool result = TargetValidator::canAttackPlayer(attacker, target, false, false);
    CHECK(result);
}

TEST_CASE("cannot attack party member") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    attacker.faction = Faction::Xyros;
    target.faction = Faction::Fenor;
    bool result = TargetValidator::canAttackPlayer(attacker, target, true, false);
    CHECK_FALSE(result);
}

TEST_CASE("cannot attack in safe zone") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    attacker.faction = Faction::Xyros;
    target.faction = Faction::Fenor;
    bool result = TargetValidator::canAttackPlayer(attacker, target, false, true);
    CHECK_FALSE(result);
}

TEST_CASE("can attack same faction if target is Red") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    attacker.faction = Faction::Xyros;
    target.faction = Faction::Xyros;
    target.pkStatus = PKStatus::Red;
    bool result = TargetValidator::canAttackPlayer(attacker, target, false, false);
    CHECK(result);
}

TEST_CASE("can attack same faction if target is Black") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    attacker.faction = Faction::Xyros;
    target.faction = Faction::Xyros;
    target.pkStatus = PKStatus::Black;
    bool result = TargetValidator::canAttackPlayer(attacker, target, false, false);
    CHECK(result);
}

TEST_CASE("cannot attack dead target") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    target.faction = Faction::Fenor; attacker.faction = Faction::Xyros;
    target.isDead = true;
    target.lifeState = LifeState::Dead;
    bool result = TargetValidator::canAttackPlayer(attacker, target, false, false);
    CHECK_FALSE(result);
}

TEST_CASE("cannot attack dying target") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    target.faction = Faction::Fenor; attacker.faction = Faction::Xyros;
    target.lifeState = LifeState::Dying;
    bool result = TargetValidator::canAttackPlayer(attacker, target, false, false);
    CHECK_FALSE(result);
}

TEST_CASE("isAttackerAlive rejects dying attacker") {
    CharacterStats stats;
    stats.maxHP = 100; stats.currentHP = 0;
    stats.lifeState = LifeState::Dying;
    bool result = TargetValidator::isAttackerAlive(stats);
    CHECK_FALSE(result);
}

TEST_CASE("faction None can be attacked by anyone") {
    CharacterStats attacker, target;
    attacker.maxHP = 100; attacker.currentHP = 100;
    target.maxHP = 100; target.currentHP = 100;
    attacker.faction = Faction::Xyros;
    target.faction = Faction::None;
    bool result = TargetValidator::canAttackPlayer(attacker, target, false, false);
    CHECK(result);
}

} // TEST_SUITE
