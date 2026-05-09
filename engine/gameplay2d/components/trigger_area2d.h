/**************************************************************************/
/*  trigger_area2d.h                                                      */
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
// engine/gameplay2d/components/trigger_area2d.h
//
// Lightweight overlap zone for the open-source demo. Functionally equivalent
// to a Collider2D with isTrigger=true, but kept as a distinct component so
// authors can mark "this entity is purely an event source" without reasoning
// about collision layers / static flags.
//
// The Trigger2DSystem walks pairs of (TriggerArea2D, Collider2D) overlaps and
// flips wasOverlapping_ each frame. Consumers poll isOverlapping() / didEnter()
// / didExit() during their update; no callback indirection is required, which
// keeps the demo dependency-free and easy to step through in a debugger.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"
#include "engine/gameplay2d/components/collider2d.h"

namespace fate {

struct TriggerArea2D {
    FATE_COMPONENT(TriggerArea2D)

    Collider2DShape shape = Collider2DShape::Box;
    Vec2 offset = {0.0f, 0.0f};
    Vec2 size   = {64.0f, 64.0f};    // Box: w/h; Circle: size.x = radius
    uint32_t layer = 1u;
    uint32_t mask  = 0xFFFFFFFFu;
    Color debugColor = {0.9f, 0.7f, 0.2f, 0.35f};

    // Frame state — written by Trigger2DSystem, polled by consumers.
    bool isOverlapping  = false;
    bool wasOverlapping = false;

    bool didEnter() const { return isOverlapping && !wasOverlapping; }
    bool didExit()  const { return !isOverlapping && wasOverlapping; }

    Rect getBounds(const Vec2& entityPos) const {
        Vec2 center = entityPos + offset;
        if (shape == Collider2DShape::Circle) {
            float r = size.x;
            return { center.x - r, center.y - r, r * 2.0f, r * 2.0f };
        }
        return { center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y };
    }
};

} // namespace fate

FATE_REFLECT(fate::TriggerArea2D,
    FATE_FIELD(shape, Enum),
    FATE_FIELD(offset, Vec2),
    FATE_FIELD(size, Vec2),
    FATE_FIELD(layer, UInt),
    FATE_FIELD(mask, UInt),
    FATE_FIELD(debugColor, Color)
)
