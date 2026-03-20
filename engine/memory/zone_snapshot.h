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
