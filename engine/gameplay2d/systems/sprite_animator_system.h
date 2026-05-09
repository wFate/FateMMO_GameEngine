/**************************************************************************/
/*  sprite_animator_system.h                                              */
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
// engine/gameplay2d/systems/sprite_animator_system.h
//
// Advances every SpriteAnimator2D timer and writes the resolved frame into
// the entity's SpriteComponent (currentFrame + updateSourceRect()). Fires
// hitFrame once per loop and auto-plays returnAnimation when a non-loop clip
// reaches its last frame.
//
// Walk/idle blending is intentionally kept in the demo's authored clips —
// there's no built-in state machine here. CharacterController2D.isMoving and
// .animCoastTimer_ together drive which clip name to play; the demo's update
// is the single owner of that decision.

#pragma once

#include "engine/ecs/world.h"

namespace fate {

class SpriteAnimator2DSystem : public System {
public:
    const char* name() const override { return "SpriteAnimator2DSystem"; }
    void update(float dt) override;
};

} // namespace fate
