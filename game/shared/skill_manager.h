#pragma once
#include "game/shared/game_types.h"
#include "game/shared/character_stats.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace fate {

// ============================================================================
// LearnedSkill — a skill the player has acquired
// ============================================================================
struct LearnedSkill {
    std::string skillId;
    int unlockedRank  = 0;  // Unlocked via skillbook (0-3)
    int activatedRank = 0;  // Activated via skill points (0-3)

    [[nodiscard]] int effectiveRank() const { return activatedRank; }
};

// ============================================================================
// SkillDefinition — loaded from DB, cached in memory
// ============================================================================
struct SkillDefinition {
    std::string skillId;
    std::string skillName;
    std::string className;

    SkillType       skillType   = SkillType::Active;
    SkillTargetType targetType  = SkillTargetType::SingleEnemy;
    DamageType      damageType  = DamageType::Physical;

    int   baseDamage       = 0;
    int   mpCost           = 0;
    float furyCost         = 0.0f;
    float cooldownSeconds  = 0.0f;
    float range            = 0.0f;
    int   levelRequirement = 1;
    int   maxRank          = 3;

    std::string description;

    // Cast time (0 = instant)
    float castTime = 0.0f;

    // Double-cast: casting this skill opens an instant-cast window
    bool enablesDoubleCast  = false;
    float doubleCastWindow  = 2.0f;

    // Rank scaling arrays (index 0 = rank 1, etc.)
    std::vector<float> damagePerRank;
    std::vector<float> cooldownPerRank;
    std::vector<float> costPerRank;

    // ---- Resource & Scaling ----
    ResourceType resourceType = ResourceType::Mana;
    bool  canCrit            = true;
    bool  usesHitRate        = true;
    float furyOnHit          = 0.0f;
    bool  scalesWithResource = false;

    // ---- Effect Application Flags ----
    bool appliesBleed  = false;
    bool appliesBurn   = false;
    bool appliesPoison = false;
    bool appliesSlow   = false;
    bool appliesFreeze = false;

    std::vector<float> stunDurationPerRank;
    std::vector<float> effectDurationPerRank;
    std::vector<float> effectValuePerRank;

    // ---- Passive Bonuses (per-rank) ----
    std::vector<float> passiveDamageReductionPerRank;
    std::vector<float> passiveCritBonusPerRank;
    std::vector<float> passiveSpeedBonusPerRank;
    std::vector<int>   passiveHPBonusPerRank;
    std::vector<int>   passiveStatBonusPerRank;

    // ---- Special Mechanics ----
    bool isUltimate = false;
    std::vector<float> executeThresholdPerRank;

    bool  grantsInvulnerability = false;
    bool  removesDebuffs        = false;
    bool  grantsStunImmunity    = false;
    bool  grantsCritGuarantee   = false;

    float aoeRadius = 0.0f;
    std::vector<int> maxTargetsPerRank;

    float teleportDistance     = 0.0f;
    float dashDistance         = 0.0f;
    float transformDamageMult  = 0.0f;
    float transformSpeedBonus  = 0.0f;
};

// ============================================================================
// SkillManager — server-authoritative skill logic for a single character
// ============================================================================
class SkillManager {
public:
    // ---- Callbacks ----
    std::function<void(const std::string&, int)>         onSkillUsed;         // (skillId, rank)
    std::function<void(const std::string&, std::string)> onSkillFailed;       // (skillId, reason)
    std::function<void(const std::string&, int)>         onSkillLearned;      // (skillId, rank)
    std::function<void(int)>                             onSkillPointsChanged; // (availablePoints)

    // ---- Initialization ----
    void initialize(CharacterStats* playerStats);

    // ---- Skill Queries ----
    [[nodiscard]] bool               hasSkill(const std::string& skillId) const;
    [[nodiscard]] const LearnedSkill* getLearnedSkill(const std::string& skillId) const;

    // ---- Skill Acquisition ----
    bool learnSkill(const std::string& skillId, int rank);         // From skillbook usage
    bool activateSkillRank(const std::string& skillId);            // Spend skill point to activate next rank

    // ---- Cooldowns ----
    [[nodiscard]] bool  isOnCooldown(const std::string& skillId) const;
    [[nodiscard]] float getRemainingCooldown(const std::string& skillId) const;
    void                startCooldown(const std::string& skillId, float duration);

    // ---- Skill Points ----
    void grantSkillPoint();  // Called on level up

    // ---- Skill Bar (4 pages x 5 slots = 20) ----
    bool        assignSkillToSlot(const std::string& skillId, int globalSlotIndex);  // 0-19
    [[nodiscard]] std::string getSkillInSlot(int globalSlotIndex) const;
    void clearSkillSlot(int globalSlotIndex);
    void swapSkillSlots(int slotA, int slotB);
    bool autoAssignToSkillBar(const std::string& skillId);

    // ---- Tick ----
    void tick(float currentGameTime);  // Update cooldown tracking

    // ---- Double-Cast (hidden mechanic) ----
    void activateDoubleCast(const std::string& sourceSkillId, float windowDuration);
    void consumeDoubleCast();
    [[nodiscard]] bool isDoubleCastReady() const { return doubleCastReady_; }

    // ---- Accessors ----
    [[nodiscard]] int availablePoints() const { return availableSkillPoints; }
    [[nodiscard]] int earnedPoints()    const { return totalEarnedPoints; }
    [[nodiscard]] int spentPoints()     const { return totalSpentPoints; }
    [[nodiscard]] const std::vector<LearnedSkill>&  getLearnedSkills()  const { return learnedSkills; }
    [[nodiscard]] const std::vector<std::string>&   getSkillBarSlots()  const { return skillBarSlots; }

    // Serialization support
    void setSerializedState(std::vector<LearnedSkill> skills, std::vector<std::string> bar,
                            int available, int earned, int spent) {
        learnedSkills = std::move(skills);
        skillBarSlots = std::move(bar);
        availableSkillPoints = available;
        totalEarnedPoints = earned;
        totalSpentPoints = spent;
    }

    // ---- Skill Definition Registry ----
    void registerSkillDefinition(const SkillDefinition& def);
    const SkillDefinition* getSkillDefinition(const std::string& skillId) const;

private:
    std::vector<LearnedSkill>                 learnedSkills;
    std::vector<std::string>                  skillBarSlots;        // 20 slots
    std::unordered_map<std::string, float>    cooldownEndTimes;     // Server-only
    std::unordered_map<std::string, SkillDefinition> skillDefinitions_;

    int availableSkillPoints = 0;
    int totalEarnedPoints    = 0;
    int totalSpentPoints     = 0;

    CharacterStats* stats       = nullptr;  // Non-owning pointer
    float           currentTime = 0.0f;     // Updated externally via tick()

    // ---- Double-cast state (transient, not serialized) ----
    bool doubleCastReady_          = false;
    float doubleCastExpireTime_    = 0.0f;
    std::string doubleCastSourceSkillId_;

    static constexpr int SKILL_BAR_SLOTS = 20;  // 4 pages x 5 slots
};

} // namespace fate
