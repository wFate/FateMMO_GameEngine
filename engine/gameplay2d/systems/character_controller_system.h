/**************************************************************************/
/*  character_controller_system.h                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                          FateMMO Game Engine                           */
/*                       https://www.FateMMO.com                          */
/**************************************************************************/
/* Copyright (c) 2026-present FateMMO Game Engine contributors.           */
/* Copyright (c) 2026-present Caleb Kious.                                */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/
// engine/gameplay2d/systems/character_controller_system.h
//
// Walks every CharacterController2D entity that is marked Local + isMoving and
// shifts Transform.position by inputVec * speed * dt. Server-authoritative
// instances are skipped here — see ReplicatedTransformInterpolatorSystem.
//
// Tile-grid collision is opt-in via setCollisionGrid(); when set, the system
// sweeps the player's bounding box (taken from Collider2D, falling back to a
// 28-pixel default) and rejects movement on the X then Y axes independently
// so the player slides along walls rather than catching corners. AABB-vs-
// static-Collider2D checks happen in the same pass so authored walls work
// even without a tile grid.

#pragma once

#include "engine/ecs/world.h"
#include "engine/spatial/collision_grid.h"

namespace fate {

class CharacterController2DSystem : public System {
public:
    const char* name() const override { return "CharacterController2DSystem"; }

    void update(float dt) override;

    // Optional: set a CollisionGrid so the controller can clip against tiles.
    void setCollisionGrid(const CollisionGrid* grid) { grid_ = grid; }

private:
    const CollisionGrid* grid_ = nullptr;
};

} // namespace fate
