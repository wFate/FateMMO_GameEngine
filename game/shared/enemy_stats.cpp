#include "game/shared/enemy_stats.h"

#include <algorithm>
#include <random>

namespace fate {

// ---------------------------------------------------------------------------
// Thread-local RNG
// ---------------------------------------------------------------------------
static thread_local std::mt19937 s_rng{std::random_device{}()};

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------
void EnemyStats::initialize() {
    if (useScaling && level > 1) {
        int baseHP = maxHP;
        maxHP      = baseHP + static_cast<int>(hpPerLevel * (level - 1));
        baseDamage = baseDamage + static_cast<int>(damagePerLevel * (level - 1));
    }
    currentHP = maxHP;
    isAlive   = true;
}

// ---------------------------------------------------------------------------
// takeDamage
// ---------------------------------------------------------------------------
void EnemyStats::takeDamage(int amount) {
    if (!isAlive || amount <= 0) return;

    currentHP -= amount;

    if (onDamaged) {
        onDamaged(amount);
    }

    if (currentHP <= 0) {
        currentHP = 0;
        die();
    }
}

// ---------------------------------------------------------------------------
// takeDamageFrom
// ---------------------------------------------------------------------------
void EnemyStats::takeDamageFrom(uint32_t attackerEntityId, int amount, int partyId) {
    if (!isAlive || amount <= 0) return;

    // Track in threat table
    damageByAttacker[attackerEntityId] += amount;
    // Store partyId at damage time (updates if party changes mid-fight, latest wins)
    attackerPartyId[attackerEntityId] = partyId;

    // If passive mob is provoked, notify
    if (!isAggressive && onProvokedByPlayer) {
        onProvokedByPlayer(attackerEntityId);
    }

    takeDamage(amount);

    // Note: lastDamageTime is set by the server after calling this method,
    // since EnemyStats doesn't track game time itself.
}

// ---------------------------------------------------------------------------
// die
// ---------------------------------------------------------------------------
void EnemyStats::die() {
    isAlive   = false;
    currentHP = 0;

    if (onDied) {
        onDied();
    }
}

// ---------------------------------------------------------------------------
// respawn
// ---------------------------------------------------------------------------
void EnemyStats::respawn() {
    currentHP = maxHP;
    isAlive   = true;
    clearThreatTable();

    if (onRespawned) {
        onRespawned();
    }
}

// ---------------------------------------------------------------------------
// heal
// ---------------------------------------------------------------------------
void EnemyStats::heal(int amount) {
    if (!isAlive || amount <= 0) return;
    currentHP = (std::min)(currentHP + amount, maxHP);
}

// ---------------------------------------------------------------------------
// tickLeash
// ---------------------------------------------------------------------------
bool EnemyStats::tickLeash(float gameTime) {
    if (!isAlive) return false;
    if (currentHP >= maxHP) return false;              // already full — nothing to reset
    if (lastDamageTime <= 0.0f) return false;          // never been hit
    if (damageByAttacker.empty()) return false;         // no attackers tracked

    if (gameTime - lastDamageTime >= LEASH_TIMEOUT) {
        currentHP = maxHP;
        clearThreatTable();
        lastDamageTime = 0.0f;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// getTopThreatTarget
// ---------------------------------------------------------------------------
uint32_t EnemyStats::getTopThreatTarget() const {
    uint32_t topId    = 0;
    int      topValue = 0;

    for (const auto& [entityId, damage] : damageByAttacker) {
        if (damage > topValue) {
            topValue = damage;
            topId    = entityId;
        }
    }
    return topId;
}

// ---------------------------------------------------------------------------
// getTopDamagerPartyAware
// ---------------------------------------------------------------------------
EnemyStats::LootOwnerResult EnemyStats::getTopDamagerPartyAware() const {
    return getTopDamagerPartyAware([this](uint32_t entityId) -> int {
        auto it = attackerPartyId.find(entityId);
        return (it != attackerPartyId.end()) ? it->second : -1;
    });
}

EnemyStats::LootOwnerResult EnemyStats::getTopDamagerPartyAware(
    std::function<int(uint32_t)> partyLookup) const {
    if (damageByAttacker.empty()) return {};

    // Aggregate damage by group.
    // Party members share a positive groupId (their partyId).
    // Solo players get a unique negative groupId (-entityId) so they never collide.
    std::unordered_map<int64_t, int>      damageByGroup;    // groupId -> total damage
    std::unordered_map<int64_t, uint32_t> topInGroup;       // groupId -> top individual entityId
    std::unordered_map<int64_t, int>      topInGroupDamage; // groupId -> that individual's damage

    for (const auto& [entityId, damage] : damageByAttacker) {
        int partyId = partyLookup(entityId);
        int64_t groupId = (partyId >= 0) ? static_cast<int64_t>(partyId)
                                          : -static_cast<int64_t>(entityId);

        damageByGroup[groupId] += damage;

        // Tie-break within group: higher damage wins; equal damage favors lower entityId
        auto& topDmg = topInGroupDamage[groupId];
        auto& topId  = topInGroup[groupId];
        if (damage > topDmg || (damage == topDmg && entityId < topId)) {
            topDmg = damage;
            topId  = entityId;
        }
    }

    // Find the group with the highest total damage.
    // Tie-break: for parties, lower partyId (positive groupId) wins.
    // For solo players and mixed ties, lower top-individual entityId wins.
    int64_t  bestGroup    = 0;
    int      bestDamage   = 0;
    uint32_t bestTopId    = 0;
    bool     foundAny     = false;
    for (const auto& [groupId, totalDmg] : damageByGroup) {
        uint32_t candidateTopId = topInGroup[groupId];
        bool better = false;
        if (!foundAny) {
            better = true;
        } else if (totalDmg > bestDamage) {
            better = true;
        } else if (totalDmg == bestDamage) {
            // Both groups tied on total damage.
            // For two parties: lower partyId wins (both groupIds positive).
            // Otherwise: lower top-individual entityId wins.
            if (groupId > 0 && bestGroup > 0) {
                better = (groupId < bestGroup);
            } else {
                better = (candidateTopId < bestTopId);
            }
        }
        if (better) {
            bestDamage = totalDmg;
            bestGroup  = groupId;
            bestTopId  = candidateTopId;
            foundAny   = true;
        }
    }

    LootOwnerResult result;
    result.topDamagerId   = topInGroup[bestGroup];
    result.winningGroupId = bestGroup;
    result.isParty        = (bestGroup > 0); // positive groupId means a real partyId
    return result;
}

// ---------------------------------------------------------------------------
// hasThreatFrom
// ---------------------------------------------------------------------------
bool EnemyStats::hasThreatFrom(uint32_t entityId) const {
    return damageByAttacker.contains(entityId);
}

// ---------------------------------------------------------------------------
// getThreatAmount
// ---------------------------------------------------------------------------
int EnemyStats::getThreatAmount(uint32_t entityId) const {
    auto it = damageByAttacker.find(entityId);
    return (it != damageByAttacker.end()) ? it->second : 0;
}

// ---------------------------------------------------------------------------
// clearThreatTable
// ---------------------------------------------------------------------------
void EnemyStats::clearThreatTable() {
    damageByAttacker.clear();
    attackerPartyId.clear();
}

// ---------------------------------------------------------------------------
// getMobHitRate
// ---------------------------------------------------------------------------
int EnemyStats::getMobHitRate() const {
    return mobHitRate;
}

// ---------------------------------------------------------------------------
// getScaledDamage
// ---------------------------------------------------------------------------
int EnemyStats::getScaledDamage() const {
    if (useScaling && level > 1) {
        return baseDamage + static_cast<int>(damagePerLevel * (level - 1));
    }
    return baseDamage;
}

// ---------------------------------------------------------------------------
// rollDamage
// ---------------------------------------------------------------------------
int EnemyStats::rollDamage() const {
    int scaled = getScaledDamage();
    int lo     = static_cast<int>(scaled * 0.8f);
    int hi     = static_cast<int>(scaled * 1.2f);

    if (lo >= hi) return scaled;

    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(s_rng);
}

} // namespace fate
