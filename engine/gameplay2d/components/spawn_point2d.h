/**************************************************************************/
/*  spawn_point2d.h                                                       */
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
// engine/gameplay2d/components/spawn_point2d.h
//
// Authoring marker — "this position is a valid place to spawn an entity of
// kind spawnId". Used by the demo's startup code to find player-start, pet-
// spawn, etc. SpawnZone2D handles repeated/random spawning; SpawnPoint2D is a
// single named anchor.

#pragma once

#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <cstdint>
#include <string>

namespace fate {

struct SpawnPoint2D {
    FATE_COMPONENT(SpawnPoint2D)

    std::string spawnId;            // "player_start", "boss_arena_entry", etc.
    std::string entityType;         // optional prefab key
    uint32_t    teamId       = 0;
    bool        isLocalPlayer = false;
    bool        isTownSpawn  = false;
};

} // namespace fate

FATE_REFLECT(fate::SpawnPoint2D,
    FATE_FIELD(spawnId, String),
    FATE_FIELD(entityType, String),
    FATE_FIELD(teamId, UInt),
    FATE_FIELD(isLocalPlayer, Bool),
    FATE_FIELD(isTownSpawn, Bool)
)
