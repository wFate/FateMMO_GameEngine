/**************************************************************************/
/*  nameplate.h                                                           */
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
// engine/gameplay2d/components/nameplate.h
//
// Public floating label drawn above an entity. Carries optional level + HP
// bar config so the NameplateRenderSystem can render the common MMO trio
// (name, level, HP bar) from one component.
//
// All sizes are in world pixels; the render system positions the label at
// (entityPos + worldOffset) and draws the rect/text directly without going
// through the proprietary UI manager — keeps the demo render path standalone.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"
#include <string>

namespace fate {

struct Nameplate {
    FATE_COMPONENT(Nameplate)

    std::string displayName;
    int   level         = 0;
    bool  visible       = true;
    bool  showLevel     = true;
    bool  showHealthBar = true;            // requires Health component to render

    Color textColor       = {1.0f, 1.0f, 1.0f, 1.0f};
    Color levelColor      = {0.7f, 0.85f, 1.0f, 1.0f};
    Color healthBarColor  = {0.85f, 0.2f, 0.2f, 1.0f};
    Color backgroundColor = {0.0f, 0.0f, 0.0f, 0.55f};

    Vec2  worldOffset = {0.0f, 24.0f};     // pixels above the entity origin
    Vec2  size        = {64.0f, 16.0f};    // pixel size of the plate
};

} // namespace fate

FATE_REFLECT(fate::Nameplate,
    FATE_FIELD(displayName, String),
    FATE_FIELD(level, Int),
    FATE_FIELD(visible, Bool),
    FATE_FIELD(showLevel, Bool),
    FATE_FIELD(showHealthBar, Bool),
    FATE_FIELD(textColor, Color),
    FATE_FIELD(levelColor, Color),
    FATE_FIELD(healthBarColor, Color),
    FATE_FIELD(backgroundColor, Color),
    FATE_FIELD(worldOffset, Vec2),
    FATE_FIELD(size, Vec2)
)
