#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"
#include "engine/ecs/reflect.h"

namespace fate {

// Transform component - every visible entity needs this
struct Transform {
    FATE_COMPONENT(Transform)

    Vec2 position;
    Vec2 scale = Vec2::one();
    float rotation = 0.0f;  // radians
    float depth = 0.0f;     // z-order for rendering (higher = on top)

    Transform() = default;
    Transform(float x, float y) : position(x, y) {}
    Transform(Vec2 pos) : position(pos) {}
};

} // namespace fate

FATE_REFLECT(fate::Transform,
    FATE_FIELD(position, Vec2),
    FATE_FIELD(rotation, Float),
    FATE_FIELD(depth, Float),
    FATE_FIELD(scale, Vec2)
)
