#pragma once
#include "engine/core/types.h"
#include "engine/asset/asset_handle.h"

namespace fate {

struct Particle {
    Vec2 position;
    Vec2 velocity;
    Color color;
    Color colorEnd;
    float size;
    float sizeEnd;
    float life;
    float maxLife;
    float rotation;
    float rotationSpeed;
};

struct EmitterConfig {
    float spawnRate = 10.0f;
    int burstCount = 0;

    Vec2 velocityMin = {-20, -50};
    Vec2 velocityMax = {20, -10};
    float lifetimeMin = 0.5f;
    float lifetimeMax = 1.5f;
    float sizeMin = 4.0f;
    float sizeMax = 8.0f;
    float rotationSpeedMin = 0.0f;
    float rotationSpeedMax = 0.0f;

    Color colorStart = Color::white();
    Color colorEnd = {1, 1, 1, 0};

    Vec2 gravity = {0, 0};

    AssetHandle texture;
    float depth = 5.0f;
    bool worldSpace = true;
    bool additiveBlend = false;
};

} // namespace fate
