/**************************************************************************/
/*  spawn_zone_system.h                                                   */
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
// engine/gameplay2d/systems/spawn_zone_system.h
//
// Maintains a target-count of live entities per SpawnZone2D. The zone owns a
// std::vector of EntityHandles so we can detect deaths (handle goes invalid)
// without scanning the whole world. When a slot is empty and respawnTimer_
// has elapsed, the system requests a new entity from the host-supplied
// factory and pushes the resulting handle.
//
// The factory function is the entire integration boundary — the engine holds
// no entity catalogue. The demo registers a small map of "demo_dummy_mob" /
// "demo_villager_npc" closures during onInit.

#pragma once

#include "engine/ecs/world.h"
#include "engine/ecs/entity_handle.h"
#include "engine/core/types.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fate {

class SpawnZone2DSystem : public System {
public:
    const char* name() const override { return "SpawnZone2DSystem"; }
    void update(float dt) override;

    // Signature: (world, prefabKey, spawnPosition, factionOverride) -> entity handle
    using SpawnFactory = std::function<EntityHandle(World&, const std::string&, Vec2, uint32_t)>;
    void registerFactory(const std::string& prefabKey, SpawnFactory fn) {
        factories_[prefabKey] = std::move(fn);
    }
    void clearFactories() { factories_.clear(); zoneSlots_.clear(); }

private:
    std::unordered_map<std::string, SpawnFactory> factories_;
    // Per-zone-entity-id -> live spawned handles. Cleared when a handle goes
    // invalid (the entity was destroyed).
    std::unordered_map<EntityId, std::vector<EntityHandle>> zoneSlots_;
};

} // namespace fate
