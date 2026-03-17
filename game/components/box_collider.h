#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"

namespace fate {

// Axis-Aligned Bounding Box collider
// offset is relative to the entity's Transform position (center-based)
// size is the full width/height of the collision box
struct BoxCollider {
    FATE_COMPONENT(BoxCollider)

    Vec2 offset;                   // offset from transform center
    Vec2 size = {32.0f, 32.0f};   // collision box size
    bool isTrigger = false;        // triggers don't block movement, just detect overlap
    bool isStatic = true;          // static colliders don't move (walls, trees)

    // Get world-space collision rect given entity position
    Rect getBounds(const Vec2& entityPos) const {
        return {
            entityPos.x + offset.x - size.x * 0.5f,
            entityPos.y + offset.y - size.y * 0.5f,
            size.x,
            size.y
        };
    }
};

} // namespace fate
