#pragma once
#include "engine/net/aoi.h"
#include "engine/net/protocol.h"
#include "engine/net/net_server.h"
#include "engine/net/update_frequency.h"
#include "engine/ecs/world.h"
#include "engine/ecs/persistent_id.h"
#include "engine/ecs/entity_handle.h"
#include "engine/spatial/spatial_hash.h"
#ifdef FATE_HAS_GAME
#include "engine/components/transform.h"
#include "engine/components/sprite_component.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include "game/components/player_controller.h"
#include "game/components/animator.h"
#endif // FATE_HAS_GAME
#include <unordered_map>
#include <functional>

namespace fate {

class ItemDefinitionCache;
class CostumeCache;

class ReplicationManager {
public:
    // Call once per server tick after world update
    void update(World& world, NetServer& server);

    // Register a persistent ID for a server entity (call when entity is created)
    void registerEntity(EntityHandle handle, PersistentId pid);

    // Unregister (call when entity is destroyed)
    void unregisterEntity(EntityHandle handle);

    // Get PersistentId for an entity handle (returns null PersistentId if not found)
    PersistentId getPersistentId(EntityHandle handle) const;

    // Get EntityHandle for a PersistentId (returns EntityHandle{} if not found)
    EntityHandle getEntityHandle(PersistentId pid) const;

    void setItemDefCache(const ItemDefinitionCache* cache) { itemDefCache_ = cache; }
    void setCostumeCache(const CostumeCache* cache) { costumeCache_ = cache; }

    // Optional per-entity visibility filter. Return true to SKIP (hide) the entity.
    std::function<bool(uint64_t entityPid, const ClientConnection& observer)> visibilityFilter;

private:
    AOIConfig aoiConfig_;

    // Bidirectional mapping: EntityHandle <-> PersistentId
    std::unordered_map<uint32_t, PersistentId> handleToPid_; // key = EntityHandle packed value
    std::unordered_map<uint64_t, EntityHandle> pidToHandle_; // key = PersistentId value

    // Spatial index for registered entities, rebuilt each tick
    SpatialHashEngine spatialIndex_{128.0f, 4096};

    // Per-entity monotonic sequence counter for unreliable update ordering
    std::unordered_map<uint32_t, uint8_t> entitySeqCounters_; // key = EntityHandle packed value

    // PIDs of entities unregistered this tick — allows sendDiffs to still send SvEntityLeave
    std::unordered_map<uint32_t, PersistentId> recentlyUnregistered_;

    // Monotonic tick counter incremented each update(); drives tiered update frequency
    uint32_t tickCounter_ = 0;

    // Rebuild the spatial index with current positions of all registered entities
    void rebuildSpatialIndex(World& world);

    // Build AOI visibility for a single client
    void buildVisibility(World& world, ClientConnection& client);

    // Send enter/leave/update diffs for a single client
    void sendDiffs(World& world, NetServer& server, ClientConnection& client);

    // Build full snapshot for an entity entering AOI
    SvEntityEnterMsg buildEnterMessage(World& world, Entity* entity, PersistentId pid);

    // Build delta-compressed update for a visible entity
    SvEntityUpdateMsg buildCurrentState(World& world, Entity* entity, PersistentId pid);

    const ItemDefinitionCache* itemDefCache_ = nullptr;
    const CostumeCache* costumeCache_ = nullptr;
};

} // namespace fate
