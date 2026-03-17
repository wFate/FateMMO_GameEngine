#pragma once
#include "engine/ecs/component.h"
#include "engine/core/types.h"

namespace fate {

// Transform component - every visible entity needs this
struct Transform : public Component {
    FATE_LEGACY_COMPONENT(Transform)

    Vec2 position;
    Vec2 scale = Vec2::one();
    float rotation = 0.0f;  // radians
    float depth = 0.0f;     // z-order for rendering (higher = on top)

    Transform() = default;
    Transform(float x, float y) : position(x, y) {}
    Transform(Vec2 pos) : position(pos) {}
};

} // namespace fate
