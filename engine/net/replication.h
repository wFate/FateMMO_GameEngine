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

// Cumulative since last reset. Phase 2 covered the scene-only pipeline; Phase
// 3a/3b extended it with distance/sticky/min-visible counters in lockstep with
// the behavior they describe.
//
// Read via ReplicationManager::stats(); zero via ReplicationManager::resetStats().
// Exposed to operators through the /aoi_stats GM command.
struct AOIStats {
    uint64_t ticks = 0;                        // update() invocations
    uint64_t clientsServed = 0;                // sum of clients with a non-skipped buildVisibility
    uint64_t clientSkippedNotPlaying = 0;      // playerEntityId==0 && spectateScene empty
    uint64_t clientSkippedNoScene = 0;         // empty-scene safety bail-out
    uint64_t scanned = 0;                      // handleToPid_ entries iterated (sum across clients)
    uint64_t selfExcluded = 0;                 // skipped: candidate is client's own player
    uint64_t entityMissingOrInactive = 0;      // skipped: world.getEntity null or !isActive()
    uint64_t sceneCulled = 0;                  // skipped: scene-bearing component != clientScene
    uint64_t unclassifiedSceneEntity = 0;      // admitted but had no scene-bearing component (leak counter)
    uint64_t visibilityFilterCulled = 0;       // skipped by custom visibilityFilter (e.g., GM invisibility)
    uint64_t admitted = 0;                     // pushed into aoi.current
    uint64_t entered = 0;                      // sum of |aoi.entered| across clients
    uint64_t left = 0;                         // sum of |aoi.left| across clients
    uint64_t stayed = 0;                       // sum of |aoi.stayed| across clients
    uint64_t recentlyUnregisteredFallback = 0; // leave path resolved PID via recentlyUnregistered_
    // Phase 3a — distance gate (added in lockstep with buildVisibility behavior).
    uint64_t missingPlayerPosition = 0;        // normal client w/ no resolved Transform; fail-closed skip
    uint64_t missingEntityPosition = 0;        // candidate w/ no Transform; fail-closed skip
    uint64_t distanceCulledNoPrevious = 0;     // distance > activation, was not previously visible
    uint64_t distanceCulledHadPrevious = 0;    // distance > deactivation, sticky bit released (and min-visible cleared)
    uint64_t stickyHeld = 0;                   // distance in [activation, deactivation], previous-visible kept admission
    // Phase 3b — min-visible-time anti-flap.
    uint64_t minVisibleHeld = 0;               // would-have-distance-culled, kept by min-visible floor
    uint64_t forcedLeaveDespawn = 0;           // leave bypassed min-visible because handle vanished from handleToPid_
    void reset() { *this = AOIStats{}; }
};

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

    // Phase 2 telemetry accessors. /aoi_stats GM command consumes these.
    const AOIStats& stats() const { return stats_; }
    void resetStats() { stats_.reset(); }
    size_t registeredEntityCount() const { return handleToPid_.size(); }

    // AOI tuning. Production server runs with the defaults baked into
    // AOIConfig (640 / 1280 / 10). Tests use this to widen the gate when
    // exercising sendDiffs paths (e.g. tier-cadence-skip) that under default
    // hysteresis would never receive Mid/Far/Edge candidates because the
    // distance gate culls everything beyond Near.
    void setAOIConfig(const AOIConfig& cfg) { aoiConfig_ = cfg; }
    const AOIConfig& aoiConfig() const { return aoiConfig_; }

private:
    AOIConfig aoiConfig_;
    AOIStats  stats_;

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
