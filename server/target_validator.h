#pragma once
#include "engine/net/aoi.h"
#include "engine/net/replication.h"
#include "engine/core/types.h"
#include "game/shared/game_types.h"
#include "game/shared/character_stats.h"
#include <algorithm>
#include <cmath>

namespace fate {

struct TargetValidator {
    // Check if a PersistentId is visible to a client.
    // Uses aoi.previous because aoi.current is empty between ticks
    // (advance() clears it after each replication update).
    static bool isInAOI(const VisibilitySet& aoi, uint64_t targetPersistentId,
                        const ReplicationManager& replication) {
        EntityHandle target = replication.getEntityHandle(PersistentId(targetPersistentId));
        if (target.isNull()) return false;
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

    // Reject actions from dead players (posthumous action rejection)
    static bool isAttackerAlive(const CharacterStats& stats) {
        return !stats.isDead;
    }

    // Check if attacker can hit this PvP target (faction/party/guild checks)
    static bool canAttackPlayer(const CharacterStats& /*attacker*/,
                                const CharacterStats& /*target*/) {
        // Cannot attack same-faction in safe zones (future)
        // Cannot attack self (handled elsewhere)
        return true;
    }
};

} // namespace fate
