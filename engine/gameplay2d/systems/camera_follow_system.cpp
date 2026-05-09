/**************************************************************************/
/*  camera_follow_system.cpp                                              */
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
#include "engine/gameplay2d/systems/camera_follow_system.h"
#include "engine/gameplay2d/components/camera_follow2d.h"
#include "engine/components/transform.h"
#include "engine/render/camera.h"
#include <cmath>

namespace fate {

void CameraFollow2DSystem::update(float dt) {
    if (!world_ || !camera_) return;

    Entity*           bestEntity   = nullptr;
    Transform*        bestTx       = nullptr;
    CameraFollow2D*   bestFollow   = nullptr;
    int               bestPriority = INT32_MIN;

    world_->forEach<Transform, CameraFollow2D>(
        [&](Entity* e, Transform* tx, CameraFollow2D* cf) {
            if (cf->priority > bestPriority) {
                bestEntity   = e;
                bestTx       = tx;
                bestFollow   = cf;
                bestPriority = cf->priority;
            }
        });

    if (!bestFollow || !bestTx) return;
    (void)bestEntity;

    Vec2 desired = bestTx->position + bestFollow->offset;

    if (bestFollow->mode == CameraFollowMode::Snap) {
        camera_->setPosition(desired);
        return;
    }

    Vec2 current = camera_->position();
    float dx = desired.x - current.x;
    float dy = desired.y - current.y;

    if (bestFollow->deadzone.x > 0.0f && std::fabs(dx) < bestFollow->deadzone.x) dx = 0.0f;
    if (bestFollow->deadzone.y > 0.0f && std::fabs(dy) < bestFollow->deadzone.y) dy = 0.0f;

    float t = bestFollow->lerpRate * dt;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    camera_->setPosition({ current.x + dx * t, current.y + dy * t });
}

} // namespace fate
