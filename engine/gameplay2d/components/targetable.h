/**************************************************************************/
/*  targetable.h                                                          */
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
// engine/gameplay2d/components/targetable.h
//
// Marks an entity as a candidate for the player's "select target" gesture.
// category is a small enum the Targeting2DSystem uses to resolve "what does
// the player want to click on?" — friendly NPCs lose to hostile mobs when
// they overlap on-screen, etc. Priority is a tiebreaker within a category.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <cstdint>

namespace fate {

enum class TargetCategory : uint8_t {
    Neutral = 0,
    Friendly = 1,
    Hostile = 2,
    Interactable = 3,
};

struct Targetable {
    FATE_COMPONENT(Targetable)

    TargetCategory category   = TargetCategory::Neutral;
    float          radius     = 24.0f;     // pixel radius for click/tap selection
    int            priority   = 0;
    bool           selectable = true;
    bool           canTargetSelf = false;
};

} // namespace fate

FATE_REFLECT(fate::Targetable,
    FATE_FIELD(category, Enum),
    FATE_FIELD(radius, Float),
    FATE_FIELD(priority, Int),
    FATE_FIELD(selectable, Bool),
    FATE_FIELD(canTargetSelf, Bool)
)
