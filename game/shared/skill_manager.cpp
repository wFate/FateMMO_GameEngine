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
#include "engine/core/logger.h"

#include <algorithm>
#include <cmath>

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
        LOG_WARN("SkillManager", "learnSkill('%s', %d) REJECTED: rank out of range", skillId.c_str(), rank);
        return false;
    }

    // If a definition is registered, validate class and level requirements
    const SkillDefinition* def = getSkillDefinition(skillId);
    if (def) {
        // Class restriction: must match player class name, or be "Any"/empty (classless)
        if (!def->className.empty() && def->className != "Any" && stats && def->className != stats->className) {
            LOG_WARN("SkillManager", "learnSkill('%s', %d) REJECTED: class mismatch (def='%s', player='%s')",
                     skillId.c_str(), rank, def->className.c_str(), stats->className.c_str());
            return false;
        }
        // Level requirement
        if (stats && stats->level < def->levelRequirement) {
            LOG_WARN("SkillManager", "learnSkill('%s', %d) REJECTED: level too low (need %d, have %d)",
                     skillId.c_str(), rank, def->levelRequirement, stats->level);
            return false;
        }
    }

    // Check if we already know this skill
    for (auto& skill : learnedSkills) {
        if (skill.skillId == skillId) {
            // Already known — only upgrade if the new rank is higher
            if (rank <= skill.unlockedRank) {
                LOG_WARN("SkillManager", "learnSkill('%s', %d) REJECTED: already at rank %d",
                         skillId.c_str(), rank, skill.unlockedRank);
                return false;
            }
            // Sequential unlock: rank N requires rank N-1 already unlocked
            if (rank > skill.unlockedRank + 1) {
                LOG_WARN("SkillManager", "learnSkill('%s', %d) REJECTED: rank skip (current %d)",
                         skillId.c_str(), rank, skill.unlockedRank);
                return false;
            }
            skill.unlockedRank = rank;

            if (onSkillLearned) {
                onSkillLearned(skillId, rank);
            }
            return true;
        }
    }

    // New skill — must start at rank 1
    if (rank != 1) {
        LOG_WARN("SkillManager", "learnSkill('%s', %d) REJECTED: new skill must start at rank 1",
                 skillId.c_str(), rank);
        return false;
    }

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

            // Auto-assign to skill bar on first activation
            if (nextRank == 1) {
                autoAssignToSkillBar(skillId);
            }

            const SkillDefinition* def = getSkillDefinition(skillId);
            if (def && def->skillType == SkillType::Passive) {
                int ri = nextRank - 1;
                if (ri >= 0) {
                    if (ri < (int)def->passiveHPBonusPerRank.size()) passiveHPBonus_ += def->passiveHPBonusPerRank[ri];
                    if (ri < (int)def->passiveCritBonusPerRank.size()) passiveCritBonus_ += def->passiveCritBonusPerRank[ri];
                    if (ri < (int)def->passiveSpeedBonusPerRank.size()) passiveSpeedBonus_ += def->passiveSpeedBonusPerRank[ri];
                    if (ri < (int)def->passiveDamageReductionPerRank.size()) passiveDamageReduction_ += def->passiveDamageReductionPerRank[ri];
                    if (ri < (int)def->passiveStatBonusPerRank.size()) passiveStatBonus_ += def->passiveStatBonusPerRank[ri];
                    if (ri < (int)def->passiveArmorBonusPerRank.size()) passiveArmorBonus_ += def->passiveArmorBonusPerRank[ri];
                    if (ri < (int)def->passiveHitRateBonusPerRank.size()) passiveHitRateBonus_ += def->passiveHitRateBonusPerRank[ri];
                    if (skillId == "mage_arcane_intellect") {
                        if (ri < (int)def->effectValuePerRank.size()) passiveSpellDamageBonus_ += def->effectValuePerRank[ri];
                    }
                    applyPassiveBonusesToStats();
                }
            }

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

void SkillManager::resetAllSkillRanks() {
    // Refund all spent points back to available
    availableSkillPoints += totalSpentPoints;
    totalSpentPoints = 0;

    // Reset every skill's activated rank to 0 (keep unlocked ranks from skillbooks)
    for (auto& skill : learnedSkills) {
        skill.activatedRank = 0;
    }

    // Recompute passive bonuses (will zero them out since all activatedRanks are 0)
    recomputePassiveBonuses();

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

void SkillManager::clearSkillSlot(int globalSlotIndex) {
    if (globalSlotIndex < 0 || globalSlotIndex >= SKILL_BAR_SLOTS) {
        return;
    }
    skillBarSlots[globalSlotIndex] = "";
}

void SkillManager::swapSkillSlots(int slotA, int slotB) {
    if (slotA < 0 || slotA >= SKILL_BAR_SLOTS) return;
    if (slotB < 0 || slotB >= SKILL_BAR_SLOTS) return;
    std::swap(skillBarSlots[slotA], skillBarSlots[slotB]);
}

bool SkillManager::autoAssignToSkillBar(const std::string& skillId) {
    if (!hasSkill(skillId)) {
        return false;
    }

    // Check if already assigned somewhere
    for (int i = 0; i < SKILL_BAR_SLOTS; ++i) {
        if (skillBarSlots[i] == skillId) {
            return true;
        }
    }

    // Find first empty slot
    for (int i = 0; i < SKILL_BAR_SLOTS; ++i) {
        if (skillBarSlots[i].empty()) {
            skillBarSlots[i] = skillId;
            return true;
        }
    }

    return false;  // No empty slot available
}

// ============================================================================
// Skill Definition Registry
// ============================================================================

void SkillManager::registerSkillDefinition(const SkillDefinition& def) {
    skillDefinitions_[def.skillId] = def;
}

const SkillDefinition* SkillManager::getSkillDefinition(const std::string& skillId) const {
    auto it = skillDefinitions_.find(skillId);
    if (it == skillDefinitions_.end()) {
        return nullptr;
    }
    return &it->second;
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

// ============================================================================
// Passive Bonus Application
// ============================================================================

void SkillManager::applyPassiveBonusesToStats() {
    if (!stats) return;
    stats->passiveHPBonus         = passiveHPBonus_;
    stats->passiveCritBonus       = passiveCritBonus_;
    stats->passiveSpeedBonus      = passiveSpeedBonus_;
    stats->passiveDamageReduction = passiveDamageReduction_;
    stats->passiveStatBonus       = passiveStatBonus_;
    stats->passiveArmorBonus      = passiveArmorBonus_;
    stats->passiveHitRateBonus    = passiveHitRateBonus_;
    stats->passiveSpellDamageBonus = passiveSpellDamageBonus_;
    stats->recalculateStats();
}

// ============================================================================
// Skill Execution
// ============================================================================

int SkillManager::executeSkill(const std::string& skillId, int rank,
                                const SkillExecutionContext& ctx) {
    // ---- Validation Phase ----

    // 0. Caster must be alive
    if (ctx.casterStats && !ctx.casterStats->isAlive()) {
        if (onSkillFailed) onSkillFailed(skillId, "Caster is dead");
        return 0;
    }

    // 1. Check skill learned and activated (rank must be >= 1)
    if (rank < 1) {
        if (onSkillFailed) onSkillFailed(skillId, "Invalid rank");
        return 0;
    }
    const LearnedSkill* learned = getLearnedSkill(skillId);
    if (!learned || learned->activatedRank < rank) {
        if (onSkillFailed) onSkillFailed(skillId, "Skill not learned or rank not activated");
        return 0;
    }

    // 2. Check CC canAct
    if (ctx.casterCC && !ctx.casterCC->canAct()) {
        if (onSkillFailed) onSkillFailed(skillId, "Crowd controlled");
        return 0;
    }

    // 3. Check cooldown (skip if double-cast is active for this skill's source)
    bool isFreeCast = false;
    if (isDoubleCastReady() && doubleCastSourceSkillId_ == skillId) {
        isFreeCast = true;
    }

    if (!isFreeCast && isOnCooldown(skillId)) {
        if (onSkillFailed) onSkillFailed(skillId, "On cooldown");
        return 0;
    }

    // Look up skill definition
    const SkillDefinition* def = getSkillDefinition(skillId);
    if (!def) {
        if (onSkillFailed) onSkillFailed(skillId, "Unknown skill definition");
        return 0;
    }

    int ri = rank - 1;  // rank index (0-based)

    // 4. Check resource cost
    float cost = (ri < static_cast<int>(def->costPerRank.size()))
                 ? def->costPerRank[ri] : 0.0f;

    if (!isFreeCast && cost > 0.0f) {
        if (def->resourceType == ResourceType::Mana) {
            if (!stats || stats->currentMP < static_cast<int>(cost)) {
                if (onSkillFailed) onSkillFailed(skillId, "Not enough resources");
                return 0;
            }
        } else if (def->resourceType == ResourceType::Fury) {
            if (!stats || stats->currentFury < cost) {
                if (onSkillFailed) onSkillFailed(skillId, "Not enough resources");
                return 0;
            }
        }
    }

    // 5. Check target alive (for targeted skills)
    if (def->targetType != SkillTargetType::Self) {
        if (!ctx.targetAlive) {
            if (onSkillFailed) onSkillFailed(skillId, "Target is dead");
            return 0;
        }
    }

    // 6. Check range (for targeted skills) — range is in tiles, distance is in pixels
    if (def->targetType != SkillTargetType::Self && def->range > 0.0f) {
        float maxRange = def->range * 32.0f + 16.0f; // tiles to pixels + half-tile tolerance
        if (ctx.distanceToTarget > maxRange) {
            if (onSkillFailed) onSkillFailed(skillId, "Out of range");
            return 0;
        }
    }

    // 7. Check target type validity
    if (def->targetType == SkillTargetType::SingleEnemy && !ctx.targetMobStats && !ctx.targetIsPlayer) {
        if (onSkillFailed) onSkillFailed(skillId, "Invalid target");
        return 0;
    }

    // ---- Execution Phase ----

    // Arcane Mastery: chance to cast for free (mana skills only, does NOT skip cooldown)
    bool arcaneMasteryProc = false;
    if (!isFreeCast && stats && stats->arcaneMasteryActive && def->resourceType == ResourceType::Mana) {
        std::uniform_real_distribution<float> arcaneDist(0.0f, 1.0f);
        thread_local std::mt19937 arcaneMasteryRng{std::random_device{}()};
        if (arcaneDist(arcaneMasteryRng) < stats->arcaneMasteryChance / 100.0f) {
            arcaneMasteryProc = true; // skip resource deduction only
        }
    }

    // 1. Deduct resource cost (skip for free cast and ResourceType::None)
    if (!isFreeCast && !arcaneMasteryProc && def->resourceType != ResourceType::None) {
        if (def->scalesWithResource && def->resourceType == ResourceType::Mana && stats) {
            // Cataclysm: spend all remaining mana
            cost = static_cast<float>(stats->currentMP);
            stats->spendMana(stats->currentMP);
        } else if (cost > 0.0f && stats) {
            if (def->resourceType == ResourceType::Mana) {
                stats->spendMana(static_cast<int>(cost));
            } else if (def->resourceType == ResourceType::Fury) {
                stats->spendFury(cost);
            }
        }
    }

    // Consume double-cast if active
    if (isFreeCast) {
        consumeDoubleCast();
    }

    // 2. Start cooldown (skip for free cast)
    if (!isFreeCast) {
        float cd = (ri < static_cast<int>(def->cooldownPerRank.size()))
                   ? def->cooldownPerRank[ri] : def->cooldownSeconds;
        startCooldown(skillId, cd);
    }

    // Handle non-damaging skills (teleport/dash)
    if (def->teleportDistance > 0.0f || def->dashDistance > 0.0f) {
        // Movement is handled by the caller (system layer)
        // Fire callback and return
        if (def->enablesDoubleCast && !isFreeCast) {
            activateDoubleCast(skillId, def->doubleCastWindow);
        }
        if (onSkillUsed) onSkillUsed(skillId, rank);
        return 0;
    }

    // Handle self-only buff skills (no damage)
    if (def->targetType == SkillTargetType::Self && def->damagePerRank.empty()) {
        // Apply self-buffs
        if (ctx.casterSEM) {
            if (def->grantsInvulnerability) {
                float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                       ? def->effectDurationPerRank[ri] : 3.0f;
                ctx.casterSEM->applyInvulnerability(effectDuration, ctx.casterEntityId);
            }
            if (def->removesDebuffs) {
                ctx.casterSEM->removeAllDebuffs();
                if (ctx.casterCC) {
                    ctx.casterCC->removeAllCC();
                }
            }
            if (def->grantsStunImmunity) {
                float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                       ? def->effectDurationPerRank[ri] : 5.0f;
                ctx.casterSEM->applyEffect(EffectType::StunImmune, effectDuration,
                                            1.0f, 0.0f, ctx.casterEntityId);
            }
            if (def->grantsCritGuarantee) {
                float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                       ? def->effectDurationPerRank[ri] : 5.0f;
                ctx.casterSEM->applyEffect(EffectType::GuaranteedCrit, effectDuration,
                                            1.0f, 0.0f, ctx.casterEntityId);
            }
            if (def->transformDamageMult > 0.0f) {
                float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                       ? def->effectDurationPerRank[ri] : 10.0f;
                ctx.casterSEM->applyTransform(effectDuration, def->transformDamageMult,
                                               def->transformSpeedBonus, ctx.casterEntityId);
            }

            // Fortify: flat armor buff + self-root (can still attack)
            if (def->locksMovement && !def->effectValuePerRank.empty()) {
                float armorVal = (ri < static_cast<int>(def->effectValuePerRank.size()))
                                 ? def->effectValuePerRank[ri] : 0.0f;
                float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                            ? def->effectDurationPerRank[ri] : 3.0f;
                ctx.casterSEM->applyEffect(EffectType::ArmorBuff, dur, armorVal,
                                            0.0f, ctx.casterEntityId);
                if (ctx.casterCC) {
                    ctx.casterCC->applyRoot(dur, ctx.casterSEM, ctx.casterEntityId);
                }
            }

            // Poison Tip: coating that adds poison to next N attacks
            if (def->appliesPoison && !def->effectValuePerRank.empty()) {
                float charges = (ri < static_cast<int>(def->effectValuePerRank.size()))
                                ? def->effectValuePerRank[ri] : 3.0f;
                float poisonDur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                  ? def->effectDurationPerRank[ri] : 6.0f;
                ctx.casterSEM->applyEffect(EffectType::PoisonCoating, 0.0f, charges,
                                            poisonDur, ctx.casterEntityId);
            }

            // Mana Shield: absorption shield (effectDuration == 0 = permanent until broken)
            if (!def->locksMovement && !def->appliesPoison &&
                !def->grantsInvulnerability && !def->removesDebuffs &&
                !def->grantsStunImmunity && !def->grantsCritGuarantee &&
                def->transformDamageMult <= 0.0f &&
                !def->effectValuePerRank.empty()) {
                float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                       ? def->effectDurationPerRank[ri] : 0.0f;
                if (effectDuration <= 0.0f && stats) {
                    float shieldPct = (ri < static_cast<int>(def->effectValuePerRank.size()))
                                      ? def->effectValuePerRank[ri] : 0.0f;
                    float shieldAmount = stats->maxHP * shieldPct / 100.0f;
                    ctx.casterSEM->applyShield(shieldAmount, 0.0f, ctx.casterEntityId);
                }
            }

            // Meditation: mana regen buff (effectDuration > 0)
            if (!def->locksMovement && !def->appliesPoison &&
                !def->grantsInvulnerability && !def->removesDebuffs &&
                !def->grantsStunImmunity && !def->grantsCritGuarantee &&
                def->transformDamageMult <= 0.0f &&
                !def->effectValuePerRank.empty()) {
                float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                       ? def->effectDurationPerRank[ri] : 0.0f;
                if (effectDuration > 0.0f) {
                    float regenVal = (ri < static_cast<int>(def->effectValuePerRank.size()))
                                     ? def->effectValuePerRank[ri] : 0.0f;
                    ctx.casterSEM->applyEffect(EffectType::ManaRegenBuff, effectDuration,
                                                regenVal, 0.0f, ctx.casterEntityId);
                }
            }
        }
        if (def->enablesDoubleCast && !isFreeCast) {
            activateDoubleCast(skillId, def->doubleCastWindow);
        }
        if (onSkillUsed) onSkillUsed(skillId, rank);
        return 0;
    }

    // 3. Hit roll
    if (def->usesHitRate && stats) {
        bool isMage = (stats->classDef.classType == ClassType::Mage);

        if (isMage) {
            // Mages use spell resist
            int targetMR = ctx.targetMagicResist;
            bool resisted = CombatSystem::rollSpellResist(
                stats->level, stats->getIntelligence(),
                ctx.targetLevel, targetMR);
            if (resisted) {
                if (onSkillFailed) onSkillFailed(skillId, "Spell resisted");
                return 0;
            }
        } else {
            // Physical uses hit rate
            int targetEvasion = 0;  // mobs have 0 evasion by default
            bool hit = CombatSystem::rollToHit(
                stats->level, static_cast<int>(stats->getHitRate()),
                ctx.targetLevel, targetEvasion);
            if (!hit) {
                if (onSkillFailed) onSkillFailed(skillId, "Attack missed");
                return 0;
            }
        }
    }

    // 4. Calculate base damage
    bool isCrit = false;
    bool forceCrit = false;
    if (def->canCrit && ctx.casterSEM && ctx.casterSEM->hasGuaranteedCrit()) {
        forceCrit = true;
    }

    // Steady Aim: guaranteed crit after standing still for 5s
    if (stats && stats->steadyAimReady) {
        forceCrit = true;
        stats->steadyAimReady = false;
        stats->steadyAimTimer = 0.0f;
    }

    int damage = 0;
    if (stats) {
        damage = stats->calculateDamage(forceCrit, isCrit);
        if (forceCrit && isCrit && ctx.casterSEM) {
            ctx.casterSEM->consumeGuaranteedCrit();
        }
    }

    // Apply skill damage percent
    float skillPercent = (ri < static_cast<int>(def->damagePerRank.size()))
                         ? def->damagePerRank[ri] : 100.0f;
    damage = static_cast<int>(std::round(damage * skillPercent / 100.0f));

    // Passive spell damage bonus (Arcane Intellect)
    if (stats && stats->passiveSpellDamageBonus > 0.0f &&
        (def->damageType == DamageType::Magic || def->damageType == DamageType::Fire ||
         def->damageType == DamageType::Water || def->damageType == DamageType::Lightning ||
         def->damageType == DamageType::Void)) {
        damage = static_cast<int>(std::round(damage * (1.0f + stats->passiveSpellDamageBonus / 100.0f)));
    }

    // Apply status effect damage multiplier on caster
    if (ctx.casterSEM) {
        float seMult = ctx.casterSEM->getDamageMultiplier();
        damage = static_cast<int>(std::round(damage * seMult));
    }

    // 5. Level multiplier (PvE only)
    if (!ctx.targetIsPlayer && stats) {
        float levelMult = CombatSystem::calculateDamageMultiplier(stats->level, ctx.targetLevel);
        damage = static_cast<int>(std::round(damage * levelMult));
    }

    // 6. PvP multiplier
    if (ctx.targetIsPlayer) {
        damage = static_cast<int>(std::round(damage * 0.30f));
    }

    // Cataclysm scaling: damage * (manaSpent / baseCost)
    if (def->scalesWithResource && !isFreeCast) {
        float baseCost = (ri < static_cast<int>(def->costPerRank.size()))
                         ? def->costPerRank[ri] : 1.0f;
        if (baseCost > 0.0f) {
            damage = static_cast<int>(std::round(damage * (cost / baseCost)));
        }
    }

    // 7. Execute check (skill threshold + equipment threshold stack)
    if (!ctx.targetIsBoss && ctx.targetMaxHP > 0) {
        float skillThreshold = 0.0f;
        if (!def->executeThresholdPerRank.empty()) {
            skillThreshold = (ri < static_cast<int>(def->executeThresholdPerRank.size()))
                             ? def->executeThresholdPerRank[ri] : 0.0f;
        }
        float equipThreshold = stats ? stats->equipExecuteThreshold : 0.0f;
        float totalThreshold = skillThreshold + equipThreshold;

        if (totalThreshold > 0.0f) {
            float hpPercent = static_cast<float>(ctx.targetCurrentHP) /
                              static_cast<float>(ctx.targetMaxHP);
            if (hpPercent <= totalThreshold) {
                // Execute: deal remaining HP as damage, boosted by equipment execute bonus
                float execBonus = 1.0f + (stats ? stats->equipExecuteDamageBonus : 0.0f);
                damage = static_cast<int>(std::round(ctx.targetCurrentHP * execBonus));
            }
        }
    }

    // 8. Defense pipeline on target

    // Shield absorption
    if (ctx.targetSEM) {
        damage = ctx.targetSEM->absorbDamage(damage);
    }

    // Block roll (physical only, player targets only)
    if (def->damageType == DamageType::Physical && ctx.targetIsPlayer && ctx.targetPlayerStats) {
        float blockChance = ctx.targetPlayerStats->getBlockChance();
        if (stats && blockChance > 0.0f) {
            bool blocked = CombatSystem::rollBlock(
                stats->classDef.classType,
                stats->getStrength(),
                stats->getDexterity(),
                blockChance);
            if (blocked) {
                damage = 0;
                // Retaliation: flag next attack bonus after a successful block
                if (ctx.targetPlayerStats->retaliationActive) {
                    ctx.targetPlayerStats->retaliationReady = true;
                }
            }
        }
    }

    // Armor / MR reduction
    if (damage > 0) {
        bool isMagicDamage = (def->damageType == DamageType::Magic ||
                              def->damageType == DamageType::Fire ||
                              def->damageType == DamageType::Water ||
                              def->damageType == DamageType::Lightning ||
                              def->damageType == DamageType::Void);

        if (def->damageType != DamageType::True) {
            if (isMagicDamage) {
                // Magic resistance
                float mrReduction = ctx.targetIsPlayer
                    ? CombatSystem::getPlayerMagicDamageReduction(ctx.targetMagicResist)
                    : CombatSystem::getMobMagicDamageReduction(ctx.targetMagicResist);
                damage = static_cast<int>(std::round(damage * (1.0f - mrReduction)));
                // Elemental resist (from equipment, stacks multiplicatively with MR)
                float elemResist = ctx.targetElementalResists[static_cast<int>(def->damageType)];
                if (elemResist > 0.0f && damage > 0) {
                    damage = static_cast<int>(std::round(damage * (1.0f - elemResist)));
                }
            } else {
                // Physical armor (reduced by ArmorShred stacks on target)
                float effectiveArmor = ctx.targetArmor;
                if (ctx.targetSEM) {
                    float shred = ctx.targetSEM->getArmorShred();
                    if (shred > 0.0f) effectiveArmor = (std::max)(0.0f, effectiveArmor - shred);
                }
                damage = CombatSystem::applyArmorReduction(damage, effectiveArmor);
            }
        }
    }

    // 9. Hunter's Mark
    if (ctx.targetSEM) {
        float bonusTaken = ctx.targetSEM->getBonusDamageTaken();
        if (bonusTaken > 0.0f) {
            damage = static_cast<int>(std::round(damage * (1.0f + bonusTaken)));
        }
    }

    // 10. Bewitch (mobs only)
    if (!ctx.targetIsPlayer && ctx.targetSEM) {
        float bewitchMult = ctx.targetSEM->consumeBewitch(ctx.casterEntityId);
        if (bewitchMult > 0.0f) {
            damage = static_cast<int>(std::round(damage * bewitchMult));
        }
    }

    // Ensure minimum damage of 1 (if we should deal damage at all)
    if (damage > 0) {
        damage = (std::max)(1, damage);
    }

    // H3: Re-validate target is still alive before applying damage
    bool targetStillAlive = true;
    if (ctx.targetMobStats) targetStillAlive = ctx.targetMobStats->isAlive;
    else if (ctx.targetPlayerStats) targetStillAlive = ctx.targetPlayerStats->isAlive();
    if (!targetStillAlive) {
        return 0; // Target died between validation and execution
    }

    // H4: Re-validate caster is still alive before applying damage
    if (ctx.casterStats && !ctx.casterStats->isAlive()) {
        return 0; // Caster died before hit landed
    }

    // 11. Apply damage to target
    int actualDamage = damage;
    if (ctx.targetMobStats && damage > 0) {
        ctx.targetMobStats->takeDamageFrom(ctx.casterEntityId, damage);
        actualDamage = damage;  // takeDamageFrom doesn't return actual
    } else if (ctx.targetPlayerStats && damage > 0) {
        actualDamage = ctx.targetPlayerStats->takeDamage(damage);
    }

    // 12. Freeze break
    if (ctx.targetCC && actualDamage > 0) {
        ctx.targetCC->breakFreeze();
    }

    // 13. Apply status effects to target
    if (ctx.targetSEM && actualDamage > 0) {
        float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                               ? def->effectDurationPerRank[ri] : 0.0f;
        float effectValue = (ri < static_cast<int>(def->effectValuePerRank.size()))
                            ? def->effectValuePerRank[ri] : 0.0f;

        if (def->appliesBleed && effectDuration > 0.0f) {
            ctx.targetSEM->applyDoT(EffectType::Bleed, effectDuration,
                                     effectValue, ctx.casterEntityId);
        }
        if (def->appliesBurn && effectDuration > 0.0f) {
            ctx.targetSEM->applyDoT(EffectType::Burn, effectDuration,
                                     effectValue, ctx.casterEntityId);
        }
        if (def->appliesPoison && effectDuration > 0.0f) {
            ctx.targetSEM->applyDoT(EffectType::Poison, effectDuration,
                                     effectValue, ctx.casterEntityId);
        }
        if (def->appliesSlow && effectDuration > 0.0f) {
            ctx.targetSEM->applyEffect(EffectType::Slow, effectDuration,
                                        effectValue, 0.0f, ctx.casterEntityId);
        }
        if (def->appliesFreeze && effectDuration > 0.0f && ctx.targetCC) {
            ctx.targetCC->applyFreeze(effectDuration, ctx.targetSEM, ctx.casterEntityId);
            // Interrupt any active cast or channel when CC is applied
            if (ctx.targetPlayerStats && ctx.targetPlayerStats->isCasting()) {
                ctx.targetPlayerStats->interruptCast();
            }
            if (ctx.targetPlayerStats && ctx.targetPlayerStats->isChanneling()) {
                ctx.targetPlayerStats->interruptChannel();
            }
        }
    }

    // 14. Apply crowd control (stun — only if skill doesn't already apply freeze above)
    if (ctx.targetCC && ctx.targetSEM && actualDamage > 0) {
        float stunDuration = (ri < static_cast<int>(def->stunDurationPerRank.size()))
                             ? def->stunDurationPerRank[ri] : 0.0f;
        if (stunDuration > 0.0f && !def->appliesFreeze) {
            ctx.targetCC->applyStun(stunDuration, ctx.targetSEM, ctx.casterEntityId);
            // Interrupt any active cast or channel when CC is applied
            if (ctx.targetPlayerStats && ctx.targetPlayerStats->isCasting()) {
                ctx.targetPlayerStats->interruptCast();
            }
            if (ctx.targetPlayerStats && ctx.targetPlayerStats->isChanneling()) {
                ctx.targetPlayerStats->interruptChannel();
            }
        }
    }

    // 15. Apply self-buffs
    if (ctx.casterSEM) {
        if (def->grantsInvulnerability) {
            float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                   ? def->effectDurationPerRank[ri] : 3.0f;
            ctx.casterSEM->applyInvulnerability(effectDuration, ctx.casterEntityId);
        }
        if (def->removesDebuffs) {
            ctx.casterSEM->removeAllDebuffs();
            if (ctx.casterCC) {
                ctx.casterCC->removeAllCC();
            }
        }
        if (def->grantsStunImmunity) {
            float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                   ? def->effectDurationPerRank[ri] : 5.0f;
            ctx.casterSEM->applyEffect(EffectType::StunImmune, effectDuration,
                                        1.0f, 0.0f, ctx.casterEntityId);
        }
        if (def->grantsCritGuarantee) {
            float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                   ? def->effectDurationPerRank[ri] : 5.0f;
            ctx.casterSEM->applyEffect(EffectType::GuaranteedCrit, effectDuration,
                                        1.0f, 0.0f, ctx.casterEntityId);
        }
        if (def->transformDamageMult > 0.0f) {
            float effectDuration = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                                   ? def->effectDurationPerRank[ri] : 10.0f;
            ctx.casterSEM->applyTransform(effectDuration, def->transformDamageMult,
                                           def->transformSpeedBonus, ctx.casterEntityId);
        }
    }

    // 16. Lifesteal (capped at HP target actually had before THIS hit)
    //     After takeDamage, target HP = (pre-hit HP - actualDamage). So pre-hit HP
    //     for this specific hit = current HP + actualDamage. Cap lifesteal at that.
    if (stats && stats->equipBonusLifesteal > 0.0f && actualDamage > 0) {
        int preHitHP = 0;
        if (ctx.targetMobStats) preHitHP = ctx.targetMobStats->currentHP + actualDamage;
        else if (ctx.targetPlayerStats) preHitHP = ctx.targetPlayerStats->currentHP + actualDamage;
        int effectiveDamage = (std::min)(actualDamage, preHitHP);
        int healAmount = static_cast<int>(std::round(stats->equipBonusLifesteal * effectiveDamage));
        if (healAmount > 0) {
            stats->heal(healAmount);
        }
    }

    // 17. Fury on hit
    if (stats && def->furyOnHit > 0.0f) {
        stats->addFury(def->furyOnHit);
    }

    // 18. Armor shred on crit
    if (def->canCrit && isCrit && ctx.targetSEM) {
        float armorShredValue = (stats && stats->equipBonusArmorPierce > 0)
            ? static_cast<float>(stats->equipBonusArmorPierce)
            : 5.0f;
        ctx.targetSEM->applyEffect(EffectType::ArmorShred, 5.0f,
                                    armorShredValue, 0.0f, ctx.casterEntityId);
    }

    // Exploit Weakness: additional armor shred on crit (up to 3 stacks)
    if (isCrit && stats && stats->exploitWeaknessActive && ctx.targetSEM) {
        int stacks = ctx.targetSEM->getEffectStacks(EffectType::ArmorShred);
        if (stacks < 3) {
            ctx.targetSEM->applyEffect(EffectType::ArmorShred, 5.0f,
                stats->exploitWeaknessValue, 0.0f, ctx.casterEntityId);
        }
    }

    // Enable double-cast window if applicable (only on normal cast, not the free cast itself)
    if (def->enablesDoubleCast && !isFreeCast) {
        activateDoubleCast(skillId, def->doubleCastWindow);
    }

    // 19. Fire callback
    if (onSkillUsed) onSkillUsed(skillId, rank);

    // 20. Return actual damage dealt
    return actualDamage;
}

// ============================================================================
// AOE Skill Execution
// ============================================================================

int SkillManager::executeSkillAOE(const std::string& skillId, int rank,
                                   const SkillExecutionContext& primaryCtx,
                                   std::vector<SkillExecutionContext>& targets) {
    // Caster must be alive
    if (primaryCtx.casterStats && !primaryCtx.casterStats->isAlive()) {
        if (onSkillFailed) onSkillFailed(skillId, "Caster is dead");
        return 0;
    }

    // Validate using primary context (resource cost, cooldown, CC check)
    const LearnedSkill* learned = getLearnedSkill(skillId);
    if (!learned || learned->activatedRank < rank) {
        if (onSkillFailed) onSkillFailed(skillId, "Skill not learned or rank not activated");
        return 0;
    }

    if (primaryCtx.casterCC && !primaryCtx.casterCC->canAct()) {
        if (onSkillFailed) onSkillFailed(skillId, "Crowd controlled");
        return 0;
    }

    bool isFreeCast = false;
    if (isDoubleCastReady() && doubleCastSourceSkillId_ == skillId) {
        isFreeCast = true;
    }

    if (!isFreeCast && isOnCooldown(skillId)) {
        if (onSkillFailed) onSkillFailed(skillId, "On cooldown");
        return 0;
    }

    const SkillDefinition* def = getSkillDefinition(skillId);
    if (!def) {
        if (onSkillFailed) onSkillFailed(skillId, "Unknown skill definition");
        return 0;
    }

    int ri = rank - 1;
    float cost = (ri < static_cast<int>(def->costPerRank.size()))
                 ? def->costPerRank[ri] : 0.0f;

    // Check resources
    if (!isFreeCast && cost > 0.0f) {
        if (def->resourceType == ResourceType::Mana) {
            if (!stats || stats->currentMP < static_cast<int>(cost)) {
                if (onSkillFailed) onSkillFailed(skillId, "Not enough resources");
                return 0;
            }
        } else if (def->resourceType == ResourceType::Fury) {
            if (!stats || stats->currentFury < cost) {
                if (onSkillFailed) onSkillFailed(skillId, "Not enough resources");
                return 0;
            }
        }
    }

    // Deduct cost once
    if (!isFreeCast && stats) {
        if (def->scalesWithResource && def->resourceType == ResourceType::Mana) {
            cost = static_cast<float>(stats->currentMP);
            stats->spendMana(stats->currentMP);
        } else if (cost > 0.0f) {
            if (def->resourceType == ResourceType::Mana) {
                stats->spendMana(static_cast<int>(cost));
            } else {
                stats->spendFury(cost);
            }
        }
    }

    if (isFreeCast) {
        consumeDoubleCast();
    }

    // Start cooldown once
    if (!isFreeCast) {
        float cd = (ri < static_cast<int>(def->cooldownPerRank.size()))
                   ? def->cooldownPerRank[ri] : def->cooldownSeconds;
        startCooldown(skillId, cd);
    }

    // Cap targets
    int maxTargets = (ri < static_cast<int>(def->maxTargetsPerRank.size()))
                     ? def->maxTargetsPerRank[ri] : static_cast<int>(targets.size());
    int targetCount = (std::min)(static_cast<int>(targets.size()), maxTargets);

    int totalDamage = 0;

    // Execute against each target (skip resource/cooldown validation per-target)
    for (int i = 0; i < targetCount; ++i) {
        // H5: Re-check caster CC status before each AOE target hit
        if (primaryCtx.casterCC && !primaryCtx.casterCC->canAct()) {
            break; // Stop hitting remaining targets if caster got CC'd
        }

        auto& tctx = targets[i];

        if (!tctx.targetAlive) continue;

        // Hit roll per target
        bool missed = false;
        if (def->usesHitRate && stats) {
            bool isMage = (stats->classDef.classType == ClassType::Mage);
            if (isMage) {
                missed = CombatSystem::rollSpellResist(
                    stats->level, stats->getIntelligence(),
                    tctx.targetLevel, tctx.targetMagicResist);
            } else {
                missed = !CombatSystem::rollToHit(
                    stats->level, static_cast<int>(stats->getHitRate()),
                    tctx.targetLevel, 0);
            }
        }
        if (missed) continue;

        // Calculate damage per target
        bool isCrit = false;
        bool forceCrit = (def->canCrit && primaryCtx.casterSEM && primaryCtx.casterSEM->hasGuaranteedCrit());
        int damage = stats ? stats->calculateDamage(forceCrit, isCrit) : 0;
        if (forceCrit && isCrit && primaryCtx.casterSEM) {
            primaryCtx.casterSEM->consumeGuaranteedCrit();
        }

        float skillPercent = (ri < static_cast<int>(def->damagePerRank.size()))
                             ? def->damagePerRank[ri] : 100.0f;
        damage = static_cast<int>(std::round(damage * skillPercent / 100.0f));

        if (primaryCtx.casterSEM) {
            damage = static_cast<int>(std::round(damage * primaryCtx.casterSEM->getDamageMultiplier()));
        }

        if (!tctx.targetIsPlayer && stats) {
            damage = static_cast<int>(std::round(damage *
                CombatSystem::calculateDamageMultiplier(stats->level, tctx.targetLevel)));
        }
        if (tctx.targetIsPlayer) {
            damage = static_cast<int>(std::round(damage * 0.30f));
        }

        // Cataclysm scaling
        if (def->scalesWithResource && !isFreeCast) {
            float baseCost = (ri < static_cast<int>(def->costPerRank.size()))
                             ? def->costPerRank[ri] : 1.0f;
            if (baseCost > 0.0f) {
                damage = static_cast<int>(std::round(damage * (cost / baseCost)));
            }
        }

        // Defense pipeline
        if (tctx.targetSEM) {
            damage = tctx.targetSEM->absorbDamage(damage);
        }

        bool isMagicDamage = (def->damageType == DamageType::Magic ||
                              def->damageType == DamageType::Fire ||
                              def->damageType == DamageType::Water ||
                              def->damageType == DamageType::Lightning ||
                              def->damageType == DamageType::Void);

        if (def->damageType != DamageType::True && damage > 0) {
            if (isMagicDamage) {
                float mr = tctx.targetIsPlayer
                    ? CombatSystem::getPlayerMagicDamageReduction(tctx.targetMagicResist)
                    : CombatSystem::getMobMagicDamageReduction(tctx.targetMagicResist);
                damage = static_cast<int>(std::round(damage * (1.0f - mr)));
                // Elemental resist (from equipment, stacks multiplicatively with MR)
                float elemResist = tctx.targetElementalResists[static_cast<int>(def->damageType)];
                if (elemResist > 0.0f && damage > 0) {
                    damage = static_cast<int>(std::round(damage * (1.0f - elemResist)));
                }
            } else {
                damage = CombatSystem::applyArmorReduction(damage, tctx.targetArmor);
            }
        }

        if (tctx.targetSEM) {
            float bonus = tctx.targetSEM->getBonusDamageTaken();
            if (bonus > 0.0f) damage = static_cast<int>(std::round(damage * (1.0f + bonus)));
        }

        damage = (std::max)(1, damage);

        // Apply damage
        int actualDamage = damage;
        if (tctx.targetMobStats) {
            tctx.targetMobStats->takeDamageFrom(primaryCtx.casterEntityId, damage);
        } else if (tctx.targetPlayerStats) {
            actualDamage = tctx.targetPlayerStats->takeDamage(damage);
        }

        if (tctx.targetCC) tctx.targetCC->breakFreeze();

        // Apply effects per target
        if (tctx.targetSEM) {
            float eDur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                         ? def->effectDurationPerRank[ri] : 0.0f;
            float eVal = (ri < static_cast<int>(def->effectValuePerRank.size()))
                         ? def->effectValuePerRank[ri] : 0.0f;

            if (def->appliesBleed && eDur > 0.0f)
                tctx.targetSEM->applyDoT(EffectType::Bleed, eDur, eVal, primaryCtx.casterEntityId);
            if (def->appliesBurn && eDur > 0.0f)
                tctx.targetSEM->applyDoT(EffectType::Burn, eDur, eVal, primaryCtx.casterEntityId);
            if (def->appliesPoison && eDur > 0.0f)
                tctx.targetSEM->applyDoT(EffectType::Poison, eDur, eVal, primaryCtx.casterEntityId);
            if (def->appliesSlow && eDur > 0.0f)
                tctx.targetSEM->applyEffect(EffectType::Slow, eDur, eVal, 0.0f, primaryCtx.casterEntityId);
        }
        if (tctx.targetCC && tctx.targetSEM) {
            float stunDur = (ri < static_cast<int>(def->stunDurationPerRank.size()))
                            ? def->stunDurationPerRank[ri] : 0.0f;
            if (stunDur > 0.0f) {
                if (def->appliesFreeze)
                    tctx.targetCC->applyFreeze(stunDur, tctx.targetSEM, primaryCtx.casterEntityId);
                else
                    tctx.targetCC->applyStun(stunDur, tctx.targetSEM, primaryCtx.casterEntityId);
                // Interrupt any active cast or channel when CC is applied (AOE path)
                if (tctx.targetPlayerStats && tctx.targetPlayerStats->isCasting()) {
                    tctx.targetPlayerStats->interruptCast();
                }
                if (tctx.targetPlayerStats && tctx.targetPlayerStats->isChanneling()) {
                    tctx.targetPlayerStats->interruptChannel();
                }
            }
        }

        totalDamage += actualDamage;
    }

    // Self-buffs (once, not per-target)
    if (primaryCtx.casterSEM) {
        if (def->grantsInvulnerability) {
            float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                        ? def->effectDurationPerRank[ri] : 3.0f;
            primaryCtx.casterSEM->applyInvulnerability(dur, primaryCtx.casterEntityId);
        }
        if (def->removesDebuffs) {
            primaryCtx.casterSEM->removeAllDebuffs();
            if (primaryCtx.casterCC) primaryCtx.casterCC->removeAllCC();
        }
        if (def->grantsStunImmunity) {
            float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                        ? def->effectDurationPerRank[ri] : 5.0f;
            primaryCtx.casterSEM->applyEffect(EffectType::StunImmune, dur, 1.0f, 0.0f, primaryCtx.casterEntityId);
        }
        if (def->grantsCritGuarantee) {
            float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                        ? def->effectDurationPerRank[ri] : 5.0f;
            primaryCtx.casterSEM->applyEffect(EffectType::GuaranteedCrit, dur, 1.0f, 0.0f, primaryCtx.casterEntityId);
        }
        if (def->transformDamageMult > 0.0f) {
            float dur = (ri < static_cast<int>(def->effectDurationPerRank.size()))
                        ? def->effectDurationPerRank[ri] : 10.0f;
            primaryCtx.casterSEM->applyTransform(dur, def->transformDamageMult,
                                                  def->transformSpeedBonus, primaryCtx.casterEntityId);
        }
    }

    // Lifesteal on total damage
    if (stats && stats->equipBonusLifesteal > 0.0f && totalDamage > 0) {
        int heal = static_cast<int>(std::round(stats->equipBonusLifesteal * totalDamage));
        if (heal > 0) stats->heal(heal);
    }

    // Fury on hit
    if (stats && def->furyOnHit > 0.0f) {
        stats->addFury(def->furyOnHit);
    }

    if (def->enablesDoubleCast) {
        activateDoubleCast(skillId, def->doubleCastWindow);
    }

    if (onSkillUsed) onSkillUsed(skillId, rank);
    return totalDamage;
}

} // namespace fate
