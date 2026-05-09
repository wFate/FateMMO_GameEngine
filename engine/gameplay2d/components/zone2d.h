/**************************************************************************/
/*  zone2d.h                                                              */
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
// engine/gameplay2d/components/zone2d.h
//
// Demo-safe named region inside a scene. Used for: HUD zone-name display, PvP
// flagging, level-range gates, ambient music selection. The PortalZoneSystem
// watches the player's Transform and updates currentZoneId_ whenever they
// cross a zone boundary.
//
// pvpEnabled here is purely a flag — the demo does not implement PvP combat
// rules. Distinct type-name from any proprietary game/ ZoneComponent so both
// can coexist when the full repo is checked out.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"
#include <string>

namespace fate {

struct Zone2D {
    FATE_COMPONENT(Zone2D)

    std::string zoneId;                         // stable key (e.g. "town_market")
    std::string displayName;                    // shown to player on entry
    std::string zoneType   = "zone";            // "town", "zone", "dungeon"
    Vec2        size       = {640.0f, 360.0f}; // axis-aligned bounds, center-origin
    int         minLevel   = 1;
    int         maxLevel   = 99;
    bool        pvpEnabled = false;

    Rect getBounds(const Vec2& entityPos) const {
        return { entityPos.x - size.x * 0.5f, entityPos.y - size.y * 0.5f,
                 size.x, size.y };
    }

    bool contains(const Vec2& zoneCenter, const Vec2& point) const {
        return getBounds(zoneCenter).contains(point);
    }
};

} // namespace fate

FATE_REFLECT(fate::Zone2D,
    FATE_FIELD(zoneId, String),
    FATE_FIELD(displayName, String),
    FATE_FIELD(zoneType, String),
    FATE_FIELD(size, Vec2),
    FATE_FIELD(minLevel, Int),
    FATE_FIELD(maxLevel, Int),
    FATE_FIELD(pvpEnabled, Bool)
)
