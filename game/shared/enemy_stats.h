#pragma once

#include "game/shared/game_types.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace fate {

// ---------------------------------------------------------------------------
// EnemyStats -- server-side mob stats (converted from NetworkEnemyStats.cs)
// ---------------------------------------------------------------------------
class EnemyStats {
public:
    // ---- Identity ----
    std::string enemyId;                        // from mob_definitions
    std::string enemyName    = "Enemy";
    std::string monsterType  = "Normal";        // Normal, MiniBoss, Boss, RaidBoss
    std::string sceneId;                        // which scene this mob belongs to

    // ---- Combat Stats ----
    int   baseDamage         = 10;
    int   armor              = 0;
    int   magicResist        = 0;               // (MobLevel * 2) + TierBonus
    bool  dealsMagicDamage   = false;
    int   mobHitRate         = 10;              // Normal=10, MiniBoss=12, Boss=16, RaidBoss=20
    float critRate           = 0.05f;
    float attackSpeed        = 1.0f;
    float moveSpeed          = 1.0f;

    // ---- Level / XP ----
    int   level              = 1;
    int   xpReward           = 10;
    bool  useScaling         = false;
    float hpPerLevel         = 5.0f;
    float damagePerLevel     = 1.0f;

    // ---- Loot (server-only) ----
    std::string lootTableId;
    int   minGoldDrop        = 0;
    int   maxGoldDrop        = 0;
    float goldDropChance     = 1.0f;

    // ---- AI Behavior ----
    bool  isAggressive       = true;
    int   honorReward        = 0;
    bool  isGauntletMob      = false;
    bool  isBoss             = false;

    // ---- Health State ----
    int   currentHP          = 100;
    int   maxHP              = 100;
    bool  isAlive            = true;

    // ---- Combat Leash (boss/mini-boss only) ----
    float lastDamageTime     = 0.0f;   // game-time of last incoming damage
    static constexpr float LEASH_TIMEOUT = 15.0f; // seconds with no damage before HP reset

    // ---- Threat Table (server-only) ----
    std::unordered_map<uint32_t, int> damageByAttacker;   // entityId -> total damage

    // ---- Callbacks ----
    std::function<void()>       onDied;
    std::function<void()>       onRespawned;
    std::function<void(int)>    onDamaged;
    std::function<void(uint32_t)> onProvokedByPlayer;     // for passive mobs

    // ---- Methods ----

    // Apply scaling if useScaling: maxHP = baseHP + hpPerLevel*(level-1), etc.
    void initialize();

    // Subtract HP, check death.
    void takeDamage(int amount);

    // Track damage in threat table + apply damage.
    void takeDamageFrom(uint32_t attackerEntityId, int amount);

    // Set isAlive=false, fire onDied callback.
    void die();

    // Reset HP, clear threat table, set isAlive=true.
    void respawn();

    // Heal by amount, clamped to maxHP.
    void heal(int amount);

    // Boss/mini-boss leash: if no damage received for LEASH_TIMEOUT seconds,
    // reset to full HP and clear threat. Returns true if a reset occurred.
    bool tickLeash(float gameTime);

    // Returns entityId with highest damage dealt, 0 if none.
    [[nodiscard]] uint32_t getTopThreatTarget() const;

    // Result returned by getTopDamagerPartyAware.
    struct LootOwnerResult {
        uint32_t topDamagerId  = 0;   // Top individual in winning group
        int64_t  winningGroupId = 0;  // The party/solo group that won
        bool     isParty        = false; // True if winning group is a party (not solo)
    };

    // Returns info about the group (party or solo) that dealt the most total damage,
    // plus the top individual within that group. Tie-breaking favors lower entity/group ID.
    // partyLookup(entityId) must return current partyId >= 0 if in a party, or -1 if solo.
    [[nodiscard]] LootOwnerResult getTopDamagerPartyAware(
        std::function<int(uint32_t)> partyLookup) const;

    // Check if an entity has entries in the threat table.
    [[nodiscard]] bool hasThreatFrom(uint32_t entityId) const;

    // Get total damage tracked for a given entity, 0 if absent.
    [[nodiscard]] int getThreatAmount(uint32_t entityId) const;

    // Clear all entries in the threat table.
    void clearThreatTable();

    // Returns the mobHitRate field.
    [[nodiscard]] int getMobHitRate() const;

    // baseDamage + damagePerLevel*(level-1) if scaling is enabled.
    [[nodiscard]] int getScaledDamage() const;

    // Random value in [scaledDamage*0.8, scaledDamage*1.2].
    [[nodiscard]] int rollDamage() const;
};

} // namespace fate
