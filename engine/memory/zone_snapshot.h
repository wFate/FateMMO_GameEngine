/**************************************************************************/
/*  zone_snapshot.h                                                       */
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
#pragma once
#include "engine/ecs/persistent_id.h"
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace fate {

// ==========================================================================
// Zone Snapshot — data structures for zone state serialization
//
// Entity snapshots use JSON for component data (same format as scene files),
// enabling reuse of PrefabLibrary::jsonToEntity() for deserialization.
// ==========================================================================

// Serialized snapshot of a single entity's component data.
struct EntitySnapshot {
    std::string entityName;
    std::string entityTag;
    nlohmann::json entityJson; // full entity JSON (name, tag, components) — same format as scene files
};

// Tracked mob state within a spawn zone (position, health, respawn timer).
struct TrackedMobSnapshot {
    std::string enemyId;
    float posX = 0.0f;
    float posY = 0.0f;
    float health = 0.0f;
    double respawnAt = -1.0; // absolute timestamp, -1 if alive
    bool alive = true;
};

// Snapshot of a spawn zone and its tracked mobs.
struct SpawnZoneSnapshot {
    std::string zoneName;
    std::vector<TrackedMobSnapshot> trackedMobs;
};

// Full snapshot of a zone's state — entities + spawn zones.
struct ZoneSnapshot {
    uint32_t version = 1;
    std::string zoneName;
    double worldTime = 0.0;
    std::vector<EntitySnapshot> entities;
    std::vector<SpawnZoneSnapshot> spawnZones;

    bool hasData() const { return !entities.empty() || !spawnZones.empty(); }
    void clear() { entities.clear(); spawnZones.clear(); }
};

} // namespace fate
