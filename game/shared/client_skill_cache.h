#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace fate {

// ============================================================================
// ClientSkillRankData — per-rank display data for the skill UI
// ============================================================================
struct ClientSkillRankData {
    int resourceCost = 0;
    float cooldownSeconds = 0.0f;
    int damagePercent = 0;
    int maxTargets = 1;
    float effectDuration = 0.0f;
    float effectValue = 0.0f;
    float stunDuration = 0.0f;
    float passiveDamageReduction = 0.0f;
    float passiveCritBonus = 0.0f;
    float passiveSpeedBonus = 0.0f;
    int passiveHPBonus = 0;
    int passiveStatBonus = 0;
};

// ============================================================================
// ClientSkillDef — client-side skill definition for UI display
// ============================================================================
struct ClientSkillDef {
    std::string skillId;
    std::string skillName;
    std::string description;
    std::string skillType;     // "Active" or "Passive"
    std::string resourceType;  // "Mana", "Fury", "None"
    std::string targetType;    // "Self", "SingleEnemy", etc.
    int levelRequired = 1;
    float range = 0.0f;
    float aoeRadius = 0.0f;
    bool isUltimate = false;
    bool isPassive = false;
    bool consumesAllResource = false;
    ClientSkillRankData ranks[3];
};

// ============================================================================
// ClientSkillDefinitionCache — static cache for client-side skill UI data
// ============================================================================
class ClientSkillDefinitionCache {
public:
    /// Populate the cache with skills for a given class.
    /// Clears existing data first, then stores all skills sorted by levelRequired.
    static void populate(const std::string& className, const std::vector<ClientSkillDef>& skills) {
        (void)className;  // stored implicitly via the skills themselves
        cache_.clear();
        sorted_.clear();

        for (const auto& skill : skills) {
            cache_[skill.skillId] = skill;
        }

        sorted_ = skills;
        std::sort(sorted_.begin(), sorted_.end(),
                  [](const ClientSkillDef& a, const ClientSkillDef& b) {
                      return a.levelRequired < b.levelRequired;
                  });
    }

    /// Clear all cached data (call on disconnect).
    static void clear() {
        cache_.clear();
        sorted_.clear();
    }

    /// Get a skill by ID. Returns nullptr if not found.
    static const ClientSkillDef* getSkill(const std::string& skillId) {
        auto it = cache_.find(skillId);
        return (it != cache_.end()) ? &it->second : nullptr;
    }

    /// Get all skills sorted by levelRequired ascending.
    static const std::vector<ClientSkillDef>& getAllSkills() {
        return sorted_;
    }

    /// Check if a skill exists in the cache.
    static bool hasSkill(const std::string& skillId) {
        return cache_.find(skillId) != cache_.end();
    }

private:
    static inline std::unordered_map<std::string, ClientSkillDef> cache_;
    static inline std::vector<ClientSkillDef> sorted_;
};

} // namespace fate
