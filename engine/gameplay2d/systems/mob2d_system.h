/**************************************************************************/
/*  mob2d_system.h                                                        */
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
// engine/gameplay2d/systems/mob2d_system.h
//
// Demo mob brain. For every Mob2D + CharacterController2D pair, runs a tiny
// state machine:
//
//   Idle   -> Chase  if hostile target enters aggroRadiusPx
//   Chase  -> Attack if within attackRangePx
//          -> Return if target died, leashed past leashRadiusPx, or lost
//   Attack -> Chase  while target lives + in range; sets Attack.wantsToAttack_
//   Return -> Idle   when within 8px of spawnPosition
//
// Movement is implemented by writing CharacterController2D::lastInputVec_ +
// flipping the controller authority to AISimulated. The existing
// CharacterController2DSystem then performs the actual collision-aware move.
//
// Targets are tracked via EntityHandle so a destroyed-and-recycled slot can
// be detected and ignored.

#pragma once

#include "engine/ecs/world.h"

namespace fate {

class Mob2DSystem : public System {
public:
    const char* name() const override { return "Mob2DSystem"; }
    void update(float dt) override;
};

} // namespace fate
