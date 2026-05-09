/**************************************************************************/
/*  spawn_zone2d.h                                                        */
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
// engine/gameplay2d/components/spawn_zone2d.h
//
// Public spawn area for demo mobs/NPCs. SpawnZone2DSystem maintains
// `targetCount` live entities of `prefabKey` within `radius` of the zone
// center, respawning after `respawnSeconds` when one is destroyed.
//
// prefabKey resolution is host-supplied: the demo hands SpawnZone2DSystem a
// std::function<EntityHandle(World&, const std::string&, Vec2)> at init so
// authors can register their own spawn factories without coupling the engine
// to any specific entity catalogue.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/core/types.h"
#include <cstdint>
#include <string>

namespace fate {

struct SpawnZone2D {
    FATE_COMPONENT(SpawnZone2D)

    std::string prefabKey;            // factory key, e.g. "demo_dummy_mob"
    int         targetCount    = 1;
    float       respawnSeconds = 5.0f;
    float       radius         = 96.0f;
    uint32_t    factionOverride = 0;  // 0 = factory default
    bool        active         = true;

    // When true (default), a slot whose entity reports Health::isDead is also
    // destroyed by the spawn system before respawn — keeps long-lived servers
    // from accumulating corpse entities. Set false for ragdoll/loot-bag flows
    // where the dead entity is consumed by some other system.
    bool        destroyOnDeath = true;

    // Runtime — system-owned scratch.
    int   liveCount_     = 0;
    float respawnTimer_  = 0.0f;
};

} // namespace fate

FATE_REFLECT(fate::SpawnZone2D,
    FATE_FIELD(prefabKey, String),
    FATE_FIELD(targetCount, Int),
    FATE_FIELD(respawnSeconds, Float),
    FATE_FIELD(radius, Float),
    FATE_FIELD(factionOverride, UInt),
    FATE_FIELD(active, Bool),
    FATE_FIELD(destroyOnDeath, Bool)
)
