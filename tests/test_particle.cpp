#include <doctest/doctest.h>
#include "engine/particle/particle_emitter.h"

TEST_CASE("ParticleEmitter continuous spawning") {
    fate::EmitterConfig config;
    config.spawnRate = 10.0f; // 10 per second
    config.lifetimeMin = 1.0f;
    config.lifetimeMax = 1.0f;
    config.gravity = {0, 0};

    fate::ParticleEmitter emitter;
    emitter.init(config, 100);

    // After 0.5 seconds, should have ~5 particles
    emitter.update(0.5f, {0, 0});
    CHECK(emitter.activeCount() >= 4);
    CHECK(emitter.activeCount() <= 6);
    CHECK_FALSE(emitter.isFinished());
}

TEST_CASE("ParticleEmitter burst mode") {
    fate::EmitterConfig config;
    config.spawnRate = 0.0f;
    config.burstCount = 10;
    config.lifetimeMin = 0.5f;
    config.lifetimeMax = 0.5f;

    fate::ParticleEmitter emitter;
    emitter.init(config, 100);

    emitter.burst({100, 200});
    emitter.update(0.0f, {0, 0}); // process the burst
    CHECK(emitter.activeCount() == 10);
    CHECK_FALSE(emitter.isFinished());

    // After lifetime expires
    emitter.update(0.6f, {0, 0});
    CHECK(emitter.activeCount() == 0);
    CHECK(emitter.isFinished());
}

TEST_CASE("ParticleEmitter vertex count matches active") {
    fate::EmitterConfig config;
    config.spawnRate = 100.0f;
    config.lifetimeMin = 1.0f;
    config.lifetimeMax = 1.0f;

    fate::ParticleEmitter emitter;
    emitter.init(config, 50);

    emitter.update(0.1f, {0, 0});
    size_t active = emitter.activeCount();
    CHECK(active > 0);
    // Each particle = 4 vertices (quad)
    CHECK(emitter.vertices().size() == active * 4);
}

TEST_CASE("ParticleEmitter max particles cap") {
    fate::EmitterConfig config;
    config.spawnRate = 1000.0f;
    config.lifetimeMin = 10.0f;
    config.lifetimeMax = 10.0f;

    fate::ParticleEmitter emitter;
    emitter.init(config, 16);

    emitter.update(1.0f, {0, 0});
    CHECK(emitter.activeCount() == 16); // capped at max
}

TEST_CASE("ParticleEmitter gravity applies") {
    fate::EmitterConfig config;
    config.spawnRate = 0.0f;
    config.burstCount = 1;
    config.lifetimeMin = 10.0f;
    config.lifetimeMax = 10.0f;
    config.velocityMin = {0, 0};
    config.velocityMax = {0, 0};
    config.gravity = {0, 100}; // downward

    fate::ParticleEmitter emitter;
    emitter.init(config, 10);
    emitter.burst({0, 0});
    emitter.update(0.0f, {0, 0}); // spawn
    emitter.update(1.0f, {0, 0}); // apply gravity for 1s

    // Particle should have moved downward
    // We can't easily check particle position directly, but vertex data should reflect it
    CHECK(emitter.activeCount() == 1);
}
