#include <doctest/doctest.h>
#include "game/shared/enemy_stats.h"

using namespace fate;

TEST_SUITE("EnemyStats") {

// ============================================================================
// Initialization
// ============================================================================

TEST_CASE("Initialize sets HP correctly") {
    EnemyStats stats;
    stats.maxHP = 200;
    stats.currentHP = 0;  // start at 0 to prove initialize resets it
    stats.initialize();

    CHECK(stats.currentHP == 200);
    CHECK(stats.maxHP == 200);
    CHECK(stats.isAlive == true);
}

TEST_CASE("Initialize with scaling adjusts maxHP and baseDamage") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.baseDamage = 10;
    stats.useScaling = true;
    stats.hpPerLevel = 10.0f;
    stats.damagePerLevel = 2.0f;
    stats.level = 5;

    stats.initialize();

    // maxHP = 100 + 10*(5-1) = 140
    CHECK(stats.maxHP == 140);
    CHECK(stats.currentHP == 140);
    // baseDamage = 10 + 2*(5-1) = 18
    CHECK(stats.baseDamage == 18);
}

TEST_CASE("Initialize without scaling leaves stats unchanged") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.baseDamage = 10;
    stats.useScaling = false;
    stats.level = 10;

    stats.initialize();

    CHECK(stats.maxHP == 100);
    CHECK(stats.currentHP == 100);
    CHECK(stats.baseDamage == 10);
}

TEST_CASE("Initialize at level 1 with scaling does not scale") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.baseDamage = 10;
    stats.useScaling = true;
    stats.hpPerLevel = 10.0f;
    stats.damagePerLevel = 2.0f;
    stats.level = 1;

    stats.initialize();

    // level=1 => (level-1)=0, no scaling applied (condition is level > 1)
    CHECK(stats.maxHP == 100);
    CHECK(stats.currentHP == 100);
    CHECK(stats.baseDamage == 10);
}

// ============================================================================
// takeDamage
// ============================================================================

TEST_CASE("takeDamage reduces HP") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.takeDamage(10);
    CHECK(stats.currentHP == 90);
    CHECK(stats.isAlive == true);
}

TEST_CASE("takeDamage does not go below 0") {
    EnemyStats stats;
    stats.maxHP = 50;
    stats.initialize();

    stats.takeDamage(99999);
    CHECK(stats.currentHP == 0);
    CHECK(stats.isAlive == false);
}

TEST_CASE("takeDamage when already dead is a no-op") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.die();
    CHECK(stats.isAlive == false);
    CHECK(stats.currentHP == 0);

    stats.takeDamage(50);
    CHECK(stats.currentHP == 0);  // unchanged
    CHECK(stats.isAlive == false);
}

TEST_CASE("takeDamage with 0 does nothing") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.takeDamage(0);
    CHECK(stats.currentHP == 100);
    CHECK(stats.isAlive == true);
}

TEST_CASE("takeDamage with negative does nothing") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.takeDamage(-10);
    CHECK(stats.currentHP == 100);
    CHECK(stats.isAlive == true);
}

// ============================================================================
// takeDamageFrom / threat table
// ============================================================================

TEST_CASE("takeDamageFrom tracks threat") {
    EnemyStats stats;
    stats.maxHP = 1000;
    stats.initialize();

    stats.takeDamageFrom(1, 100);
    stats.takeDamageFrom(2, 200);

    CHECK(stats.getThreatAmount(1) == 100);
    CHECK(stats.getThreatAmount(2) == 200);
    CHECK(stats.currentHP == 700);
}

TEST_CASE("takeDamageFrom accumulates threat from same attacker") {
    EnemyStats stats;
    stats.maxHP = 1000;
    stats.initialize();

    stats.takeDamageFrom(1, 50);
    stats.takeDamageFrom(1, 30);
    stats.takeDamageFrom(1, 20);

    CHECK(stats.getThreatAmount(1) == 100);
}

TEST_CASE("getTopThreatTarget returns highest damager") {
    EnemyStats stats;
    stats.maxHP = 1000;
    stats.initialize();

    stats.takeDamageFrom(1, 100);
    stats.takeDamageFrom(2, 300);
    stats.takeDamageFrom(3, 200);

    CHECK(stats.getTopThreatTarget() == 2);
}

TEST_CASE("getTopThreatTarget returns 0 with empty table") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    CHECK(stats.getTopThreatTarget() == 0);
}

TEST_CASE("hasThreatFrom returns correct bool") {
    EnemyStats stats;
    stats.maxHP = 1000;
    stats.initialize();

    stats.takeDamageFrom(42, 10);

    CHECK(stats.hasThreatFrom(42) == true);
    CHECK(stats.hasThreatFrom(99) == false);
}

TEST_CASE("getThreatAmount for unknown entity returns 0") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    CHECK(stats.getThreatAmount(999) == 0);
}

// ============================================================================
// respawn
// ============================================================================

TEST_CASE("respawn resets HP and clears threat") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.takeDamageFrom(1, 30);
    stats.takeDamageFrom(2, 20);
    CHECK(stats.currentHP == 50);

    stats.respawn();

    CHECK(stats.currentHP == 100);
    CHECK(stats.isAlive == true);
    CHECK(stats.hasThreatFrom(1) == false);
    CHECK(stats.hasThreatFrom(2) == false);
    CHECK(stats.getTopThreatTarget() == 0);
}

TEST_CASE("respawn after death restores life") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.takeDamage(200);
    CHECK(stats.isAlive == false);
    CHECK(stats.currentHP == 0);

    stats.respawn();
    CHECK(stats.isAlive == true);
    CHECK(stats.currentHP == 100);
}

// ============================================================================
// rollDamage
// ============================================================================

TEST_CASE("rollDamage returns value in expected range") {
    EnemyStats stats;
    stats.baseDamage = 100;
    stats.useScaling = false;
    stats.initialize();

    int lo = static_cast<int>(100 * 0.8f);  // 80
    int hi = static_cast<int>(100 * 1.2f);  // 120

    for (int i = 0; i < 100; ++i) {
        int dmg = stats.rollDamage();
        CHECK(dmg >= lo);
        CHECK(dmg <= hi);
    }
}

TEST_CASE("rollDamage with scaling") {
    EnemyStats stats;
    stats.baseDamage = 50;
    stats.useScaling = true;
    stats.damagePerLevel = 5.0f;
    stats.level = 5;
    stats.initialize();

    // After initialize, baseDamage = 50 + 5*(5-1) = 70
    // But getScaledDamage also applies the formula:
    // getScaledDamage => baseDamage(70) + damagePerLevel(5)*(5-1) = 70 + 20 = 90
    // Wait - initialize already mutated baseDamage to 70, and getScaledDamage
    // checks useScaling && level>1 again, so it would be 70 + 5*(5-1) = 90
    int scaled = stats.getScaledDamage();
    int lo = static_cast<int>(scaled * 0.8f);
    int hi = static_cast<int>(scaled * 1.2f);

    for (int i = 0; i < 50; ++i) {
        int dmg = stats.rollDamage();
        CHECK(dmg >= lo);
        CHECK(dmg <= hi);
    }
}

// ============================================================================
// die
// ============================================================================

TEST_CASE("die sets isAlive false and fires callback") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    bool callbackFired = false;
    stats.onDied = [&]() { callbackFired = true; };

    stats.die();
    CHECK(stats.isAlive == false);
    CHECK(stats.currentHP == 0);
    CHECK(callbackFired == true);
}

TEST_CASE("die works without callback set") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    // No onDied callback set -- should not crash
    stats.die();
    CHECK(stats.isAlive == false);
    CHECK(stats.currentHP == 0);
}

// ============================================================================
// heal
// ============================================================================

TEST_CASE("heal restores HP clamped to maxHP") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.takeDamage(60);
    CHECK(stats.currentHP == 40);

    stats.heal(30);
    CHECK(stats.currentHP == 70);

    // Heal past max
    stats.heal(9999);
    CHECK(stats.currentHP == 100);
}

TEST_CASE("heal when dead does nothing") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.die();
    stats.heal(50);
    CHECK(stats.currentHP == 0);
    CHECK(stats.isAlive == false);
}

TEST_CASE("heal with 0 does nothing") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    stats.takeDamage(10);
    stats.heal(0);
    CHECK(stats.currentHP == 90);
}

// ============================================================================
// clearThreatTable
// ============================================================================

TEST_CASE("clearThreatTable empties all entries") {
    EnemyStats stats;
    stats.maxHP = 1000;
    stats.initialize();

    stats.takeDamageFrom(1, 10);
    stats.takeDamageFrom(2, 20);
    stats.takeDamageFrom(3, 30);

    stats.clearThreatTable();
    CHECK(stats.hasThreatFrom(1) == false);
    CHECK(stats.hasThreatFrom(2) == false);
    CHECK(stats.hasThreatFrom(3) == false);
    CHECK(stats.getTopThreatTarget() == 0);
}

// ============================================================================
// Callbacks
// ============================================================================

TEST_CASE("onDamaged callback fires with correct amount") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    int lastAmount = 0;
    stats.onDamaged = [&](int amount) { lastAmount = amount; };

    stats.takeDamage(25);
    CHECK(lastAmount == 25);
}

TEST_CASE("onRespawned callback fires") {
    EnemyStats stats;
    stats.maxHP = 100;
    stats.initialize();

    bool fired = false;
    stats.onRespawned = [&]() { fired = true; };

    stats.respawn();
    CHECK(fired == true);
}

TEST_CASE("onProvokedByPlayer fires for passive mob") {
    EnemyStats stats;
    stats.maxHP = 1000;
    stats.isAggressive = false;
    stats.initialize();

    uint32_t provoker = 0;
    stats.onProvokedByPlayer = [&](uint32_t id) { provoker = id; };

    stats.takeDamageFrom(42, 10);
    CHECK(provoker == 42);
}

TEST_CASE("onProvokedByPlayer does not fire for aggressive mob") {
    EnemyStats stats;
    stats.maxHP = 1000;
    stats.isAggressive = true;
    stats.initialize();

    bool fired = false;
    stats.onProvokedByPlayer = [&](uint32_t) { fired = true; };

    stats.takeDamageFrom(42, 10);
    CHECK(fired == false);
}

// ============================================================================
// getMobHitRate
// ============================================================================

TEST_CASE("getMobHitRate returns configured value") {
    EnemyStats stats;
    stats.mobHitRate = 16;
    CHECK(stats.getMobHitRate() == 16);
}

} // TEST_SUITE
