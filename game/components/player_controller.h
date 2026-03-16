#pragma once
#include "engine/ecs/component.h"
#include "engine/core/types.h"

namespace fate {

// Player controller component - handles input-driven movement
// TWOM-style: cardinal only, no diagonal
struct PlayerController : public Component {
    FATE_COMPONENT(PlayerController)

    float moveSpeed = 96.0f; // pixels per second (3 tiles/sec at 32px/tile)
    Direction facing = Direction::Down;
    bool isMoving = false;
};

} // namespace fate
