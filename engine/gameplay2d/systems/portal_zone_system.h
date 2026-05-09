/**************************************************************************/
/*  portal_zone_system.h                                                  */
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
// engine/gameplay2d/systems/portal_zone_system.h
//
// Two responsibilities:
//   1. Track which Zone2D the local player currently occupies. Last-known zone
//      is exposed via currentZoneId() / currentZoneName(); on transitions the
//      onZoneChanged callback fires.
//   2. Detect when the local player's bounding box overlaps a Portal2D and
//      either invoke onPortalEnter (always) or, if the portal is same-scene
//      and no callback is installed, teleport the player to targetSpawnPos.
//
// Portals fire on the leading edge — re-entering the same portal without
// first leaving its trigger does not re-fire.

#pragma once

#include "engine/ecs/world.h"
#include <functional>
#include <string>

namespace fate {

class Entity;
struct Portal2D;
struct Zone2D;

class PortalZoneSystem : public System {
public:
    const char* name() const override { return "PortalZoneSystem"; }
    void update(float dt) override;

    using PortalCallback = std::function<void(Entity* player, Entity* portal, Portal2D*)>;
    void setOnPortalEnter(PortalCallback cb) { onPortalEnter_ = std::move(cb); }

    using ZoneCallback = std::function<void(Entity* player, Entity* zoneEntity, Zone2D*)>;
    void setOnZoneChanged(ZoneCallback cb) { onZoneChanged_ = std::move(cb); }

    const std::string& currentZoneId()   const { return currentZoneId_; }
    const std::string& currentZoneName() const { return currentZoneName_; }

private:
    PortalCallback onPortalEnter_;
    ZoneCallback   onZoneChanged_;
    std::string    currentZoneId_;
    std::string    currentZoneName_;
};

} // namespace fate
