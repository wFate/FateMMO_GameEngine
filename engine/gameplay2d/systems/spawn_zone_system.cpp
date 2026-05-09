/**************************************************************************/
/*  spawn_zone_system.cpp                                                 */
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
#include "engine/gameplay2d/systems/spawn_zone_system.h"
#include "engine/gameplay2d/components/spawn_zone2d.h"
#include "engine/gameplay2d/components/health.h"
#include "engine/components/transform.h"
#include <cmath>
#include <cstdlib>
#include <vector>

namespace fate {

namespace {

Vec2 randomOffset(float radius) {
    if (radius <= 0.0f) return {0.0f, 0.0f};
    float ang = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 6.2831853f;
    float r   = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * radius;
    return { std::cos(ang) * r, std::sin(ang) * r };
}

// A slot is vacant if its entity is gone, OR if its entity carries a Health
// component and that component reports isDead. The HealthDamageSystem flips
// isDead but does not destroy entities, so the spawn zone owns that policy.
bool slotIsVacant(World& world, EntityHandle h, bool* outDeadButAlive) {
    *outDeadButAlive = false;
    if (!world.isAlive(h)) return true;
    Entity* e = world.getEntity(h);
    if (!e) return true;
    if (auto* hp = e->getComponent<Health>()) {
        if (hp->isDead) {
            *outDeadButAlive = true;
            return true;
        }
    }
    return false;
}

} // anonymous

void SpawnZone2DSystem::update(float dt) {
    if (!world_) return;

    // First pass: collect zone work + entities that need destruction. We can't
    // call destroyEntity inside forEach (structural change during iteration is
    // forbidden by the World contract).
    std::vector<EntityHandle> toDestroy;

    world_->forEach<Transform, SpawnZone2D>(
        [&](Entity* zoneEntity, Transform*, SpawnZone2D* sz) {
            if (!sz->active || sz->prefabKey.empty()) return;
            auto& slots = zoneSlots_[zoneEntity->id()];

            for (size_t i = 0; i < slots.size(); ) {
                bool deadButAlive = false;
                if (slotIsVacant(*world_, slots[i], &deadButAlive)) {
                    if (deadButAlive && sz->destroyOnDeath) {
                        toDestroy.push_back(slots[i]);
                    }
                    slots[i] = slots.back();
                    slots.pop_back();
                } else {
                    ++i;
                }
            }
            sz->liveCount_ = static_cast<int>(slots.size());
        });

    for (EntityHandle h : toDestroy) {
        world_->destroyEntity(h);
    }

    // Second pass: respawn into vacant slots (now that the world is structurally
    // settled and the slot bookkeeping reflects the post-destroy count).
    world_->forEach<Transform, SpawnZone2D>(
        [&](Entity* zoneEntity, Transform* zoneTx, SpawnZone2D* sz) {
            if (!sz->active || sz->prefabKey.empty()) return;
            auto& slots = zoneSlots_[zoneEntity->id()];

            if (sz->liveCount_ >= sz->targetCount) {
                sz->respawnTimer_ = 0.0f;
                return;
            }

            sz->respawnTimer_ += dt;
            if (sz->respawnTimer_ < sz->respawnSeconds) return;
            sz->respawnTimer_ = 0.0f;

            auto it = factories_.find(sz->prefabKey);
            if (it == factories_.end()) return;

            Vec2 spawnPos = zoneTx->position + randomOffset(sz->radius);
            EntityHandle h = it->second(*world_, sz->prefabKey, spawnPos, sz->factionOverride);
            if (!h.isNull()) {
                slots.push_back(h);
                sz->liveCount_ = static_cast<int>(slots.size());
            }
        });
}

} // namespace fate
