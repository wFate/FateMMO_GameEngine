/**************************************************************************/
/*  targeting_system.cpp                                                  */
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
#include "engine/gameplay2d/systems/targeting_system.h"
#include "engine/gameplay2d/components/targetable.h"
#include "engine/gameplay2d/components/character_controller2d.h"
#include "engine/components/transform.h"
#include "engine/render/camera.h"
#include "engine/input/input.h"
#include <cstdint>

namespace fate {

namespace {

int categoryRank(TargetCategory c) {
    switch (c) {
        case TargetCategory::Hostile:      return 4;
        case TargetCategory::Interactable: return 3;
        case TargetCategory::Friendly:     return 2;
        case TargetCategory::Neutral:      return 1;
    }
    return 0;
}

} // anonymous

Entity* Targeting2DSystem::selectedEntity() const {
    if (!world_ || selectedHandle_.isNull()) return nullptr;
    return world_->getEntity(selectedHandle_);
}

Entity* Targeting2DSystem::pickAtWorldPoint(Vec2 worldPoint) const {
    if (!world_) return nullptr;

    Entity*     bestEntity = nullptr;
    Targetable* bestComp   = nullptr;
    int         bestRank   = -1;
    int         bestPrio   = 0;
    float       bestDistSq = 0.0f;

    world_->forEach<Transform, Targetable>(
        [&](Entity* e, Transform* tx, Targetable* t) {
            if (!t->selectable) return;
            // canTargetSelf gate: skip the local player unless they opted in.
            if (!t->canTargetSelf) {
                if (auto* cc = e->getComponent<CharacterController2D>()) {
                    if (cc->isLocalPlayer) return;
                }
            }
            float dx = worldPoint.x - tx->position.x;
            float dy = worldPoint.y - tx->position.y;
            float distSq = dx * dx + dy * dy;
            if (distSq > t->radius * t->radius) return;

            int rank = categoryRank(t->category);
            if (rank < bestRank) return;
            if (rank == bestRank) {
                // Within the same category, priority is the hard tiebreaker.
                // Distance only breaks ties when priorities are equal — the
                // previous logic let a closer-but-lower-priority candidate win.
                if (t->priority < bestPrio) return;
                if (t->priority == bestPrio && distSq > bestDistSq) return;
            }
            bestEntity = e;
            bestComp   = t;
            bestRank   = rank;
            bestPrio   = t->priority;
            bestDistSq = distSq;
        });

    (void)bestComp;
    return bestEntity;
}

void Targeting2DSystem::update(float /*dt*/) {
    if (!world_ || !camera_) return;
    Input& input = Input::instance();
    if (!input.isMousePressed(1)) return;     // SDL left button
    if (input.isUIBlocking()) return;

    Vec2 worldClick = camera_->screenToWorld(input.mousePosition(),
                                             input.windowWidth(),
                                             input.windowHeight());

    Entity* picked = pickAtWorldPoint(worldClick);
    if (picked) {
        selectedHandle_ = picked->handle();
        Targetable* tc = picked->getComponent<Targetable>();
        if (onSelect_) onSelect_(picked, tc);
    } else {
        selectedHandle_ = EntityHandle{};
        if (onSelect_) onSelect_(nullptr, nullptr);
    }
}

} // namespace fate
