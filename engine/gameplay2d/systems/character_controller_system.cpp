/**************************************************************************/
/*  character_controller_system.cpp                                       */
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
#include "engine/gameplay2d/systems/character_controller_system.h"
#include "engine/gameplay2d/components/character_controller2d.h"
#include "engine/gameplay2d/components/collider2d.h"
#include "engine/gameplay2d/components/replicated_transform2d.h"
#include "engine/components/transform.h"
#include "engine/input/input.h"
#include "engine/core/types.h"
#include <cmath>

namespace fate {

namespace {

bool sweepsBlockedTile(const CollisionGrid* grid, const Vec2& center, float halfW, float halfH) {
    if (!grid || grid->empty()) return false;
    return grid->isBlockedRect(center.x, center.y, halfW, halfH);
}

bool collidesWithStatics(World& world, const Vec2& center, const Collider2D* selfCol,
                         const Entity* selfEnt) {
    bool hit = false;
    world.forEach<Transform, Collider2D>([&](Entity* e, Transform* tx, Collider2D* col) {
        if (hit || e == selfEnt) return;
        if (col->isTrigger || !col->isStatic) return;
        // Honor layer/mask. If the moving entity has no Collider2D, fall back
        // to the legacy "everyone collides" behavior so demos that hand-roll a
        // controller without a collider still block on walls.
        if (selfCol && !selfCol->interactsWith(*col)) return;
        if (selfCol) {
            if (Collider2D::overlaps(*selfCol, center, *col, tx->position)) hit = true;
        } else {
            Rect b = col->getBounds(tx->position);
            Rect movingBounds{ center.x - 14.0f, center.y - 14.0f, 28.0f, 28.0f };
            if (movingBounds.overlaps(b)) hit = true;
        }
    });
    return hit;
}

void resolveAxis(World& world, Transform& tx, float& delta, bool xAxis,
                 const CollisionGrid* grid, float halfW, float halfH,
                 const Collider2D* selfCol, const Entity* selfEnt) {
    if (delta == 0.0f) return;
    Vec2 trial = tx.position;
    if (xAxis) trial.x += delta;
    else       trial.y += delta;

    if (sweepsBlockedTile(grid, trial, halfW, halfH) ||
        collidesWithStatics(world, trial, selfCol, selfEnt)) {
        return; // axis blocked
    }
    tx.position = trial;
}

} // anonymous

void CharacterController2DSystem::update(float dt) {
    if (!world_) return;

    Input& input = Input::instance();

    world_->forEach<Transform, CharacterController2D>(
        [&](Entity* e, Transform* tx, CharacterController2D* cc) {
            if (cc->authority == MovementAuthority::ServerAuthoritative) {
                cc->isMoving = false;
                return;
            }

            Vec2 inputVec{0.0f, 0.0f};
            if (cc->isLocalPlayer && cc->inputEnabled && !input.isUIBlocking()) {
                Direction d = input.getCardinalDirection();
                inputVec = directionToVec(d);
                if (d != Direction::None) cc->facing = d;
            } else if (cc->authority == MovementAuthority::AISimulated) {
                inputVec = cc->lastInputVec_;  // written by Mob2DSystem
                if (inputVec.lengthSq() > 0.001f) {
                    if (std::fabs(inputVec.x) > std::fabs(inputVec.y))
                        cc->facing = (inputVec.x > 0) ? Direction::Right : Direction::Left;
                    else
                        cc->facing = (inputVec.y > 0) ? Direction::Up : Direction::Down;
                }
            }

            cc->lastInputVec_ = inputVec;
            cc->isMoving = (inputVec.lengthSq() > 0.0001f);

            if (cc->isMoving) cc->animCoastTimer_ = cc->animCoastSeconds;
            else if (cc->animCoastTimer_ > 0.0f) cc->animCoastTimer_ -= dt;

            if (!cc->isMoving) return;

            float halfW = 14.0f, halfH = 14.0f;
            Collider2D* selfCol = e->getComponent<Collider2D>();
            if (selfCol) {
                Rect b = selfCol->getBounds(tx->position);
                halfW = b.w * 0.5f;
                halfH = b.h * 0.5f;
            }

            float dx = inputVec.x * cc->moveSpeed * dt;
            float dy = inputVec.y * cc->moveSpeed * dt;

            if (cc->collisionEnabled) {
                resolveAxis(*world_, *tx, dx, /*xAxis=*/true,  grid_, halfW, halfH, selfCol, e);
                resolveAxis(*world_, *tx, dy, /*xAxis=*/false, grid_, halfW, halfH, selfCol, e);
            } else {
                tx->position.x += dx;
                tx->position.y += dy;
            }
        });

    // Independent pass: smooth ReplicatedTransform2D entities toward target.
    world_->forEach<Transform, ReplicatedTransform2D>(
        [&](Entity*, Transform* tx, ReplicatedTransform2D* rep) {
            if (!rep->hasReceivedSnapshot) return;
            float t = rep->interpRate * dt;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            tx->position.x += (rep->targetPosition.x - tx->position.x) * t;
            tx->position.y += (rep->targetPosition.y - tx->position.y) * t;
            rep->timeSinceLastUpdate_ += dt;
        });
}

} // namespace fate
