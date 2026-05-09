/**************************************************************************/
/*  mob2d.h                                                               */
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
// engine/gameplay2d/components/mob2d.h
//
// Demo-safe mob brain. Single-state machine — Idle → Chase → Attack → Return →
// Idle. Numbers are pixel radii; the Mob2DSystem ticks the state, picks the
// nearest hostile target, and writes CharacterController2D.lastInputVec_ so
// the existing movement system pushes the mob toward the target.
//
// This is intentionally far less capable than the proprietary MobAIComponent
// (no fear, leash penalties, escape lanes, telegraph integration). Authors
// who want those behaviors should bring their own AI system.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/ecs/entity_handle.h"
#include "engine/core/types.h"
#include <cstdint>

namespace fate {

enum class Mob2DState : uint8_t {
    Idle    = 0,
    Chase   = 1,
    Attack  = 2,
    Return  = 3,
    Dead    = 4,
};

struct Mob2D {
    FATE_COMPONENT(Mob2D)
    // NOTE: `enabled` is provided by FATE_COMPONENT — do not redeclare it here.

    // Authoring
    float aggroRadiusPx   = 192.0f;     // distance to acquire target
    float leashRadiusPx   = 320.0f;     // distance from spawn before returning
    float attackRangePx   = 48.0f;
    float chaseSpeed      = 64.0f;
    float returnSpeed     = 96.0f;

    // Runtime — system-owned.
    Mob2DState   state         = Mob2DState::Idle;
    Vec2         spawnPosition = {0.0f, 0.0f};
    bool         hasSpawnAnchor = false;
    EntityHandle currentTarget_;
    float        stateTimer_ = 0.0f;
};

} // namespace fate

FATE_REFLECT(fate::Mob2D,
    FATE_FIELD(aggroRadiusPx, Float),
    FATE_FIELD(leashRadiusPx, Float),
    FATE_FIELD(attackRangePx, Float),
    FATE_FIELD(chaseSpeed, Float),
    FATE_FIELD(returnSpeed, Float),
    FATE_FIELD(state, Enum)
)
