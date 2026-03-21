#pragma once
#include "engine/net/aoi.h"
#include "engine/net/replication.h"
#include "engine/core/types.h"
#include <algorithm>
#include <cmath>

namespace fate {

struct TargetValidator {
    // Check if a PersistentId (64-bit) corresponds to an EntityHandle in the AOI.
    // Requires the replication manager to resolve PersistentId → EntityHandle.
    // Check if a PersistentId is visible to a client.
    // Uses aoi.previous because aoi.current is empty between ticks
    // (advance() clears it after each replication update).
    // Packets are processed in poll() BEFORE replication rebuilds visibility.
    static bool isInAOI(const VisibilitySet& aoi, uint64_t targetPersistentId,
                        const ReplicationManager& replication) {
        EntityHandle target = replication.getEntityHandle(PersistentId(targetPersistentId));
        if (target.isNull()) return false;
        // previous is sorted (computeDiff sorts it) and holds last tick's visibility
        return std::binary_search(aoi.previous.begin(), aoi.previous.end(), target);
    }

    static bool isInRange(Vec2 playerPos, Vec2 targetPos,
                          float maxRange, float latencyTolerance = 16.0f) {
        float dx = playerPos.x - targetPos.x;
        float dy = playerPos.y - targetPos.y;
        float distSq = dx * dx + dy * dy;
        float allowed = maxRange + latencyTolerance;
        return distSq <= allowed * allowed;
    }
};

} // namespace fate
