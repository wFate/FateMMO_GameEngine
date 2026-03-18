#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <pqxx/pqxx>

namespace fate {

// ============================================================================
// Definition Caches — loaded once at server startup from PostgreSQL
//
// NOTE: ItemDefinitionCache lives in server/cache/item_definition_cache.h
//       LootTableCache lives in server/cache/loot_table_cache.h
//       This file adds the remaining definition caches: Mob, Skill, Scene.
// ============================================================================

// ============================================================================
// Mob Definition (73 mobs)
// ============================================================================
struct CachedMobDef {
    std::string mobDefId;
    std::string displayName;
    int baseHP = 100;
    int baseDamage = 10;
    int baseArmor = 0;
    float critRate = 0.05f;
    float attackSpeed = 1.5f;
    float moveSpeed = 1.8f;
    int magicResist = 0;
    bool dealsMagicDamage = false;
    int mobHitRate = 0;
    float hpPerLevel = 20.0f;
    float damagePerLevel = 2.0f;
    float armorPerLevel = 0.5f;
    int baseXPReward = 10;
    float xpPerLevel = 2.0f;
    float aggroRange = 4.0f;
    float attackRange = 1.0f;
    float leashRadius = 6.0f;
    int respawnSeconds = 30;
    int minSpawnLevel = 1;
    int maxSpawnLevel = 100;
    int spawnWeight = 10;
    bool isAggressive = true;
    bool isBoss = false;
    bool isElite = false;
    std::string attackStyle = "Melee";
    std::string monsterType = "Normal";
    std::string lootTableId;
    int minGoldDrop = 0;
    int maxGoldDrop = 0;
    float goldDropChance = 1.0f;
    int honorReward = 0;

    [[nodiscard]] int getHPForLevel(int level) const {
        return baseHP + static_cast<int>(hpPerLevel * (level - 1));
    }
    [[nodiscard]] int getDamageForLevel(int level) const {
        return baseDamage + static_cast<int>(damagePerLevel * (level - 1));
    }
    [[nodiscard]] int getArmorForLevel(int level) const {
        return baseArmor + static_cast<int>(armorPerLevel * (level - 1));
    }
    [[nodiscard]] int getXPRewardForLevel(int level) const {
        return baseXPReward + static_cast<int>(xpPerLevel * (level - 1));
    }
};

// ============================================================================
// Skill Definition (60 skills)
// ============================================================================
struct CachedSkillDef {
    std::string skillId;
    std::string skillName;
    std::string classRequired;
    std::string skillType;      // "Active" or "Passive"
    int levelRequired = 1;
    std::string resourceType;   // "None", "Fury", "Mana"
    std::string targetType;     // "Self", "SingleEnemy", "SingleAlly", etc.
    float range = 1.0f;
    float aoeRadius = 0;
    std::string damageType;     // "Physical", "Magic", "Fire", etc.
    bool canCrit = true;
    bool usesHitRate = true;
    float furyOnHit = 0;
    bool isUltimate = false;
    float castTime = 0;
    float channelTime = 0;
    bool appliesBleed = false;
    bool appliesBurn = false;
    bool appliesPoison = false;
    bool appliesSlow = false;
    bool appliesTaunt = false;
    bool appliesFreeze = false;
    bool grantsInvulnerability = false;
    bool grantsStunImmunity = false;
    bool grantsCritGuarantee = false;
    bool removesDebuffs = false;
    float teleportDistance = 0;
    float dashDistance = 0;
    bool isResurrection = false;
    bool locksMovement = false;
    bool consumesAllResource = false;
    bool scalesWithResource = false;
    std::string description;
    std::string animationTrigger;
};

// ============================================================================
// Skill Rank (174 ranks across 60 skills)
// ============================================================================
struct CachedSkillRank {
    std::string skillId;
    int rank = 1;
    int resourceCost = 0;
    float cooldownSeconds = 1.0f;
    int damagePercent = 100;
    int maxTargets = 1;
    float effectDuration = 0;
    float effectValue = 0;
    float effectValue2 = 0;
    float stunDuration = 0;
    float executeThreshold = 0;
    float passiveDamageReduction = 0;
    float passiveCritBonus = 0;
    float passiveSpeedBonus = 0;
    float passiveHPBonus = 0;
    int passiveStatBonus = 0;
    float passiveArmorBonus = 0;
    float passiveHitRateBonus = 0;
    float transformDamageMult = 0;
    float transformSpeedBonus = 0;
    float resurrectHPPercent = 0;
};

// ============================================================================
// Scene Info (3 scenes)
// ============================================================================
struct SceneInfoRecord {
    std::string sceneId;
    std::string sceneName;
    std::string sceneType;
    int minLevel = 1;
    bool isDungeon = false;
    bool pvpEnabled = false;
};

// ============================================================================
// MobDefCache
// ============================================================================
class MobDefCache {
public:
    bool initialize(pqxx::connection& conn);
    void reload(pqxx::connection& conn);

    [[nodiscard]] const CachedMobDef* get(const std::string& mobDefId) const;
    [[nodiscard]] bool has(const std::string& mobDefId) const;
    [[nodiscard]] int count() const { return static_cast<int>(mobs_.size()); }
    [[nodiscard]] const std::unordered_map<std::string, CachedMobDef>& all() const { return mobs_; }

private:
    std::unordered_map<std::string, CachedMobDef> mobs_;
};

// ============================================================================
// SkillDefCache
// ============================================================================
class SkillDefCache {
public:
    bool initialize(pqxx::connection& conn);
    void reload(pqxx::connection& conn);

    [[nodiscard]] const CachedSkillDef* getSkill(const std::string& skillId) const;
    [[nodiscard]] std::vector<CachedSkillRank> getRanks(const std::string& skillId) const;
    [[nodiscard]] const CachedSkillRank* getRank(const std::string& skillId, int rank) const;
    [[nodiscard]] std::vector<const CachedSkillDef*> getSkillsForClass(const std::string& className) const;
    [[nodiscard]] bool has(const std::string& skillId) const;
    [[nodiscard]] int skillCount() const { return static_cast<int>(skills_.size()); }
    [[nodiscard]] int rankCount() const { return static_cast<int>(ranks_.size()); }

private:
    std::unordered_map<std::string, CachedSkillDef> skills_;
    std::unordered_map<std::string, CachedSkillRank> ranks_;
    std::unordered_map<std::string, std::vector<CachedSkillRank>> ranksBySkill_;

    static std::string rankKey(const std::string& skillId, int rank) {
        return skillId + ":" + std::to_string(rank);
    }
};

// ============================================================================
// SceneCache
// ============================================================================
class SceneCache {
public:
    bool initialize(pqxx::connection& conn);

    [[nodiscard]] const SceneInfoRecord* get(const std::string& sceneId) const;
    [[nodiscard]] bool isPvPEnabled(const std::string& sceneId) const;
    [[nodiscard]] int count() const { return static_cast<int>(scenes_.size()); }

private:
    std::unordered_map<std::string, SceneInfoRecord> scenes_;
};

} // namespace fate
