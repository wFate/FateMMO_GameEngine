// ============================================================================
// NOTE: Networking & Database Integration Pending
// ============================================================================
// This file contains core skill logic (server-authoritative).
// Integration points needed:
//
// NETWORKING (ENet):
//   - Sync learnedSkills list to owning client (SyncList equivalent)
//   - Sync skillBarSlots to owning client
//   - Sync availableSkillPoints/totalEarned/totalSpent
//   - Handle CmdUseSkill(skillId, targetEntityId) from client
//   - Handle CmdUseSkillWithTarget(skillId, targetEntityId)
//   - Handle CmdAssignSkillToSlot(skillId, slotIndex)
//   - Handle CmdActivateSkillRank(skillId)
//   - Send skill definitions to client on spawn (TargetRpc equivalent)
//   - Send cooldown start notifications to client
//
// DATABASE (libpqxx):
//   - LoadSkills(): SELECT from character_skills WHERE character_id = ?
//   - SaveSkills(): UPSERT to character_skills
//   - LoadSkillPoints(): SELECT from character_skill_points
//   - SaveSkillPoints(): UPSERT to character_skill_points
//   - SkillDefinitionCache: Load all skills from skill_definitions on startup
//
// SKILL EXECUTION (requires CharacterStats + EnemyStats + StatusEffects):
//   - ExecuteSingleTargetSkill(): Apply damage, status effects, CC
//   - ExecuteAoESelfSkill(): Area damage around caster
//   - ExecuteAoETargetSkill(): Area damage at target location
//   - ExecuteHealSkill(): Heal self or ally
//   - ExecuteBuffSkill(): Apply status effects to self/allies
//   - All damage goes through CombatSystem formulas
//   - Validate range, line of sight, resources, cooldown before executing
// ============================================================================

#include "game/shared/skill_manager.h"

#include <algorithm>

namespace fate {

// ============================================================================
// Initialization
// ============================================================================

void SkillManager::initialize(CharacterStats* playerStats) {
    stats = playerStats;
    skillBarSlots.resize(SKILL_BAR_SLOTS, "");
}

// ============================================================================
// Skill Queries
// ============================================================================

bool SkillManager::hasSkill(const std::string& skillId) const {
    return std::ranges::any_of(learnedSkills, [&](const LearnedSkill& s) {
        return s.skillId == skillId;
    });
}

const LearnedSkill* SkillManager::getLearnedSkill(const std::string& skillId) const {
    auto it = std::ranges::find_if(learnedSkills, [&](const LearnedSkill& s) {
        return s.skillId == skillId;
    });
    return (it != learnedSkills.end()) ? &(*it) : nullptr;
}

// ============================================================================
// Skill Acquisition
// ============================================================================

bool SkillManager::learnSkill(const std::string& skillId, int rank) {
    if (rank < 1 || rank > 3) {
        return false;
    }

    // Check if we already know this skill
    for (auto& skill : learnedSkills) {
        if (skill.skillId == skillId) {
            // Already known — only upgrade if the new rank is higher
            if (rank <= skill.unlockedRank) {
                return false;
            }
            skill.unlockedRank = rank;

            if (onSkillLearned) {
                onSkillLearned(skillId, rank);
            }
            return true;
        }
    }

    // New skill
    LearnedSkill newSkill;
    newSkill.skillId      = skillId;
    newSkill.unlockedRank = rank;
    newSkill.activatedRank = 0;
    learnedSkills.push_back(std::move(newSkill));

    if (onSkillLearned) {
        onSkillLearned(skillId, rank);
    }
    return true;
}

bool SkillManager::activateSkillRank(const std::string& skillId) {
    if (availableSkillPoints <= 0) {
        if (onSkillFailed) {
            onSkillFailed(skillId, "No skill points available");
        }
        return false;
    }

    for (auto& skill : learnedSkills) {
        if (skill.skillId == skillId) {
            int nextRank = skill.activatedRank + 1;

            // Cannot activate beyond unlocked rank
            if (nextRank > skill.unlockedRank) {
                if (onSkillFailed) {
                    onSkillFailed(skillId, "Rank not yet unlocked via skillbook");
                }
                return false;
            }

            // Cannot exceed max rank (3)
            if (nextRank > 3) {
                if (onSkillFailed) {
                    onSkillFailed(skillId, "Already at maximum rank");
                }
                return false;
            }

            skill.activatedRank = nextRank;
            availableSkillPoints--;
            totalSpentPoints++;

            if (onSkillPointsChanged) {
                onSkillPointsChanged(availableSkillPoints);
            }
            return true;
        }
    }

    if (onSkillFailed) {
        onSkillFailed(skillId, "Skill not learned");
    }
    return false;
}

// ============================================================================
// Cooldowns
// ============================================================================

bool SkillManager::isOnCooldown(const std::string& skillId) const {
    auto it = cooldownEndTimes.find(skillId);
    if (it == cooldownEndTimes.end()) {
        return false;
    }
    return currentTime < it->second;
}

float SkillManager::getRemainingCooldown(const std::string& skillId) const {
    auto it = cooldownEndTimes.find(skillId);
    if (it == cooldownEndTimes.end()) {
        return 0.0f;
    }
    float remaining = it->second - currentTime;
    return (remaining > 0.0f) ? remaining : 0.0f;
}

void SkillManager::startCooldown(const std::string& skillId, float duration) {
    cooldownEndTimes[skillId] = currentTime + duration;
}

// ============================================================================
// Skill Points
// ============================================================================

void SkillManager::grantSkillPoint() {
    availableSkillPoints++;
    totalEarnedPoints++;

    if (onSkillPointsChanged) {
        onSkillPointsChanged(availableSkillPoints);
    }
}

// ============================================================================
// Skill Bar (4 pages x 5 slots = 20 total)
// ============================================================================

bool SkillManager::assignSkillToSlot(const std::string& skillId, int globalSlotIndex) {
    if (globalSlotIndex < 0 || globalSlotIndex >= SKILL_BAR_SLOTS) {
        return false;
    }

    // Empty string clears the slot
    if (skillId.empty()) {
        skillBarSlots[globalSlotIndex] = "";
        return true;
    }

    // Must have learned the skill
    if (!hasSkill(skillId)) {
        return false;
    }

    skillBarSlots[globalSlotIndex] = skillId;
    return true;
}

std::string SkillManager::getSkillInSlot(int globalSlotIndex) const {
    if (globalSlotIndex < 0 || globalSlotIndex >= SKILL_BAR_SLOTS) {
        return "";
    }
    return skillBarSlots[globalSlotIndex];
}

// ============================================================================
// Tick
// ============================================================================

void SkillManager::tick(float currentGameTime) {
    currentTime = currentGameTime;

    // Prune expired cooldowns to keep the map from growing unbounded
    std::erase_if(cooldownEndTimes, [&](const auto& pair) {
        return pair.second <= currentTime;
    });

    // Expire double-cast window
    if (doubleCastReady_ && currentTime >= doubleCastExpireTime_) {
        doubleCastReady_ = false;
        doubleCastSourceSkillId_.clear();
    }
}

// ============================================================================
// Double-Cast (hidden mechanic)
// ============================================================================

void SkillManager::activateDoubleCast(const std::string& sourceSkillId, float windowDuration) {
    doubleCastReady_ = true;
    doubleCastExpireTime_ = currentTime + windowDuration;
    doubleCastSourceSkillId_ = sourceSkillId;
}

void SkillManager::consumeDoubleCast() {
    doubleCastReady_ = false;
    doubleCastSourceSkillId_.clear();
}

} // namespace fate
