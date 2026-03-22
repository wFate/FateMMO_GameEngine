#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <pqxx/pqxx>
#include "game/shared/cached_mob_def.h"

namespace fate {

// ============================================================================
// Definition Caches — loaded once at server startup from PostgreSQL
//
// NOTE: ItemDefinitionCache lives in server/cache/item_definition_cache.h
//       LootTableCache lives in server/cache/loot_table_cache.h
//       This file adds the remaining definition caches: Mob, Skill, Scene.
// ============================================================================

// CachedMobDef is defined in game/shared/cached_mob_def.h (no pqxx dependency,
// usable by both server and game code).

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
    int difficultyTier = 1;
    float defaultSpawnX = 0.0f;
    float defaultSpawnY = 0.0f;
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
    [[nodiscard]] const SceneInfoRecord* getByName(const std::string& sceneName) const;
    [[nodiscard]] bool isPvPEnabled(const std::string& sceneId) const;
    [[nodiscard]] int count() const { return static_cast<int>(scenes_.size()); }

private:
    std::unordered_map<std::string, SceneInfoRecord> scenes_;
};

} // namespace fate
