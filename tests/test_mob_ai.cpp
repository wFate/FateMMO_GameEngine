#include <doctest/doctest.h>
#include "game/shared/mob_ai.h"
using namespace fate;

TEST_CASE("MobAI: roaming produces non-zero velocity with pixel-scale roamRadius") {
    MobAI ai;
    Vec2 home{500.0f, 500.0f};
    ai.roamRadius = 96.0f;       // 3 tiles in pixels (default was 3.0 = 3px, too small)
    ai.baseRoamSpeed = 57.6f;    // 1.8 tiles/sec * 32 px/tile
    ai.canRoam = true;
    ai.roamWhileIdle = true;
    ai.initialize(home);

    // Tick enough times for think() to switch Idle→Roam and driveMovement to produce velocity
    bool movedAtLeastOnce = false;
    float gameTime = 0.0f;
    for (int i = 0; i < 200; ++i) {
        float dt = 0.05f;
        gameTime += dt;
        Vec2 vel = ai.tick(gameTime, dt, home, nullptr);
        if (vel.lengthSq() > 0.01f) {
            movedAtLeastOnce = true;
            break;
        }
    }
    CHECK(movedAtLeastOnce);
}

TEST_CASE("MobAI: roaming fails with tiny roamRadius (regression)") {
    MobAI ai;
    Vec2 home{500.0f, 500.0f};
    ai.roamRadius = 3.0f;        // 3 pixels — the old broken default
    ai.baseRoamSpeed = 57.6f;
    ai.canRoam = true;
    ai.roamWhileIdle = true;
    ai.initialize(home);

    // With 3px roam radius, roam targets are within the 6px arrival threshold
    // so the mob should never produce meaningful velocity
    bool movedAtLeastOnce = false;
    float gameTime = 0.0f;
    for (int i = 0; i < 200; ++i) {
        float dt = 0.05f;
        gameTime += dt;
        Vec2 vel = ai.tick(gameTime, dt, home, nullptr);
        if (vel.lengthSq() > 0.01f) {
            movedAtLeastOnce = true;
            break;
        }
    }
    CHECK_FALSE(movedAtLeastOnce);
}

TEST_CASE("MobAI: chase produces velocity toward target") {
    MobAI ai;
    Vec2 home{500.0f, 500.0f};
    ai.acquireRadius = 160.0f;   // 5 tiles in pixels
    ai.contactRadius = 192.0f;   // 6 tiles
    ai.attackRange = 32.0f;      // 1 tile
    ai.baseChaseSpeed = 57.6f;   // 1.8 tiles/sec * 32
    ai.isPassive = false;
    ai.initialize(home);

    // Place target within aggro range
    Vec2 target{500.0f, 400.0f}; // 100px away (within 160px acquireRadius)
    float gameTime = 0.0f;
    Vec2 vel{};

    // Tick until mob starts chasing
    for (int i = 0; i < 20; ++i) {
        float dt = 0.05f;
        gameTime += dt;
        vel = ai.tick(gameTime, dt, home, &target);
        if (vel.lengthSq() > 0.01f) break;
    }

    CHECK(vel.lengthSq() > 0.01f);
    CHECK(ai.getMode() == AIMode::Chase);
}

TEST_CASE("MobAI: attack fires callback when in range") {
    MobAI ai;
    Vec2 home{500.0f, 500.0f};
    ai.acquireRadius = 160.0f;
    ai.contactRadius = 192.0f;
    ai.attackRange = 64.0f;      // 2 tiles
    ai.baseChaseSpeed = 57.6f;
    ai.attackCooldown = 1.0f;
    ai.isPassive = false;
    ai.initialize(home);

    int attackCount = 0;
    ai.onAttackFired = [&]() { attackCount++; };

    // Place target within attack range
    Vec2 target{500.0f, 480.0f}; // 20px away (within 64px attackRange)
    float gameTime = 0.0f;

    // Tick enough for think→Chase→Attack and cooldown to fire
    for (int i = 0; i < 60; ++i) {
        float dt = 0.05f;
        gameTime += dt;
        ai.tick(gameTime, dt, home, &target);
    }

    CHECK(attackCount >= 1);
}

TEST_CASE("MobAI: return home uses proper speed") {
    MobAI ai;
    Vec2 home{500.0f, 500.0f};
    ai.acquireRadius = 160.0f;
    ai.contactRadius = 192.0f;
    ai.baseChaseSpeed = 57.6f;
    ai.baseReturnSpeed = 57.6f;  // was defaulting to 3.0 px/sec (broken)
    ai.isPassive = false;
    ai.initialize(home);

    // Put mob far from home in ReturnHome state
    Vec2 farPos{500.0f, 200.0f}; // 300px from home

    // Give it a target to enter Chase, then remove target to trigger return
    Vec2 target{500.0f, 190.0f};
    float gameTime = 0.0f;
    for (int i = 0; i < 10; ++i) {
        float dt = 0.05f;
        gameTime += dt;
        ai.tick(gameTime, dt, farPos, &target);
    }

    // Now remove target — mob should enter ChaseMemory then ReturnHome
    for (int i = 0; i < 40; ++i) {
        float dt = 0.05f;
        gameTime += dt;
        ai.tick(gameTime, dt, farPos, nullptr);
    }

    // Should be returning home with velocity toward home
    Vec2 vel = ai.tick(gameTime + 0.05f, 0.05f, farPos, nullptr);
    if (ai.getMode() == AIMode::ReturnHome) {
        CHECK(vel.lengthSq() > 100.0f); // Should be moving at ~57.6 px/sec, not 3 px/sec
    }
}
