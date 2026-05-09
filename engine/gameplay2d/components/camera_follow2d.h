/**************************************************************************/
/*  camera_follow2d.h                                                     */
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
// engine/gameplay2d/components/camera_follow2d.h
//
// Marker + smoothing config for "the camera should track this entity". The
// CameraFollow2DSystem picks the highest-priority enabled instance whose
// Transform exists and writes the App's Camera position from it.
//
// Two follow modes:
//   Snap  — camera position = entity position every frame (TWOM-style).
//   Smooth — camera lerps toward the entity using lerpRate (per-second).
//
// Deadzone is in screen pixels; movement within the deadzone is ignored. Set
// width/height to 0 to disable the deadzone.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"
#include <cstdint>

namespace fate {

enum class CameraFollowMode : uint8_t {
    Snap   = 0,
    Smooth = 1,
};

struct CameraFollow2D {
    FATE_COMPONENT(CameraFollow2D)

    CameraFollowMode mode = CameraFollowMode::Smooth;
    float lerpRate        = 8.0f;        // higher = snappier
    Vec2  offset          = {0.0f, 0.0f};
    Vec2  deadzone        = {0.0f, 0.0f};
    int   priority        = 0;           // higher wins when multiple are enabled
};

} // namespace fate

FATE_REFLECT(fate::CameraFollow2D,
    FATE_FIELD(mode, Enum),
    FATE_FIELD(lerpRate, Float),
    FATE_FIELD(offset, Vec2),
    FATE_FIELD(deadzone, Vec2),
    FATE_FIELD(priority, Int)
)
