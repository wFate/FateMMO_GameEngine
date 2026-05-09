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
#include <array>
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
    // Stage D.1 — sticky-band sub-tier (sendDiffs cadence). These are
    // PRE-DIRTY cadence-disposition counters: they are incremented at the
    // cadence gate BEFORE buildCurrentState() and the dirtyMask==0 filter
    // downstream. A stationary entity can therefore be counted as
    // stayedFullRate (or stayedCriticalOverride) and still produce zero
    // bytes on the wire because the post-build dirtyMask check drops it.
    //
    // Source of truth for actual byte savings is NetServer per-opcode
    // stats on 0xBA (SvEntityUpdateBatch); these counters describe how
    // often the cadence allowed/denied an emit candidate, not how many
    // packets were emitted. Sums together describe each tick's stayed-set
    // cadence disposition:
    //   stayedFullRate: cadence allowed full-rate consideration this tick
    //     (inside activation, OR sticky-band on its scheduled offset
    //     frame, OR no Transform / observer client where cadence is
    //     bypassed by design). Whether bytes hit the wire still depends
    //     on dirtyMask.
    //   stayedThrottled: cadence skipped this tick (sticky band on an
    //     off-offset frame and no critical HP/death change). buildCurrent
    //     State did not run, so this entity contributes zero 0xBA bytes
    //     this tick by construction. Note: a throttled entity is not
    //     guaranteed savings — it may have been dirtyMask==0 anyway.
    //   stayedCriticalOverride: cadence would have skipped, but the
    //     HP/death probe forced the entity through to the build stage.
    //     dirtyMask still decides whether bytes go on the wire.
    uint64_t stayedFullRate = 0;
    uint64_t stayedThrottled = 0;
    uint64_t stayedCriticalOverride = 0;

    // Stage D.1b Phase 1 — bucketed telemetry. Distance bucket boundaries
    // (squared-distance comparisons match D.1's `d2 > actR2` semantics so
    // d == 640 stays in B3, d == 640.001 falls to B4):
    //   B0 = [0, 256]    px   d² ≤ 65536
    //   B1 = (256, 384]  px   d² ≤ 147456
    //   B2 = (384, 512]  px   d² ≤ 262144
    //   B3 = (512, 640]  px   d² ≤ 409600   (current sticky-band boundary)
    //   B4 = (640, 1280] px   d² >  409600  (sticky band, throttled by D.1)
    //
    // Counters apply only to stayed entries with a Transform AND a non-
    // observer client (the cadence/distance branch). Observer / no-Transform
    // entries take the `else` branch and bypass bucket accounting by design
    // (no anchor distance to bucket against).
    //
    // bucketStayed[i] is the total stayed entries that landed in bucket i;
    // bucketCadence{FullRate,Throttled,CriticalOverride}[i] partitions that
    // total by cadence disposition (sum across the three == bucketStayed[i]).
    // bucketDirtyMaskNonzero[i] and bucketEmittedBytes[i] are POST-DIRTY:
    // they increment only after the dirtyMask != 0 filter and capture the
    // actual byte contribution to 0xBA SvEntityUpdateBatch (via tmpWriter
    // size at serialize time). These two are the byte-truth axis the
    // pre-dirty cadence counters above explicitly do not provide.
    //
    // Per-bucket hot-reason counters mirror Stage C MobAILane classification
    // order from game/shared/mobai_scheduler.cpp; mutually exclusive within
    // a bucket. Sum of hot-reason counters == bucketStayed[i] when all
    // entities are mobs; bucketNonMob[i] tracks players/NPCs/drops (no
    // MobAIComponent) so the sum still closes:
    //   bucketHot[i]           — combat AIMode (Chase/ChaseMemory/Attack)
    //   bucketHotProximity[i]  — dearWarmedUp + lastTickInterval ≤ 0.001
    //   bucketForcedTarget[i]  — taunt active or pending decrement
    //   bucketCold[i]          — has MobAIComponent, none of the above
    //   bucketNonMob[i]        — no MobAIComponent
    std::array<uint64_t, 5> bucketStayed{};
    std::array<uint64_t, 5> bucketCadenceFullRate{};
    std::array<uint64_t, 5> bucketCadenceThrottled{};
    std::array<uint64_t, 5> bucketCadenceCriticalOverride{};
    std::array<uint64_t, 5> bucketDirtyMaskNonzero{};
    std::array<uint64_t, 5> bucketEmittedBytes{};
    std::array<uint64_t, 5> bucketHot{};
    std::array<uint64_t, 5> bucketHotProximity{};
    std::array<uint64_t, 5> bucketForcedTarget{};
    std::array<uint64_t, 5> bucketCold{};
    std::array<uint64_t, 5> bucketNonMob{};

    // D.1b Phase 2 telemetry (Fork B). Per-bucket dirty-bit histogram. Bit
    // indices match the SvEntityUpdateMsg fieldMask layout (bit 0 = position,
    // bit 1 = animFrame, ..., bit 16 = costumeVisuals). Increments only at
    // POST-DIRTY (one increment per set bit per emit), so the sum of all 17
    // entries in row bucket[i] is >= bucketDirtyMaskNonzero[i] (each emit
    // contributes between 1 and 17 increments depending on field count).
    //
    // Prior smoke showed B0..B2 carry ~77% of 0xBA bytes with a flat ~21 B
    // per entry; this histogram identifies which fields drive that share so
    // the regime decision (position quantization vs. cadence throttle vs.
    // field-specific change) has data. No behavior change.
    std::array<std::array<uint64_t, 17>, 5> bucketDirtyByField{};

    // D.1b Phase 2 telemetry (Fork D). Bucket-to-bucket transition matrix.
    // bucketTransitions[from][to] increments when an entity that emitted in
    // bucket=from on its previous emit now emits in bucket=to. Detects the
    // band->inner cascade hypothesis: D.1's sticky-band 1/3 cadence holds
    // lastSentState stale for up to 3 ticks, so when the player walks the
    // entity into the inner buckets the entity emits a fat dirtyMask
    // covering every change since the last full-rate emit. If
    // bucketTransitions[4][0..3] dominates and bucketEmittedBytes[0..3] is
    // significantly higher per-emit than bucketEmittedBytes[4] would
    // suggest, the cascade is real and the fix is to refresh lastSentState
    // on cadence-throttle even when not emitting.
    std::array<std::array<uint64_t, 5>, 5> bucketTransitions{};

    // 0xBA flush-attribution counters. The wire-side counter
    // `OpcodeStats::sentPackets[SvEntityUpdateBatch]` only shows actual
    // sends; without these we cannot tell whether a "low batch count"
    // window was zero-delta ticks, dirtyMask==0 skips, or genuine UDP
    // loss. All four are per-server (sum across all clients per window).
    //   batchesSentDeltas       — count of flushBatch() calls that
    //                              actually wrote a 0xBA packet
    //                              (deltaCount > 0 at flush time).
    //   batchedEntitiesTotal    — sum of deltaCount across those sends
    //                              (i.e. how many per-entity update
    //                              records went on the wire).
    //   flushBatchZeroDelta     — flushBatch() called with deltaCount == 0
    //                              and short-circuited (no 0xBA send).
    //                              One per flush invocation per client.
    //   dirtyMaskZeroSkips      — `if (dirtyMask == 0) continue;` hits
    //                              after buildCurrentState() ran. Sums
    //                              with stayedThrottled + stayedFullRate
    //                              to attribute "no batch this tick" to
    //                              cadence vs. nothing-changed.
    uint64_t batchesSentDeltas    = 0;
    uint64_t batchedEntitiesTotal = 0;
    uint64_t flushBatchZeroDelta  = 0;
    uint64_t dirtyMaskZeroSkips   = 0;

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

    // Snapshot the 0xBA flush-attribution counters for the 5s server log
    // (added 2026-05-07 alongside the NetClient classifyGap bias fix).
    //
    // Two reset policies in one helper:
    //   * batches/deltas/zeroDeltaFlushes/dirtyMaskSkips were ADDED for this
    //     telemetry slice and aren't consumed by /aoi_stats — snapshot AND
    //     zero (per-window).
    //   * clientsServed/stayedFullRate/stayedThrottled/stayedCriticalOverride
    //     are pre-existing cumulative counters that /aoi_stats reads as
    //     running totals. We compute deltas vs a private prev-snapshot
    //     instead of zeroing stats_ — so the 5s log shows windowed values
    //     without breaking the GM command's "since reset" semantics.
    //     Saturates at 0 if `/aoi_stats reset` ran during the window
    //     (current < prev → delta clamped to 0).
    struct FlushAttribution {
        // Per-window (reset semantics)
        uint64_t batchesSentDeltas      = 0;
        uint64_t batchedEntitiesTotal   = 0;
        uint64_t flushBatchZeroDelta    = 0;
        uint64_t dirtyMaskZeroSkips     = 0;
        // Per-window deltas vs prev snapshot; stats_ totals untouched
        uint64_t clientsServedDelta          = 0;
        uint64_t stayedFullRateDelta         = 0;
        uint64_t stayedThrottledDelta        = 0;
        uint64_t stayedCriticalOverrideDelta = 0;
    };
    FlushAttribution flushAttributionCounters() {
        auto saturatingDelta = [](uint64_t cur, uint64_t prev) -> uint64_t {
            return (cur >= prev) ? (cur - prev) : 0;
        };
        FlushAttribution out{
            stats_.batchesSentDeltas,
            stats_.batchedEntitiesTotal,
            stats_.flushBatchZeroDelta,
            stats_.dirtyMaskZeroSkips,
            saturatingDelta(stats_.clientsServed,          flushAttrPrev_.clientsServed),
            saturatingDelta(stats_.stayedFullRate,         flushAttrPrev_.stayedFullRate),
            saturatingDelta(stats_.stayedThrottled,        flushAttrPrev_.stayedThrottled),
            saturatingDelta(stats_.stayedCriticalOverride, flushAttrPrev_.stayedCriticalOverride)
        };
        stats_.batchesSentDeltas    = 0;
        stats_.batchedEntitiesTotal = 0;
        stats_.flushBatchZeroDelta  = 0;
        stats_.dirtyMaskZeroSkips   = 0;
        flushAttrPrev_.clientsServed          = stats_.clientsServed;
        flushAttrPrev_.stayedFullRate         = stats_.stayedFullRate;
        flushAttrPrev_.stayedThrottled        = stats_.stayedThrottled;
        flushAttrPrev_.stayedCriticalOverride = stats_.stayedCriticalOverride;
        return out;
    }

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
    // Per-window prev snapshot for `flushAttributionCounters()` deltas.
    // Holds the running totals of the four AOI counters at the moment of
    // the LAST flush; subtracted from current totals to produce window
    // deltas without disturbing stats_ (which /aoi_stats consumes).
    struct FlushAttrPrev {
        uint64_t clientsServed          = 0;
        uint64_t stayedFullRate         = 0;
        uint64_t stayedThrottled        = 0;
        uint64_t stayedCriticalOverride = 0;
    };
    FlushAttrPrev flushAttrPrev_;

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
