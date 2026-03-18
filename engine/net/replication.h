#pragma once
#include "engine/net/aoi.h"
#include "engine/net/protocol.h"
#include "engine/net/net_server.h"
#include "engine/ecs/world.h"
#include "engine/ecs/persistent_id.h"
#include "engine/ecs/entity_handle.h"
#include "game/components/transform.h"
#include "game/components/sprite_component.h"
#include "game/components/game_components.h"
#include "game/components/faction_component.h"
#include <unordered_map>

namespace fate {

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

private:
    AOIConfig aoiConfig_;

    // Bidirectional mapping: EntityHandle <-> PersistentId
    std::unordered_map<uint32_t, PersistentId> handleToPid_; // key = EntityHandle packed value
    std::unordered_map<uint64_t, EntityHandle> pidToHandle_; // key = PersistentId value

    // Build AOI visibility for a single client
    void buildVisibility(World& world, ClientConnection& client);

    // Send enter/leave/update diffs for a single client
    void sendDiffs(World& world, NetServer& server, ClientConnection& client);

    // Build full snapshot for an entity entering AOI
    SvEntityEnterMsg buildEnterMessage(World& world, Entity* entity, PersistentId pid);

    // Build delta-compressed update for a visible entity
    SvEntityUpdateMsg buildCurrentState(World& world, Entity* entity, PersistentId pid);
};

} // namespace fate
