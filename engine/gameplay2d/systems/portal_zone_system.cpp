/**************************************************************************/
/*  portal_zone_system.cpp                                                */
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
#include "engine/gameplay2d/systems/portal_zone_system.h"
#include "engine/gameplay2d/components/portal2d.h"
#include "engine/gameplay2d/components/zone2d.h"
#include "engine/gameplay2d/components/character_controller2d.h"
#include "engine/components/transform.h"

namespace fate {

void PortalZoneSystem::update(float /*dt*/) {
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

    if (!player || !playerTx) return;

    // Zone tracking.
    Entity*  resolvedZoneEntity = nullptr;
    Zone2D*  resolvedZone       = nullptr;
    world_->forEach<Transform, Zone2D>(
        [&](Entity* e, Transform* tx, Zone2D* z) {
            if (resolvedZone) return;
            if (z->contains(tx->position, playerTx->position)) {
                resolvedZoneEntity = e;
                resolvedZone       = z;
            }
        });

    std::string newZoneId   = resolvedZone ? resolvedZone->zoneId       : std::string();
    std::string newZoneName = resolvedZone ? resolvedZone->displayName  : std::string();

    if (newZoneId != currentZoneId_) {
        currentZoneId_   = newZoneId;
        currentZoneName_ = newZoneName;
        if (onZoneChanged_) onZoneChanged_(player, resolvedZoneEntity, resolvedZone);
    }

    // Portal entries.
    world_->forEach<Transform, Portal2D>(
        [&](Entity* e, Transform* tx, Portal2D* p) {
            p->firedThisFrame_ = false;
            Rect bounds = p->getTriggerBounds(tx->position);
            bool inside = bounds.contains(playerTx->position);
            if (inside && !p->playerInside_) {
                p->playerInside_ = true;
                p->firedThisFrame_ = true;
                if (onPortalEnter_) {
                    onPortalEnter_(player, e, p);
                } else if (p->targetScene.empty()) {
                    // Same-scene default: teleport to the target spawn.
                    playerTx->position = p->targetSpawnPos;
                }
            } else if (!inside) {
                p->playerInside_ = false;
            }
        });
}

} // namespace fate
