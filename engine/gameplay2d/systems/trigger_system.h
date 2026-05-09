/**************************************************************************/
/*  trigger_system.h                                                      */
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
// engine/gameplay2d/systems/trigger_system.h
//
// Walks every TriggerArea2D and tests it against every Collider2D whose layer
// intersects the trigger's mask. Writes wasOverlapping_=isOverlapping at the
// start of each tick, then computes the new isOverlapping value, so consumer
// code that polls didEnter()/didExit() during update() sees correct edges.
//
// Cost is O(triggers * colliders) — fine for the demo (low entity counts).
// Production users with thousands of overlap pairs should swap to a spatial
// hash query; the SpatialHash in engine/spatial/spatial_hash.h already exists
// for that.

#pragma once

#include "engine/ecs/world.h"

namespace fate {

class Trigger2DSystem : public System {
public:
    const char* name() const override { return "Trigger2DSystem"; }
    void update(float dt) override;
};

} // namespace fate
