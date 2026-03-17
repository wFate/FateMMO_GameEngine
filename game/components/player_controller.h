#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/core/types.h"

namespace fate {

// Player controller component - handles movement
// isLocalPlayer = true means this entity responds to keyboard/touch input
// isLocalPlayer = false means this entity is controlled by server/AI (other players, NPCs)
// Prefab templates should save isLocalPlayer = false — it gets set at runtime
struct PlayerController {
    FATE_COMPONENT(PlayerController)

    float moveSpeed = 96.0f; // pixels per second (3 tiles/sec at 32px/tile)
    Direction facing = Direction::Down;
    bool isMoving = false;
    bool isLocalPlayer = false; // only ONE entity should have this true
};

} // namespace fate
