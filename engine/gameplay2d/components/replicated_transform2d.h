/**************************************************************************/
/*  replicated_transform2d.h                                              */
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
// engine/gameplay2d/components/replicated_transform2d.h
//
// Snapshot of the last replicated state for a server-authoritative entity.
// The CharacterController2DSystem skips local input on entities whose
// CharacterController2D::authority == ServerAuthoritative; instead it
// interpolates Transform toward (targetPosition, targetFacing) at
// interpRate per second.
//
// Demo-safe: the engine doesn't ship a network reader for this — the user's
// integration code writes targetPosition/targetFacing when packets arrive.
// This decouples the smoothing logic from any specific protocol.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"

namespace fate {

struct ReplicatedTransform2D {
    FATE_COMPONENT(ReplicatedTransform2D)

    Vec2      targetPosition = {0.0f, 0.0f};
    Direction targetFacing   = Direction::Down;
    float     interpRate     = 12.0f;             // higher = snappier
    float     relevanceRadiusPx = 1024.0f;        // hint for AOI; not enforced here
    float     updateIntervalSec = 0.05f;          // hint for senders
    float     timeSinceLastUpdate_ = 0.0f;        // runtime
    bool      hasReceivedSnapshot = false;
};

} // namespace fate

FATE_REFLECT(fate::ReplicatedTransform2D,
    FATE_FIELD(targetPosition, Vec2),
    FATE_FIELD(targetFacing, Direction),
    FATE_FIELD(interpRate, Float),
    FATE_FIELD(relevanceRadiusPx, Float),
    FATE_FIELD(updateIntervalSec, Float)
)
