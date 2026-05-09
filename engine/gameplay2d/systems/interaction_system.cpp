/**************************************************************************/
/*  interaction_system.cpp                                                */
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
#include "engine/gameplay2d/systems/interaction_system.h"
#include "engine/gameplay2d/components/interactable.h"
#include "engine/gameplay2d/components/character_controller2d.h"
#include "engine/components/transform.h"
#include "engine/input/input.h"

namespace fate {

void Interaction2DSystem::update(float /*dt*/) {
    if (!world_) return;

    Entity*    player   = nullptr;
    Transform* playerTx = nullptr;

    world_->forEach<Transform, CharacterController2D>(
        [&](Entity* e, Transform* tx, CharacterController2D* cc) {
            if (cc->isLocalPlayer && !player) {
                player   = e;
                playerTx = tx;
            }
        });

    nearestEntity_   = nullptr;
    nearestDistance_ = 0.0f;
    nearestPrompt_.clear();

    if (!player || !playerTx) return;

    // Reset the per-frame trigger flag on every interactable, then find the
    // nearest one in range.
    Entity*       chosen      = nullptr;
    Interactable* chosenComp  = nullptr;
    float         chosenDistSq = 0.0f;

    world_->forEach<Transform, Interactable>(
        [&](Entity* e, Transform* tx, Interactable* it) {
            it->triggeredThisFrame_ = false;
            if (!it->enabled || (it->consumed_ && !it->repeatable)) return;

            float dx = tx->position.x - playerTx->position.x;
            float dy = tx->position.y - playerTx->position.y;
            float distSq = dx * dx + dy * dy;
            float r = it->interactionRadius;
            if (distSq > r * r) return;

            if (!chosen || distSq < chosenDistSq) {
                chosen      = e;
                chosenComp  = it;
                chosenDistSq = distSq;
            }
        });

    if (chosen && chosenComp) {
        nearestEntity_ = chosen;
        nearestDistance_ = (chosenDistSq > 0.0f) ? std::sqrt(chosenDistSq) : 0.0f;
        nearestPrompt_ = chosenComp->prompt;

        bool keyPressed = false;
        if (interactKey_ != SDL_SCANCODE_UNKNOWN) {
            keyPressed = Input::instance().isKeyPressed(interactKey_);
        }

        if (keyPressed || wantsTrigger_) {
            wantsTrigger_ = false;
            chosenComp->triggeredThisFrame_ = true;
            chosenComp->consumed_ = true;
            if (onInteract_) onInteract_(chosen, chosenComp);
        }
    }
}

} // namespace fate
