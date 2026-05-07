#include "engine/net/replication.h"
#include "engine/net/packet.h"
#ifdef FATE_HAS_GAME
#include "game/components/dropped_item_component.h"
#include "game/components/game_components.h"
#include "game/shared/honor_system.h"
#include "server/cache/item_definition_cache.h"
#include "server/cache/costume_cache.h"
#endif // FATE_HAS_GAME
#include <cmath>

namespace fate {

#ifdef FATE_HAS_GAME

// S143-C.1 — critical-change probe used by sendDiffs to skip buildCurrentState
// on tier-cadence-skipped entities that have NO HP/death change. Returns false
// (no critical change, allow skip) for entities lacking BOTH stat components,
// so non-combat replicated entities (NPCs, dropped items) fall through to the
// normal tier cadence rather than being forced through buildCurrentState every
// tick.
//
// HARD CONTRACT: the encoding here MUST match buildCurrentState's currentHP +
// deathState assignments below. If buildCurrentState changes either field's
// derivation, this probe must change in lockstep — otherwise a far-tier mob
// could go silent on death because the probe and the canonical encoding
// disagree about what counts as "deathState changed". Cross-reference comments
// are placed at both sites.
namespace {
inline bool hasCriticalEntityChange(Entity* entity, const SvEntityUpdateMsg& last) {
    if (auto* charStats = entity->getComponent<CharacterStatsComponent>()) {
        // Mirrors buildCurrentState lines 587-588 and 612-613 (player branch).
        int32_t currentHP = static_cast<int32_t>(charStats->stats.currentHP);
        uint8_t deathState = charStats->stats.isAlive()
            ? 0
            : static_cast<uint8_t>(charStats->stats.lifeState);
        return currentHP != last.currentHP || deathState != last.deathState;
    }
    if (auto* enemyStats = entity->getComponent<EnemyStatsComponent>()) {
        // Mirrors buildCurrentState lines 595 and 615-617 (mob branch).
        int32_t currentHP = static_cast<int32_t>(enemyStats->stats.currentHP);
        uint8_t deathState = enemyStats->stats.isAlive ? 0 : static_cast<uint8_t>(2);
        return currentHP != last.currentHP || deathState != last.deathState;
    }
    return false; // non-combat entity (NPC, dropped item) — no HP/death to track
}

// Stage D.1b Phase 1 — distance bucket assignment from squared distance.
// Uses ≤ comparisons so each upper bound stays in the lower bucket; the
// boundary at 640 matches D.1's `d2 > actR2` semantics (640 stays in B3,
// 640.001 falls to B4). Production AOI deactivation is 1280 px so entries
// with d² > 1280² (=1,638,400) won't reach this loop; the final bucket
// returns 4 for any d² > 409600 without an explicit upper guard.
//   B0 = [0, 256]    px   d² ≤ 65536
//   B1 = (256, 384]  px   d² ≤ 147456
//   B2 = (384, 512]  px   d² ≤ 262144
//   B3 = (512, 640]  px   d² ≤ 409600
//   B4 = (640, 1280] px   d² >  409600
constexpr int distanceBucketFromD2(float d2) {
    if (d2 <= 65536.0f)  return 0;
    if (d2 <= 147456.0f) return 1;
    if (d2 <= 262144.0f) return 2;
    if (d2 <= 409600.0f) return 3;
    return 4;
}

// Stage D.1b Phase 1 — per-bucket hot-reason classifier. Order mirrors
// game/shared/mobai_scheduler.cpp::classifyMobLane exactly so the counters
// are reusable for the Phase 2 design conversation:
//   1. combat AIMode (Chase/ChaseMemory/Attack) → bucketHot
//   2. taunt active or pending decrement         → bucketForcedTarget
//   3. dearWarmedUp + lastTickInterval ≤ 0.001   → bucketHotProximity
//   4. mob with none of the above                → bucketCold
//   * no MobAIComponent (player, NPC, drop)      → bucketNonMob
// Each call increments exactly ONE counter; sums close to bucketStayed[i].
inline void incrementHotReasonBucketCounter(AOIStats& stats, int bucket, Entity* entity) {
    auto* aiComp = entity->getComponent<MobAIComponent>();
    if (!aiComp) {
        ++stats.bucketNonMob[bucket];
        return;
    }
    AIMode mode = aiComp->ai.getMode();
    if (mode == AIMode::Chase || mode == AIMode::ChaseMemory || mode == AIMode::Attack) {
        ++stats.bucketHot[bucket];
        return;
    }
    auto* esComp = entity->getComponent<EnemyStatsComponent>();
    if (esComp && (esComp->stats.forcedTarget != 0 || esComp->stats.forcedTargetTimer > 0.0f)) {
        ++stats.bucketForcedTarget[bucket];
        return;
    }
    if (aiComp->dearWarmedUp && aiComp->lastTickInterval <= 0.001f) {
        ++stats.bucketHotProximity[bucket];
        return;
    }
    ++stats.bucketCold[bucket];
}

} // namespace

void ReplicationManager::update(World& world, NetServer& server) {
    ++tickCounter_;
    ++stats_.ticks;
    // NOTE: Spatial index rebuild is commented out — it was rebuilt every tick
    // but never queried, wasting CPU. Phase 3a/3b re-enable scene + distance
    // AOI with 640/1280 hysteresis and a min-visible-time floor
    // (AOIConfig::minVisibleTicks) that prevents flicker at the boundary. The
    // previous 640/768 (128 px gap) was disabled in production for ~6 weeks
    // because it flickered. Phase 3c will bring the spatial index back online
    // for the candidate sweep — this commit keeps the linear handleToPid_
    // scan so the behavior fix ships before the perf change.
    // rebuildSpatialIndex(world);

    server.connections().forEach([&](ClientConnection& client) {
        // Admin observers authenticate without selecting a character, so
        // playerEntityId stays 0 but spectateScene drives what they see.
        // Skip only when neither path applies.
        if (client.playerEntityId == 0 && client.spectateScene.empty()) {
            ++stats_.clientSkippedNotPlaying;
            return;
        }
        ++stats_.clientsServed;
        buildVisibility(world, client);
        sendDiffs(world, server, client);

        // After the first replication tick following a scene change (or initial
        // login), notify the client that all initial entities have been sent.
        // The client holds its loading screen until this arrives.
        if (client.pendingScenePopulated) {
            client.pendingScenePopulated = false;
            server.sendTo(client.clientId, Channel::ReliableOrdered,
                          PacketType::SvScenePopulated, nullptr, 0);
        }
    });

    // Clear after all diffs are sent — unregisterEntity() populates this during the tick
    recentlyUnregistered_.clear();
}

void ReplicationManager::rebuildSpatialIndex(World& world) {
    spatialIndex_.beginRebuild(static_cast<uint32_t>(handleToPid_.size()));

    for (const auto& [handleValue, pid] : handleToPid_) {
        EntityHandle handle(handleValue);
        Entity* entity = world.getEntity(handle);
        if (!entity || !entity->isActive()) continue;

        auto* transform = entity->getComponent<Transform>();
        if (!transform) continue;

        // Use EntityHandle's packed value as EntityId for the spatial hash
        spatialIndex_.addEntity(handleValue, transform->position);
    }

    spatialIndex_.endRebuild();
}

void ReplicationManager::registerEntity(EntityHandle handle, PersistentId pid) {
    handleToPid_[handle.value] = pid;
    pidToHandle_[pid.value()] = handle;
    entitySeqCounters_[handle.value] = 0;
}

// IMPORTANT: Do NOT erase handleToPid_ without saving the PID first.
// sendDiffs() needs the PID to send SvEntityLeave for entities that left the AOI.
// If the PID is erased before sendDiffs runs, the leave message is silently skipped
// and the client keeps a zombie ghost entity forever.
void ReplicationManager::unregisterEntity(EntityHandle handle) {
    auto it = handleToPid_.find(handle.value);
    if (it != handleToPid_.end()) {
        recentlyUnregistered_[handle.value] = it->second;
        pidToHandle_.erase(it->second.value());
        handleToPid_.erase(it);
    }
    entitySeqCounters_.erase(handle.value);
}

PersistentId ReplicationManager::getPersistentId(EntityHandle handle) const {
    auto it = handleToPid_.find(handle.value);
    if (it != handleToPid_.end()) return it->second;
    return PersistentId::null();
}

EntityHandle ReplicationManager::getEntityHandle(PersistentId pid) const {
    auto it = pidToHandle_.find(pid.value());
    if (it != pidToHandle_.end()) return it->second;
    return EntityHandle{};
}

void ReplicationManager::buildVisibility(World& world, ClientConnection& client) {
    // Scene + distance filtered visibility. Phase 2 added the scene gate plus
    // the unclassified-leak counter; Phase 3a re-enables distance gating with
    // sticky hysteresis (640 / 1280); Phase 3b adds a min-visible floor that
    // suppresses boundary flicker.
    client.aoi.current.clear();

    // Resolve client scene + player position together — one player-entity
    // lookup feeds both the scene filter and the distance gate.
    // spectateScene (admin observer) overrides scene and bypasses distance.
    std::string clientScene;
    const bool isSpectator =
        (client.playerEntityId == 0 && !client.spectateScene.empty());
    bool bypassDistance = isSpectator;
    bool clientPositionResolved = false;
    Vec2 clientPos{};
    if (isSpectator) {
        clientScene = client.spectateScene;
    } else if (client.playerEntityId != 0) {
        auto pit = pidToHandle_.find(client.playerEntityId);
        if (pit != pidToHandle_.end()) {
            Entity* playerEntity = world.getEntity(pit->second);
            if (playerEntity) {
                if (auto* cs = playerEntity->getComponent<CharacterStatsComponent>()) {
                    clientScene = cs->stats.currentScene;
                    // Dead players have a frozen camera at the corpse. Bypass
                    // distance so the death-cam stays scene-wide and matches
                    // the sendDiffs isObserverClient tier handling.
                    if (cs->stats.isDead) bypassDistance = true;
                }
                if (auto* pt = playerEntity->getComponent<Transform>()) {
                    clientPos = pt->position;
                    clientPositionResolved = true;
                }
            }
        }
    }

    // Safety: if we have a player entity but couldn't resolve its scene,
    // something went wrong (entity destroyed, PID evicted). Send nothing
    // rather than bypassing the scene filter and flooding all entities.
    if (clientScene.empty() && client.playerEntityId != 0 && client.spectateScene.empty()) {
        ++stats_.clientSkippedNoScene;
        client.aoi.computeDiff();
        client.aoi.advance();
        return;
    }

    for (const auto& [handleValue, pid] : handleToPid_) {
        ++stats_.scanned;

        // Exclude the client's own player entity
        if (pid.value() == client.playerEntityId) {
            ++stats_.selfExcluded;
            continue;
        }

        // Verify entity still exists and is active
        Entity* entity = world.getEntity(EntityHandle(handleValue));
        if (!entity || !entity->isActive()) {
            ++stats_.entityMissingOrInactive;
            continue;
        }

        // Scene filter: only include entities in the same scene as the client.
        // Every entity MUST have a scene — empty sceneId means it was never
        // placed in a scene and must NOT leak to any client. NOTE: only
        // *scene-bearing* components gate by scene. A registered entity with
        // none of these four components currently passes the filter — the
        // unclassifiedSceneEntity counter measures the leak. Classification
        // policy is a separate decision (telemetry first).
        bool hadSceneComponent = false;
        bool sceneCulled       = false;
        if (!clientScene.empty()) {
            // Check mob scene (EnemyStatsComponent::sceneId)
            if (auto* es = entity->getComponent<EnemyStatsComponent>()) {
                hadSceneComponent = true;
                if (es->stats.sceneId != clientScene) sceneCulled = true;
            }

            // Check player scene (CharacterStatsComponent::currentScene)
            if (!sceneCulled) {
                if (auto* otherCs = entity->getComponent<CharacterStatsComponent>()) {
                    hadSceneComponent = true;
                    if (otherCs->stats.currentScene != clientScene) sceneCulled = true;
                }
            }

            // Check NPC scene (NPCComponent::sceneId)
            if (!sceneCulled) {
                if (auto* npc = entity->getComponent<NPCComponent>()) {
                    hadSceneComponent = true;
                    if (npc->sceneId != clientScene) sceneCulled = true;
                }
            }

            // Check dropped item scene (DroppedItemComponent::sceneId)
            if (!sceneCulled) {
                if (auto* drop = entity->getComponent<DroppedItemComponent>()) {
                    hadSceneComponent = true;
                    if (drop->sceneId != clientScene) sceneCulled = true;
                }
            }
        }
        if (sceneCulled) {
            ++stats_.sceneCulled;
            continue;
        }
        if (!hadSceneComponent) ++stats_.unclassifiedSceneEntity;

        // Phase 3a — distance gate (after scene, before visibility filter).
        // Spectators and dead players bypass entirely; normal clients with
        // no resolved Transform fail CLOSED so we never silent-fall back to
        // a scene-wide flood.
        if (!bypassDistance) {
            if (!clientPositionResolved) {
                ++stats_.missingPlayerPosition;
                continue;
            }
            auto* etrans = entity->getComponent<Transform>();
            if (!etrans) {
                ++stats_.missingEntityPosition;
                continue;
            }
            const float dx = etrans->position.x - clientPos.x;
            const float dy = etrans->position.y - clientPos.y;
            const float d2 = dx * dx + dy * dy;
            // aoi.previous is sorted by the prior computeDiff() — binary
            // search is the cheap way to read the sticky bit.
            const bool wasVisible = std::binary_search(
                client.aoi.previous.begin(),
                client.aoi.previous.end(),
                EntityHandle(handleValue));
            const float r = wasVisible ? aoiConfig_.deactivationRadius
                                       : aoiConfig_.activationRadius;
            if (d2 > r * r) {
                if (wasVisible) {
                    // Phase 3b — anti-flap floor. Hold the entity if it has
                    // not been visible long enough yet. Distance-driven leave
                    // is suppressed; the entity falls through and is admitted.
                    auto fsi = client.firstSeenTick.find(handleValue);
                    if (fsi != client.firstSeenTick.end() &&
                        tickCounter_ - fsi->second < aoiConfig_.minVisibleTicks) {
                        ++stats_.minVisibleHeld;
                        // fall-through: admit anyway
                    } else {
                        ++stats_.distanceCulledHadPrevious;
                        continue;
                    }
                } else {
                    ++stats_.distanceCulledNoPrevious;
                    continue;
                }
            } else if (wasVisible &&
                       d2 > aoiConfig_.activationRadius * aoiConfig_.activationRadius) {
                // Strictly in [activation, deactivation] — sticky bit kept the
                // entity admitted across the band. wasVisible entities still
                // inside activation are not "sticky"; they would be admitted
                // by the activation radius alone, so they do NOT increment
                // this counter (otherwise /aoi_stats sticky drowns in noise
                // from normal near entities).
                ++stats_.stickyHeld;
            }
        }

        // Custom visibility filter (e.g. GM invisibility)
        if (visibilityFilter && visibilityFilter(pid.value(), client)) {
            ++stats_.visibilityFilterCulled;
            continue;
        }

        ++stats_.admitted;
        client.aoi.current.push_back(EntityHandle(handleValue));
    }

    client.aoi.computeDiff();
    stats_.entered += client.aoi.entered.size();
    stats_.left    += client.aoi.left.size();
    stats_.stayed  += client.aoi.stayed.size();
    client.aoi.advance();
}

void ReplicationManager::sendDiffs(World& world, NetServer& server, ClientConnection& client) {
    // v9: coalesce entity-enter bursts into MAX_PAYLOAD_SIZE-budgeted
    // SvEntityEnterBatch packets. Scene entry with 231 WhisperingWoods mobs
    // used to emit 231 individual ReliableOrdered packets in one tick, which
    // even with a 64-bit ACK window + CmdAckExtended would still generate
    // wasted retransmit bandwidth. Pattern mirrors the SvEntityUpdateBatch
    // flush at sendDiffs() below: fill until the next entity won't fit,
    // flush, start a new batch. ~231 × ~100 B → ~20 batch packets, a ~12×
    // reduction that fits comfortably inside the widened ACK window.
    uint8_t batchBuf[MAX_PAYLOAD_SIZE];
    ByteWriter batchWriter(batchBuf, sizeof(batchBuf));
    batchWriter.writeU16(0); // count — patched at flush time
    uint16_t batchCount = 0;

    auto flushEnterBatch = [&]() {
        if (batchCount == 0) return;
        // Patch the count (first 2 bytes) little-endian.
        batchBuf[0] = static_cast<uint8_t>(batchCount & 0xFF);
        batchBuf[1] = static_cast<uint8_t>((batchCount >> 8) & 0xFF);
        server.sendTo(client.clientId, Channel::ReliableOrdered,
                      PacketType::SvEntityEnterBatch,
                      batchWriter.data(), batchWriter.size());
        batchWriter = ByteWriter(batchBuf, sizeof(batchBuf));
        batchWriter.writeU16(0);
        batchCount = 0;
    };

    for (const auto& handle : client.aoi.entered) {
        PersistentId pid = getPersistentId(handle);
        if (pid.isNull()) continue;

        Entity* entity = world.getEntity(handle);
        if (!entity) continue;

        SvEntityEnterMsg enterMsg = buildEnterMessage(world, entity, pid);

        // Serialize into a scratch buffer to measure the encoded size.
        // SvEntityEnterMsg has variable length (strings, per-type branches),
        // so we can't size-check a priori — we have to encode once to know.
        uint8_t scratch[MAX_PAYLOAD_SIZE];
        ByteWriter scratchWriter(scratch, sizeof(scratch));
        enterMsg.write(scratchWriter);
        if (scratchWriter.overflowed()) {
            // Single entity exceeds MAX_PAYLOAD_SIZE — shouldn't happen with
            // current schema (largest realistic payload is ~300 bytes) but
            // bail gracefully rather than corrupt the batch.
            continue;
        }
        size_t entrySize = scratchWriter.size();

        // Flush if appending this entry would overflow the batch. +0 because
        // the count prefix was reserved up front.
        if (batchWriter.size() + entrySize > sizeof(batchBuf)) {
            flushEnterBatch();
        }
        batchWriter.writeBytes(scratch, entrySize);
        batchCount++;

        // Initialize last sent state
        auto state = buildCurrentState(world, entity, pid);
        state.updateSeq = entitySeqCounters_[handle.value];
        client.lastSentState[pid.value()] = state;

        // Phase 3b — record first-seen tick for the min-visible floor.
        // buildVisibility consults this map to suppress distance-driven
        // leaves until the floor elapses.
        client.firstSeenTick[handle.value] = tickCounter_;
    }
    flushEnterBatch();

    // S141 Phase B.1 — coalesce leaves into SvEntityLeaveBatch. Pre-batch, a
    // respawn cycle of 99 mobs in a single tick fanned out 99 individual
    // ReliableOrdered SvEntityLeave packets — each ~12 bytes after AEAD but
    // each consumed a pending-queue slot. Format: u16 count + N × u64 pid.
    // Per-leave footprint = 8 bytes; with MAX_PAYLOAD_SIZE the batch can
    // carry ~146 leaves at once.
    constexpr size_t kPerLeaveBytes = 8;
    uint8_t  leaveBuf[MAX_PAYLOAD_SIZE];
    ByteWriter leaveWriter(leaveBuf, sizeof(leaveBuf));
    leaveWriter.writeU16(0);  // count placeholder, patched at flush time
    uint16_t leaveCount = 0;

    auto flushLeaveBatch = [&]() {
        if (leaveCount == 0) return;
        leaveBuf[0] = static_cast<uint8_t>(leaveCount & 0xFF);
        leaveBuf[1] = static_cast<uint8_t>((leaveCount >> 8) & 0xFF);
        server.sendTo(client.clientId, Channel::ReliableOrdered,
                      PacketType::SvEntityLeaveBatch,
                      leaveWriter.data(), leaveWriter.size());
        leaveWriter = ByteWriter(leaveBuf, sizeof(leaveBuf));
        leaveWriter.writeU16(0);
        leaveCount = 0;
    };

    // Process left entities
    for (const auto& handle : client.aoi.left) {
        PersistentId pid = getPersistentId(handle);
        const bool wasUnregistered = pid.isNull();
        // Fallback: entity was unregistered this tick (pickup/despawn) — PID already erased
        if (pid.isNull()) {
            auto uit = recentlyUnregistered_.find(handle.value);
            if (uit != recentlyUnregistered_.end()) {
                pid = uit->second;
                ++stats_.recentlyUnregisteredFallback;
            }
        }

        // Phase 3b — when an unregistered-leave fires for an entity that was
        // still inside its min-visible window, count it. Despawn always
        // bypasses the floor (handle vanishes from handleToPid_ and never
        // reaches the buildVisibility distance branch); this counter surfaces
        // the operator-meaningful subset where the floor would have applied.
        if (wasUnregistered) {
            auto fsi = client.firstSeenTick.find(handle.value);
            if (fsi != client.firstSeenTick.end() &&
                tickCounter_ - fsi->second < aoiConfig_.minVisibleTicks) {
                ++stats_.forcedLeaveDespawn;
            }
        }
        // Always erase firstSeenTick on leave so a re-registered handle
        // starts a fresh window.
        client.firstSeenTick.erase(handle.value);

        if (pid.isNull()) continue;

        // Flush before appending if next entry would overflow.
        if (leaveWriter.size() + kPerLeaveBytes > sizeof(leaveBuf)) {
            flushLeaveBatch();
        }
        // Inline-encode the persistent id (matches SvEntityLeaveMsg::write).
        detail::writeU64(leaveWriter, pid.value());
        ++leaveCount;

        client.lastSentState.erase(pid.value());
        client.lastEmittedBucket.erase(pid.value());
    }
    flushLeaveBatch();

    // Process stayed entities (delta updates) — batched into single packets

    // Compute client player position once for tier checks.  Also detect dead
    // players so we can treat them like observers (camera frozen at the
    // corpse — without this fallthrough, mobs that wander >40 tiles from the
    // death point drop to Mid/Far/Edge tiers and the death view becomes
    // choppy at 2-7Hz instead of the normal 20Hz).
    Vec2 clientPos{};
    bool clientIsDead = false;
    {
        auto clientHandle = getEntityHandle(PersistentId(client.playerEntityId));
        Entity* clientEntity = world.getEntity(clientHandle);
        if (clientEntity) {
            auto* ct = clientEntity->getComponent<Transform>();
            if (ct) clientPos = ct->position;
            auto* cs = clientEntity->getComponent<CharacterStatsComponent>();
            if (cs && cs->stats.isDead) clientIsDead = true;
        }
    }
    // Admin observers have no player entity, so clientPos defaults to (0,0).
    // That would put every distant mob in the Edge tier and choke update rate.
    // Treat observed-scene entities as Near so spectators see smooth motion.
    // Dead players get the same fallthrough -- their camera is frozen at the
    // death spot so they're effectively spectating until respawn.
    const bool isObserverClient =
        (client.playerEntityId == 0 && !client.spectateScene.empty())
        || clientIsDead;

    // Batch buffer: accumulate multiple entity deltas into one packet.
    // Renamed (deltaBuf/deltaWriter/deltaCount) to not collide with the
    // v9 enter-batch declared at the top of this function.
    uint8_t deltaBuf[MAX_PAYLOAD_SIZE];
    ByteWriter deltaWriter(deltaBuf, sizeof(deltaBuf));
    // Reserve 1 byte for entity count at the start
    deltaWriter.writeU8(0);
    uint8_t deltaCount = 0;

    auto flushBatch = [&]() {
        if (deltaCount == 0) return;
        // Patch the count byte at position 0
        deltaBuf[0] = deltaCount;
        server.sendTo(client.clientId, Channel::Unreliable,
                      PacketType::SvEntityUpdateBatch,
                      deltaWriter.data(), deltaWriter.size());
        // Reset for next batch
        deltaWriter = ByteWriter(deltaBuf, sizeof(deltaBuf));
        deltaWriter.writeU8(0);
        deltaCount = 0;
    };

    for (const auto& handle : client.aoi.stayed) {
        PersistentId pid = getPersistentId(handle);
        if (pid.isNull()) continue;

        Entity* entity = world.getEntity(handle);
        if (!entity) continue;

        // Look up last sent state up front — needed by both the cadence-skip
        // probe and the post-build dirtyMask comparison. First-tick entities
        // (no last state yet) shouldn't be reaching the stayed set; bail.
        auto lastIt = client.lastSentState.find(pid.value());
        if (lastIt == client.lastSentState.end()) continue;
        const SvEntityUpdateMsg& last = lastIt->second;

        // S143-C.1 — tier-cadence skip BEFORE buildCurrentState. The probe
        // mirrors buildCurrentState's HP+deathState encoding so death/HP
        // updates always escape the cadence skip (without that override, a
        // dead mob on Mid/Far/Edge would stay alive on the client until the
        // next eligible tick — see the prior in-line override that this
        // hoist replaces). Non-combat entities (no stat components) fall
        // through to the cadence skip via hasCriticalEntityChange()=false.
        //
        // Stage D.1 — under production AOI (deactivationRadius=1280, all
        // visible entities are Near tier so the tier-cadence is dead code
        // here) we add a sticky-band sub-tier AFTER this block. The dead
        // tier-cadence is preserved as-is because test_replication_tier_skip
        // widens AOI to exercise it; touching it here would force those
        // tests to be rewritten without buying anything in production.
        // Critical override semantics for the sub-tier:
        //   * HP + deathState are the only fields that always bypass.
        //     statusEffectMask, casting fields, targetEntityId all ride this
        //     packet but are non-critical at 20-40 tile range:
        //       statusEffectMask wire field is currently unconsumed client-
        //         side (TODO @ game_app.cpp:600); SvBuffSync (0xC1)
        //         carries the owning client's full mask separately.
        //       castingSkillId / castingProgress are deferred (always 0).
        //       targetEntityId for distant mobs is non-critical.
        //     Telegraphs ride SvSkillResultBatch (0xEC) /
        //     SvAOETelegraph (0xEE/0xEF), combat events ride
        //     SvCombatEventMsg (0x93) — sub-tier never throttles those.
        auto* entityTransform = entity->getComponent<Transform>();
        bool cadenceOverridden = false;
        // D.1b Phase 1 — bucket sentinel. -1 means the observer / no-Transform
        // branch took the entry; bucket counters and POST-DIRTY counters skip
        // those entries because there is no anchor distance to bucket against.
        int bucket = -1;
        if (entityTransform && !isObserverClient) {
            float dx = entityTransform->position.x - clientPos.x;
            float dy = entityTransform->position.y - clientPos.y;
            float d2 = dx * dx + dy * dy;
            float dist = std::sqrt(d2);
            UpdateTier tier = getUpdateTier(dist);
            // D.1b Phase 1 — bucket assignment + per-entity hot-reason counter,
            // BEFORE the cadence gate so a tier-cadence-throttled entity still
            // contributes to bucketStayed and the hot-reason mix.
            bucket = distanceBucketFromD2(d2);
            ++stats_.bucketStayed[bucket];
            incrementHotReasonBucketCounter(stats_, bucket, entity);
            if (!shouldSendUpdate(tier, tickCounter_)) {
                if (!hasCriticalEntityChange(entity, last)) {
                    ++stats_.stayedThrottled;
                    ++stats_.bucketCadenceThrottled[bucket];
                    continue;
                }
                ++stats_.stayedCriticalOverride;
                ++stats_.bucketCadenceCriticalOverride[bucket];
                cadenceOverridden = true;
            }

            // Stage D.1 — sticky-band sub-tier. Inside activationRadius =
            // full-rate. In [activation, deactivation] = every kStickyInterval
            // -th tick, with deterministic per-handle offset so band entities
            // de-herd into different frames. HP/death bypass via the same
            // probe used by tier-cadence above. Skipped if tier-cadence
            // already overrode (we don't want to re-throttle an emit that
            // was already promoted via the dead path under widened-AOI tests).
            if (!cadenceOverridden) {
                const float actR = aoiConfig_.activationRadius;
                const float actR2 = actR * actR;
                if (d2 > actR2) {
                    constexpr uint32_t kStickyInterval = 3u;
                    const uint32_t offset = handle.value % kStickyInterval;
                    if ((tickCounter_ % kStickyInterval) != offset) {
                        if (!hasCriticalEntityChange(entity, last)) {
                            ++stats_.stayedThrottled;
                            ++stats_.bucketCadenceThrottled[bucket];
                            continue;
                        }
                        ++stats_.stayedCriticalOverride;
                        ++stats_.bucketCadenceCriticalOverride[bucket];
                    } else {
                        // On-offset: cadence permits emit consideration this
                        // tick. The post-build dirtyMask check downstream still
                        // decides whether bytes go on the wire — see AOIStats
                        // doc-comment in replication.h.
                        ++stats_.stayedFullRate;
                        ++stats_.bucketCadenceFullRate[bucket];
                    }
                } else {
                    // Inside activation radius — full-rate.
                    ++stats_.stayedFullRate;
                    ++stats_.bucketCadenceFullRate[bucket];
                }
            }
        } else {
            // No Transform OR observer/dead-camera client. Both bypass the
            // cadence pipeline by design; count as full-rate emit. Bucket
            // counters skipped (bucket == -1) — no anchor distance.
            ++stats_.stayedFullRate;
        }

        SvEntityUpdateMsg current = buildCurrentState(world, entity, pid);

        uint32_t dirtyMask = 0;

        if (current.position.x != last.position.x || current.position.y != last.position.y)
            dirtyMask |= (1 << 0);
        if (current.animFrame != last.animFrame)
            dirtyMask |= (1 << 1);
        if (current.flipX != last.flipX)
            dirtyMask |= (1 << 2);
        if (current.currentHP != last.currentHP)
            dirtyMask |= (1 << 3);
        if (current.maxHP != last.maxHP)
            dirtyMask |= (1 << 4);
        if (current.moveState != last.moveState)
            dirtyMask |= (1 << 5);
        if (current.animId != last.animId)
            dirtyMask |= (1 << 6);
        if (current.statusEffectMask != last.statusEffectMask)
            dirtyMask |= (1 << 7);
        if (current.deathState != last.deathState)
            dirtyMask |= (1 << 8);
        if (current.castingSkillId != last.castingSkillId || current.castingProgress != last.castingProgress)
            dirtyMask |= (1 << 9);
        if (current.targetEntityId != last.targetEntityId)
            dirtyMask |= (1 << 10);
        if (current.level != last.level)
            dirtyMask |= (1 << 11);
        if (current.faction != last.faction)
            dirtyMask |= (1 << 12);
        if (current.armorStyle != last.armorStyle ||
            current.hatStyle != last.hatStyle ||
            current.weaponStyle != last.weaponStyle)
            dirtyMask |= (1 << 13);
        if (current.pkStatus != last.pkStatus)
            dirtyMask |= (1 << 14);
        if (current.honorRank != last.honorRank)
            dirtyMask |= (1 << 15);
        if (current.costumeVisuals != last.costumeVisuals)
            dirtyMask |= (1 << 16);

        if (dirtyMask == 0) continue; // Nothing changed

        // D.1b Phase 1 — POST-DIRTY bucket counter. Increments only after
        // the dirtyMask filter passes; the next site (after tmpWriter is
        // built) accumulates the actual serialized payload contribution to
        // 0xBA SvEntityUpdateBatch. observer/no-Transform skipped (bucket=-1).
        if (bucket >= 0) {
            ++stats_.bucketDirtyMaskNonzero[bucket];
            // D.1b Phase 2 (Fork B) — per-field histogram. Each set bit in
            // the SvEntityUpdate fieldMask increments its bucket slot,
            // identifying which fields contribute to the per-bucket byte
            // share. Bit indices match SvEntityUpdateMsg's protocol.h
            // layout (0=position, ..., 16=costumeVisuals).
            for (int bit = 0; bit < 17; ++bit) {
                if (dirtyMask & (1u << bit)) {
                    ++stats_.bucketDirtyByField[bucket][bit];
                }
            }
            // D.1b Phase 2 (Fork D) — band->inner cascade telemetry. Record
            // the transition from the entity's previous emit bucket to the
            // current emit bucket so the operator can spot D.1's sticky-band
            // refresh-stalling cascading into fat inner-bucket emits.
            auto lbIt = client.lastEmittedBucket.find(pid.value());
            if (lbIt != client.lastEmittedBucket.end()) {
                uint8_t prev = lbIt->second;
                if (prev < 5) {
                    ++stats_.bucketTransitions[prev][bucket];
                }
            }
            client.lastEmittedBucket[pid.value()] = static_cast<uint8_t>(bucket);
        }

        uint8_t& seq = entitySeqCounters_[handle.value];
        seq++; // wraps naturally at 255->0

        SvEntityUpdateMsg deltaMsg;
        deltaMsg.persistentId    = pid.value();
        deltaMsg.fieldMask       = dirtyMask;
        deltaMsg.position        = current.position;
        deltaMsg.animFrame       = current.animFrame;
        deltaMsg.flipX           = current.flipX;
        deltaMsg.currentHP       = current.currentHP;
        deltaMsg.maxHP           = current.maxHP;
        deltaMsg.moveState       = current.moveState;
        deltaMsg.animId          = current.animId;
        deltaMsg.statusEffectMask = current.statusEffectMask;
        deltaMsg.deathState      = current.deathState;
        deltaMsg.castingSkillId  = current.castingSkillId;
        deltaMsg.castingProgress = current.castingProgress;
        deltaMsg.targetEntityId  = current.targetEntityId;
        deltaMsg.level           = current.level;
        deltaMsg.faction         = current.faction;
        deltaMsg.armorStyle      = current.armorStyle;
        deltaMsg.hatStyle        = current.hatStyle;
        deltaMsg.weaponStyle     = current.weaponStyle;
        deltaMsg.pkStatus        = current.pkStatus;
        deltaMsg.honorRank       = current.honorRank;
        deltaMsg.costumeVisuals  = current.costumeVisuals;
        deltaMsg.updateSeq       = seq;

        // Serialize delta into a temp buffer to check size
        uint8_t tmpBuf[MAX_PAYLOAD_SIZE];
        ByteWriter tmpWriter(tmpBuf, sizeof(tmpBuf));
        deltaMsg.write(tmpWriter);

        // D.1b Phase 1 — POST-DIRTY bucket emitted-bytes counter. tmpWriter
        // size is the per-entry serialized payload contribution to
        // 0xBA SvEntityUpdateBatch (excluding batch headers added at flush).
        // observer/no-Transform skipped (bucket=-1).
        if (bucket >= 0) stats_.bucketEmittedBytes[bucket] += tmpWriter.size();

        // If this delta won't fit in the current batch, flush first
        if (deltaWriter.size() + tmpWriter.size() > MAX_PAYLOAD_SIZE) {
            flushBatch();
        }

        // Write delta into batch buffer
        deltaMsg.write(deltaWriter);
        deltaCount++;

        // Update last sent state
        lastIt->second = current;
        lastIt->second.updateSeq = seq;
    }

    // Flush remaining deltas
    flushBatch();
}

SvEntityEnterMsg ReplicationManager::buildEnterMessage(World& world, Entity* entity, PersistentId pid) {
    SvEntityEnterMsg msg;
    msg.persistentId = pid.value();

    // Position from Transform
    auto* transform = entity->getComponent<Transform>();
    if (transform) {
        msg.position = transform->position;
    }

    // Determine entity type and fill type-specific fields
    auto* charStats = entity->getComponent<CharacterStatsComponent>();
    auto* enemyStats = entity->getComponent<EnemyStatsComponent>();
    auto* npcComp = entity->getComponent<NPCComponent>();
    auto* droppedItem = entity->getComponent<DroppedItemComponent>();

    if (charStats) {
        msg.entityType = 0; // player
        msg.level = charStats->stats.level;
        msg.currentHP = charStats->stats.currentHP;
        msg.maxHP = charStats->stats.maxHP;
        msg.pkStatus  = static_cast<uint8_t>(charStats->stats.pkStatus);
        msg.honorRank = static_cast<uint8_t>(HonorSystem::getHonorRank(charStats->stats.honor));
        // v24 — persistent DB id for context-menu action targeting. Server
        // handlers (CmdTrade/CmdParty/CmdSocial/CmdGuild) resolve targets by
        // character_id; without this on enter the client could only send
        // display name, which never matched the server-side equality test
        // and silently dropped every social action.
        msg.characterId = charStats->stats.characterId;

        auto* appearance = entity->getComponent<AppearanceComponent>();
        if (appearance) {
            msg.gender    = appearance->gender;
            msg.hairstyle = appearance->hairstyle;
        }

        auto* nameplate = entity->getComponent<NameplateComponent>();
        if (nameplate) {
            msg.name      = nameplate->displayName;
            msg.adminRole = nameplate->roleTier;
        }

        auto* guildComp = entity->getComponent<GuildComponent>();
        if (guildComp && guildComp->guild.guildId > 0) {
            msg.guildName     = guildComp->guild.guildName;
            msg.guildIconPath = guildComp->guild.guildIconPath;
        }

        auto* costumeComp = entity->getComponent<CostumeComponent>();
        if (costumeComp && costumeComp->showCostumes && costumeCache_) {
            uint16_t cW = 0, cA = 0, cH = 0, cS = 0, cG = 0, cB = 0;
            for (const auto& [slot, defId] : costumeComp->equippedBySlot) {
                const auto* def = costumeCache_->get(defId);
                if (!def) continue;
                switch (static_cast<EquipmentSlot>(slot)) {
                    case EquipmentSlot::Weapon:    cW = def->visualIndex; break;
                    case EquipmentSlot::Armor:     cA = def->visualIndex; break;
                    case EquipmentSlot::Hat:       cH = def->visualIndex; break;
                    case EquipmentSlot::SubWeapon: cS = def->visualIndex; break;
                    case EquipmentSlot::Gloves:    cG = def->visualIndex; break;
                    case EquipmentSlot::Shoes:     cB = def->visualIndex; break;
                    default: break;
                }
            }
            msg.costumeVisuals = packCostumeVisuals(cW, cA, cH, cS, cG, cB);
        }

        // v23 — equipment visual styles from inventory. Same lookup as
        // buildCurrentState's bit-13 path; needed on enter because the delta
        // path's first dirtyMask compare (current vs lastSentState seeded
        // here at enter) sees no change and never emits these strings.
        auto* invComp = entity->getComponent<InventoryComponent>();
        if (invComp && itemDefCache_) {
            const auto& equip = invComp->inventory.getEquipmentMap();
            auto getStyle = [&](EquipmentSlot slot) -> std::string {
                auto it = equip.find(slot);
                if (it != equip.end() && it->second.isValid())
                    return itemDefCache_->getVisualStyle(it->second.itemId);
                return "";
            };
            msg.weaponStyle = getStyle(EquipmentSlot::Weapon);
            msg.armorStyle  = getStyle(EquipmentSlot::Armor);
            msg.hatStyle    = getStyle(EquipmentSlot::Hat);
        }
    } else if (enemyStats) {
        msg.entityType = 1; // mob
        msg.level = enemyStats->stats.level;
        msg.currentHP = enemyStats->stats.currentHP;
        msg.maxHP = enemyStats->stats.maxHP;

        auto* mobNameplate = entity->getComponent<MobNameplateComponent>();
        if (mobNameplate) {
            msg.name = mobNameplate->displayName;
        }
        msg.mobDefId = enemyStats->stats.enemyId;  // enemyId stores the mob_def_id
        msg.isBoss   = enemyStats->stats.isBoss ? 1 : 0;

        // Guard paper doll: if mob has AppearanceComponent, send equipment visuals
        auto* mobAppearance = entity->getComponent<AppearanceComponent>();
        if (mobAppearance) {
            msg.hasAppearance = 1;
            msg.mobGender     = mobAppearance->gender;
            msg.mobHairstyle  = mobAppearance->hairstyle;
            msg.mobArmorStyle  = mobAppearance->armorStyle;
            msg.mobHatStyle    = mobAppearance->hatStyle;
            msg.mobWeaponStyle = mobAppearance->weaponStyle;
        }
    } else if (npcComp) {
        msg.entityType = 2; // npc
        msg.name = npcComp->displayName;
        // WU11 Phase B Session 62 — replicate NPC identity + faction-target allow-list
        // so the client can enforce same-faction click rules without a DB roundtrip.
        msg.npcId = npcComp->npcId;
        msg.npcStringId = npcComp->npcStringId;
        msg.targetFactions = npcComp->targetFactions;
    } else if (droppedItem) {
        msg.entityType = 3; // dropped item
        msg.name = droppedItem->isGold ? "Gold" : droppedItem->itemId;
        msg.itemId = droppedItem->itemId;
        msg.quantity = droppedItem->quantity;
        msg.isGold = droppedItem->isGold ? 1 : 0;
        msg.goldAmount = droppedItem->goldAmount;
        msg.enchantLevel = droppedItem->enchantLevel;
        msg.rarity = droppedItem->rarity;
    }

    // Faction
    auto* factionComp = entity->getComponent<FactionComponent>();
    if (factionComp) {
        msg.faction = static_cast<uint8_t>(factionComp->faction);
    }

    return msg;
}

SvEntityUpdateMsg ReplicationManager::buildCurrentState(World& world, Entity* entity, PersistentId pid) {
    SvEntityUpdateMsg msg;
    msg.persistentId = pid.value();
    msg.fieldMask = 0x1FFFF; // bits 0-16 all set

    // Position
    auto* transform = entity->getComponent<Transform>();
    if (transform) {
        msg.position = transform->position;
    }

    // Animation frame and flip
    auto* sprite = entity->getComponent<SpriteComponent>();
    if (sprite) {
        msg.animFrame = static_cast<uint8_t>(sprite->currentFrame);
        msg.flipX = sprite->flipX ? 1 : 0;
    }

    // HP, maxHP, level. NOTE: hasCriticalEntityChange() in the anonymous
    // namespace above mirrors the currentHP + deathState encoding from this
    // method. Any change here MUST be reflected there, or the cadence-skip
    // probe will go out of sync with the canonical encoding and dropped HP/
    // death transitions on Mid/Far/Edge entities will reappear.
    auto* charStats = entity->getComponent<CharacterStatsComponent>();
    if (charStats) {
        msg.currentHP = charStats->stats.currentHP;
        msg.maxHP     = charStats->stats.maxHP;
        msg.level     = static_cast<uint8_t>(charStats->stats.level);
        msg.pkStatus  = static_cast<uint8_t>(charStats->stats.pkStatus);
        msg.honorRank = static_cast<uint8_t>(HonorSystem::getHonorRank(charStats->stats.honor));
    } else {
        auto* enemyStats = entity->getComponent<EnemyStatsComponent>();
        if (enemyStats) {
            msg.currentHP = enemyStats->stats.currentHP;
            msg.maxHP     = enemyStats->stats.maxHP;
            msg.level     = static_cast<uint8_t>(enemyStats->stats.level);
        }
    }

    // Faction
    auto* factionComp = entity->getComponent<FactionComponent>();
    if (factionComp) {
        msg.faction = static_cast<uint8_t>(factionComp->faction);
    }

    // Status effect mask from active effects
    auto* seComp = entity->getComponent<StatusEffectComponent>();
    // Phase 7.2b.0: getActiveEffectMask returns uint64; the wire field is
    // uint32 (currently unconsumed client-side — only TODO references in
    // game_app.cpp). Truncate to the low 32 bits — high-bit EffectType values
    // (Silence, Blind, AttackSpeedSlow, ...) won't appear in this wire field.
    // SvBuffSync (vector-based, no truncation) carries the full set to the
    // owning client's buff bar, so the truncation only impacts the
    // not-yet-consumed nameplate-icon path. When that path goes live, widen
    // the wire field too — that's a PROTOCOL_VERSION bump.
    msg.statusEffectMask = seComp
        ? static_cast<uint32_t>(seComp->effects.getActiveEffectMask() & 0xFFFFFFFFu)
        : 0;

    // Death state from character/enemy stats
    if (charStats) {
        msg.deathState = charStats->stats.isAlive() ? 0 : static_cast<uint8_t>(charStats->stats.lifeState); // 0=alive, 1=dying, 2=dead
    } else {
        auto* es2 = entity->getComponent<EnemyStatsComponent>();
        msg.deathState = (es2 && !es2->stats.isAlive) ? 2 : 0;
    }

    // moveState: walking/idle from PlayerController or MobAI
    auto* pc = entity->getComponent<PlayerController>();
    auto* mobAi = entity->getComponent<MobAIComponent>();
    if (pc) {
        msg.moveState = pc->isMoving ? static_cast<uint8_t>(MoveState::Walking)
                                     : static_cast<uint8_t>(MoveState::Idle);
    } else if (mobAi) {
        auto mode = mobAi->ai.getMode();
        msg.moveState = (mode == AIMode::Chase || mode == AIMode::ReturnHome || mode == AIMode::Roam)
                        ? static_cast<uint8_t>(MoveState::Walking)
                        : static_cast<uint8_t>(MoveState::Idle);
    }

    // animId: direction + animation type
    // Note: animType 2=attack and 3=cast are deferred until server tracks attack/cast state per-entity
    {
        uint8_t animDir = 0;
        uint8_t animType = msg.moveState; // 0=idle, 1=walk maps directly
        if (pc) {
            animDir = facingToAnimDir(pc->facing);
        } else if (mobAi) {
            animDir = facingToAnimDir(mobAi->ai.getFacingDirection());
        }
        // Death override
        if (msg.deathState >= 2) {
            msg.animId = 12; // death animation
        } else {
            msg.animId = encodeAnimId(animDir, animType);
        }
    }

    // castingSkillId + castingProgress: deferred (no server-side cast times yet)
    msg.castingSkillId  = 0;
    msg.castingProgress = 0;

    // targetEntityId: from TargetingComponent
    auto* targeting = entity->getComponent<TargetingComponent>();
    msg.targetEntityId = targeting ? static_cast<uint16_t>(targeting->selectedTargetId & 0xFFFF) : 0;

    // Equipment visual styles
    auto* invComp = entity->getComponent<InventoryComponent>();
    if (invComp && itemDefCache_) {
        const auto& equip = invComp->inventory.getEquipmentMap();
        auto getStyle = [&](EquipmentSlot slot) -> std::string {
            auto it = equip.find(slot);
            if (it != equip.end() && it->second.isValid())
                return itemDefCache_->getVisualStyle(it->second.itemId);
            return "";
        };
        msg.weaponStyle = getStyle(EquipmentSlot::Weapon);
        msg.armorStyle  = getStyle(EquipmentSlot::Armor);
        msg.hatStyle    = getStyle(EquipmentSlot::Hat);
    } else {
        msg.weaponStyle.clear();
        msg.armorStyle.clear();
        msg.hatStyle.clear();
    }

    // costumeVisuals: packed costume visual indices (6 slots)
    auto* costumeComp = entity->getComponent<CostumeComponent>();
    if (costumeComp && costumeComp->showCostumes && costumeCache_) {
        uint16_t cWeapon = 0, cArmor = 0, cHat = 0, cShield = 0, cGloves = 0, cBoots = 0;
        for (const auto& [slot, defId] : costumeComp->equippedBySlot) {
            const auto* def = costumeCache_->get(defId);
            if (!def) continue;
            switch (static_cast<EquipmentSlot>(slot)) {
                case EquipmentSlot::Weapon:    cWeapon = def->visualIndex; break;
                case EquipmentSlot::Armor:     cArmor  = def->visualIndex; break;
                case EquipmentSlot::Hat:       cHat    = def->visualIndex; break;
                case EquipmentSlot::SubWeapon: cShield = def->visualIndex; break;
                case EquipmentSlot::Gloves:    cGloves = def->visualIndex; break;
                case EquipmentSlot::Shoes:     cBoots  = def->visualIndex; break;
                default: break;
            }
        }
        msg.costumeVisuals = packCostumeVisuals(cWeapon, cArmor, cHat, cShield, cGloves, cBoots);
    } else {
        msg.costumeVisuals = 0;
    }

    return msg;
}

#else
// Stubs when game code is not available
void ReplicationManager::update(World&, NetServer&) {}
void ReplicationManager::rebuildSpatialIndex(World&) {}
void ReplicationManager::registerEntity(EntityHandle, PersistentId) {}
void ReplicationManager::unregisterEntity(EntityHandle) {}
PersistentId ReplicationManager::getPersistentId(EntityHandle) const { return PersistentId::null(); }
EntityHandle ReplicationManager::getEntityHandle(PersistentId) const { return EntityHandle{}; }
void ReplicationManager::buildVisibility(World&, ClientConnection&) {}
void ReplicationManager::sendDiffs(World&, NetServer&, ClientConnection&) {}
SvEntityEnterMsg ReplicationManager::buildEnterMessage(World&, Entity*, PersistentId) { return {}; }
SvEntityUpdateMsg ReplicationManager::buildCurrentState(World&, Entity*, PersistentId) { return {}; }
#endif // FATE_HAS_GAME

} // namespace fate
