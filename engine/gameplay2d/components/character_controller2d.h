/**************************************************************************/
/*  character_controller2d.h                                              */
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
// engine/gameplay2d/components/character_controller2d.h
//
// Cardinal/touch-friendly MMO movement controller for the open-source demo.
// NOT a platformer body — there's no gravity, jump, or air control. The
// controller drives Transform.position based on the active input vector and
// the configured moveSpeed.
//
// Authority modes:
//   Local            — input drives the Transform every frame (single-player demo).
//   ServerAuthoritative — input is sent to the server; the Transform is replaced
//                         by ReplicatedTransform2D. This component still stores
//                         facing/animation state for prediction smoothing.
//   AISimulated      — driven by Mob2DSystem or another AI source; no input read.
//
// Only ONE entity per scene should have isLocalPlayer=true. The demo's Camera
// FollowSystem and Interaction2DSystem both anchor on that entity.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"
#include <cstdint>

namespace fate {

enum class MovementAuthority : uint8_t {
    Local              = 0,
    ServerAuthoritative = 1,
    AISimulated        = 2,
};

struct CharacterController2D {
    FATE_COMPONENT(CharacterController2D)

    float moveSpeed       = 96.0f;             // pixels per second
    Direction facing      = Direction::Down;
    bool isMoving         = false;
    bool isLocalPlayer    = false;             // exactly one entity at a time
    bool inputEnabled     = true;              // set false when chat is open, etc.
    bool collisionEnabled = true;              // disable for ghost/spectate mode
    MovementAuthority authority = MovementAuthority::Local;

    // Smoothing: walk animation continues briefly after input release so rapid
    // taps don't reset the cycle. Read by SpriteAnimator2DSystem.
    float animCoastSeconds = 0.12f;
    float animCoastTimer_  = 0.0f;             // runtime, not authored

    // Last computed normalized input vector (set by CharacterController2DSystem).
    // Other systems can read this for facing/blending without re-reading Input.
    Vec2 lastInputVec_ = {0.0f, 0.0f};
};

} // namespace fate

FATE_REFLECT(fate::CharacterController2D,
    FATE_FIELD(moveSpeed, Float),
    FATE_FIELD(facing, Direction),
    FATE_FIELD(isMoving, Bool),
    FATE_FIELD(isLocalPlayer, Bool),
    FATE_FIELD(inputEnabled, Bool),
    FATE_FIELD(collisionEnabled, Bool),
    FATE_FIELD(authority, Enum),
    FATE_FIELD(animCoastSeconds, Float)
)
