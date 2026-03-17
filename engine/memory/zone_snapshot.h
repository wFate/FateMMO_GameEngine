#pragma once
#include "engine/ecs/persistent_id.h"
#include <cstdint>
#include <string>
#include <vector>

namespace fate {

// ==========================================================================
// Zone Snapshot — data structures for zone state serialization
//
// These are skeleton types. The actual serialize/deserialize logic will be
// implemented when persistence is wired up. For now, just the data layout.
// ==========================================================================

// Serialized snapshot of a single entity's component data.
struct EntitySnapshot {
    PersistentId persistentId;
    std::vector<uint8_t> componentData; // opaque serialized components
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
