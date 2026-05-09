/**************************************************************************/
/*  portal2d.h                                                            */
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
// engine/gameplay2d/components/portal2d.h
//
// Walks-into trigger that calls the host's transition callback. The
// PortalZoneSystem detects player overlap and emits one event per crossing
// (re-entering the same portal without first leaving it does NOT re-fire).
//
// targetScene empty = same-scene zone hop. targetSpawnPos is in world pixels.
// Distinct from any proprietary PortalComponent so both can coexist.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"
#include <string>

namespace fate {

struct Portal2D {
    FATE_COMPONENT(Portal2D)

    std::string targetScene;                    // empty = same scene
    std::string targetZoneId;                   // optional zone label after move
    Vec2        targetSpawnPos = {0.0f, 0.0f};
    Vec2        triggerSize    = {32.0f, 32.0f};
    bool        showLabel      = true;
    std::string label;                          // overrides targetZoneId for UI
    bool        useFadeTransition = false;
    float       fadeDurationSec   = 0.4f;

    // Runtime — set/cleared by PortalZoneSystem.
    bool playerInside_ = false;
    bool firedThisFrame_ = false;

    Rect getTriggerBounds(const Vec2& entityPos) const {
        return { entityPos.x - triggerSize.x * 0.5f,
                 entityPos.y - triggerSize.y * 0.5f,
                 triggerSize.x, triggerSize.y };
    }
};

} // namespace fate

FATE_REFLECT(fate::Portal2D,
    FATE_FIELD(targetScene, String),
    FATE_FIELD(targetZoneId, String),
    FATE_FIELD(targetSpawnPos, Vec2),
    FATE_FIELD(triggerSize, Vec2),
    FATE_FIELD(showLabel, Bool),
    FATE_FIELD(label, String),
    FATE_FIELD(useFadeTransition, Bool),
    FATE_FIELD(fadeDurationSec, Float)
)
