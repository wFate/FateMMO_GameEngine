/**************************************************************************/
/*  trigger_system.cpp                                                    */
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
#include "engine/gameplay2d/systems/trigger_system.h"
#include "engine/gameplay2d/components/trigger_area2d.h"
#include "engine/gameplay2d/components/collider2d.h"
#include "engine/components/transform.h"

namespace fate {

void Trigger2DSystem::update(float /*dt*/) {
    if (!world_) return;

    world_->forEach<Transform, TriggerArea2D>(
        [&](Entity* trigEntity, Transform* trigTx, TriggerArea2D* trig) {
            trig->wasOverlapping = trig->isOverlapping;
            trig->isOverlapping  = false;

            world_->forEach<Transform, Collider2D>(
                [&](Entity* colEntity, Transform* colTx, Collider2D* col) {
                    if (trig->isOverlapping) return;          // early out
                    if (colEntity == trigEntity) return;

                    bool layerMatch = ((trig->mask  & col->layer) != 0u) ||
                                      ((col->mask  & trig->layer) != 0u);
                    if (!layerMatch) return;

                    // Shape-aware overlap. AABB-vs-AABB falsely fires on the
                    // diagonal corner of a Circle trigger / Circle collider.
                    if (Collider2D::shapesOverlap(
                            trig->shape, trig->offset, trig->size, trigTx->position,
                            col->shape,  col->offset,  col->size,  colTx->position)) {
                        trig->isOverlapping = true;
                    }
                });
        });
}

} // namespace fate
