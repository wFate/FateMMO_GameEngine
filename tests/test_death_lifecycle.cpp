#include <doctest/doctest.h>
#include "game/shared/character_stats.h"

using namespace fate;

// Helper: create a simple Warrior with known HP
static CharacterStats makeWarrior(int hp = 100) {
    CharacterStats stats;
    stats.classDef.classType = ClassType::Warrior;
    stats.level = 1;
    stats.recalculateStats();
    stats.currentHP = hp;
    stats.maxHP = hp;
    return stats;
}

TEST_SUITE("Death Lifecycle") {

TEST_CASE("new character starts Alive") {
    auto stats = makeWarrior();
    CHECK(stats.lifeState == LifeState::Alive);
    CHECK(stats.isAlive());
    CHECK_FALSE(stats.isDying());
    CHECK_FALSE(stats.isDead);
}

TEST_CASE("lethal damage transitions to Dying, not Dead") {
    auto stats = makeWarrior(50);
    stats.takeDamage(9999);  // overkill

    CHECK(stats.lifeState == LifeState::Dying);
    CHECK(stats.isDying());
    CHECK_FALSE(stats.isAlive());
    // isDead should NOT be set yet (backward compat flag stays false until Dead)
    CHECK_FALSE(stats.isDead);
    CHECK(stats.currentHP == 0);
}

TEST_CASE("advanceDeathTick transitions Dying to Dead") {
    auto stats = makeWarrior(50);
    stats.takeDamage(9999);
    CHECK(stats.lifeState == LifeState::Dying);

    stats.advanceDeathTick();

    CHECK(stats.lifeState == LifeState::Dead);
    CHECK_FALSE(stats.isDying());
    CHECK_FALSE(stats.isAlive());
    CHECK(stats.isDead);  // backward compat flag now set
}

TEST_CASE("advanceDeathTick is a no-op for Alive and Dead states") {
    SUBCASE("Alive stays Alive") {
        auto stats = makeWarrior();
        stats.advanceDeathTick();
        CHECK(stats.lifeState == LifeState::Alive);
        CHECK(stats.isAlive());
    }
    SUBCASE("Dead stays Dead") {
        auto stats = makeWarrior(50);
        stats.takeDamage(9999);
        stats.advanceDeathTick();
        CHECK(stats.lifeState == LifeState::Dead);

        stats.advanceDeathTick();  // second call
        CHECK(stats.lifeState == LifeState::Dead);
        CHECK(stats.isDead);
    }
}

TEST_CASE("respawn transitions Dead to Alive") {
    auto stats = makeWarrior(50);
    stats.takeDamage(9999);
    stats.advanceDeathTick();
    CHECK(stats.lifeState == LifeState::Dead);

    stats.respawn();

    CHECK(stats.lifeState == LifeState::Alive);
    CHECK(stats.isAlive());
    CHECK_FALSE(stats.isDying());
    CHECK_FALSE(stats.isDead);
    CHECK(stats.currentHP == stats.maxHP);
    CHECK(stats.currentMP == stats.maxMP);
}

TEST_CASE("onDied callback fires during Dying transition") {
    auto stats = makeWarrior(50);
    bool callbackFired = false;
    LifeState stateAtCallback = LifeState::Alive;

    stats.onDied = [&]() {
        callbackFired = true;
        stateAtCallback = stats.lifeState;
    };

    stats.takeDamage(9999);

    CHECK(callbackFired);
    CHECK(stateAtCallback == LifeState::Dying);
}

TEST_CASE("Dying entity cannot take further damage") {
    auto stats = makeWarrior(50);
    stats.takeDamage(9999);
    CHECK(stats.isDying());

    int dmg = stats.takeDamage(100);
    CHECK(dmg == 0);
    CHECK(stats.currentHP == 0);
    CHECK(stats.lifeState == LifeState::Dying);  // still Dying, not re-triggered
}

TEST_CASE("Dead entity cannot take damage") {
    auto stats = makeWarrior(50);
    stats.takeDamage(9999);
    stats.advanceDeathTick();
    CHECK(stats.lifeState == LifeState::Dead);

    int dmg = stats.takeDamage(100);
    CHECK(dmg == 0);
    CHECK(stats.currentHP == 0);
}

TEST_CASE("die() directly also transitions to Dying") {
    auto stats = makeWarrior(50);
    stats.die(DeathSource::PvE);

    CHECK(stats.lifeState == LifeState::Dying);
    CHECK(stats.isDying());
    CHECK_FALSE(stats.isDead);  // not Dead until advanceDeathTick
}

TEST_CASE("die() is idempotent: second call during Dying is a no-op") {
    auto stats = makeWarrior();
    stats.currentXP = 80;
    stats.die(DeathSource::PvE);
    int64_t xpAfterFirst = stats.currentXP;

    stats.die(DeathSource::PvE);
    CHECK(stats.currentXP == xpAfterFirst);
    CHECK(stats.lifeState == LifeState::Dying);
}

TEST_CASE("full lifecycle: Alive -> Dying -> Dead -> Alive") {
    auto stats = makeWarrior(100);

    // Alive
    CHECK(stats.isAlive());

    // Take lethal damage -> Dying
    stats.takeDamage(9999);
    CHECK(stats.isDying());
    CHECK_FALSE(stats.isAlive());
    CHECK_FALSE(stats.isDead);

    // Server tick advances -> Dead
    stats.advanceDeathTick();
    CHECK(stats.lifeState == LifeState::Dead);
    CHECK(stats.isDead);

    // Respawn -> Alive
    stats.respawn();
    CHECK(stats.isAlive());
    CHECK_FALSE(stats.isDead);
    CHECK(stats.currentHP == stats.maxHP);
}

} // TEST_SUITE
