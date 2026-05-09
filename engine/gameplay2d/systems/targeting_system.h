/**************************************************************************/
/*  targeting_system.h                                                    */
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
// engine/gameplay2d/systems/targeting_system.h
//
// Mouse/touch target selection. On a fresh click/tap, the system converts the
// screen coordinate to world space, walks every Targetable, and selects the
// nearest selectable hit (with category preference: Hostile > Interactable >
// Friendly > Neutral). The current selection is exposed via selectedEntity().
//
// The Camera pointer is required to project the click — set it once in the
// demo's onInit. Without it the system silently no-ops.

#pragma once

#include "engine/ecs/world.h"
#include "engine/ecs/entity_handle.h"
#include <functional>

namespace fate {

class Camera;
class Entity;
struct Targetable;

class Targeting2DSystem : public System {
public:
    const char* name() const override { return "Targeting2DSystem"; }
    void update(float dt) override;

    void setCamera(Camera* cam) { camera_ = cam; }
    void clearSelection() { selectedHandle_ = EntityHandle{}; }

    Entity* selectedEntity() const;
    EntityHandle selectedHandle() const { return selectedHandle_; }

    // Run the same selection algorithm as update() but against an explicit
    // world-space point. Useful for tests, scripted picks, or controller-input
    // pipelines that don't go through SDL mouse polling. Does NOT update
    // selectedHandle_ or fire onSelect_; the caller decides what to do with
    // the result.
    Entity* pickAtWorldPoint(Vec2 worldPoint) const;

    using SelectionCallback = std::function<void(Entity*, Targetable*)>;
    void setOnSelect(SelectionCallback cb) { onSelect_ = std::move(cb); }

private:
    Camera*           camera_ = nullptr;
    EntityHandle      selectedHandle_;
    SelectionCallback onSelect_;
};

} // namespace fate
