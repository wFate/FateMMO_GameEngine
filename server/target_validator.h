#pragma once
#include "engine/net/aoi.h"
#include "engine/net/replication.h"
#include "engine/core/types.h"
#include "game/shared/game_types.h"
#include "game/shared/character_stats.h"
#include "game/shared/faction.h"
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
        return stats.isAlive();
    }

    // Check if attacker can hit this PvP target (faction/party/safe-zone/PK checks)
    static bool canAttackPlayer(const CharacterStats& attacker,
                                const CharacterStats& target,
                                bool inSameParty,
                                bool inSafeZone) {
        // Cannot attack in safe zones
        if (inSafeZone) return false;

        // Cannot attack party members
        if (inSameParty) return false;

        // Cannot attack dead/dying targets
        if (!target.isAlive()) return false;

        // Faction-based rules
        bool sameFaction = FactionRegistry::isSameFaction(attacker.faction, target.faction);
        if (sameFaction) {
            // Same faction AND target is Red or Black (PK flagged) → fair game
            if (target.pkStatus == PKStatus::Red || target.pkStatus == PKStatus::Black)
                return true;
            // Same faction AND target is White/Purple → cannot attack innocents
            return false;
        }

        // Different factions (or either is None) → always allowed in PvP zones
        return true;
    }
};

} // namespace fate
