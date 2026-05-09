/**************************************************************************/
/*  interest_source.h                                                     */
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
// engine/gameplay2d/components/interest_source.h
//
// AOI hint for a future networking layer — "this entity emits awareness of
// itself within a radius". The demo doesn't ship an interest manager (that's
// the proprietary FateServer's job), but exposing the data shape here lets a
// downstream user write one without inventing a new component name.
//
// category is a bitmask matched against the interest layer of any future
// observer; 0xFFFFFFFF = always relevant, 0 = never broadcast.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <cstdint>

namespace fate {

struct InterestSource {
    FATE_COMPONENT_COLD(InterestSource)

    float    interestRadiusPx = 640.0f;
    uint32_t category         = 0xFFFFFFFFu;
    int      tier             = 0;        // 0=hot, 1=warm, 2=cold (sender hint)
    bool     alwaysRelevant   = false;
};

} // namespace fate

FATE_REFLECT(fate::InterestSource,
    FATE_FIELD(interestRadiusPx, Float),
    FATE_FIELD(category, UInt),
    FATE_FIELD(tier, Int),
    FATE_FIELD(alwaysRelevant, Bool)
)
