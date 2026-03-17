#include "game/shared/quest_manager.h"
#include "game/shared/quest_data.h"
#include "engine/core/logger.h"
#include "game/shared/character_stats.h"
#include "game/shared/inventory.h"

#include <algorithm>

namespace fate {

// ============================================================================
// ActiveQuest
// ============================================================================
bool ActiveQuest::isReadyToTurnIn(const QuestDefinition& def) const {
    if (objectiveProgress.size() != def.objectives.size()) return false;
    for (size_t i = 0; i < def.objectives.size(); ++i) {
        if (objectiveProgress[i] < def.objectives[i].requiredCount) return false;
    }
    return true;
}

// ============================================================================
// QuestManager — Query
// ============================================================================
bool QuestManager::canAcceptQuest(uint32_t questId, int playerLevel) const {
    // Already active or completed
    if (isQuestActive(questId) || hasCompletedQuest(questId)) return false;

    // Max active quests
    if (static_cast<int>(activeQuests_.size()) >= MAX_ACTIVE_QUESTS) return false;

    const auto* def = QuestData::getQuest(questId);
    if (!def) return false;

    // Level requirement
    if (playerLevel < def->requiredLevel) return false;

    // Prerequisite quests
    for (auto prereq : def->prerequisiteQuestIds) {
        if (!hasCompletedQuest(prereq)) return false;
    }

    return true;
}

bool QuestManager::isQuestActive(uint32_t questId) const {
    return std::any_of(activeQuests_.begin(), activeQuests_.end(),
        [questId](const ActiveQuest& aq) { return aq.questId == questId; });
}

bool QuestManager::isQuestComplete(uint32_t questId) const {
    const auto* aq = getActiveQuest(questId);
    if (!aq) return false;
    const auto* def = QuestData::getQuest(questId);
    if (!def) return false;
    return aq->isReadyToTurnIn(*def);
}

bool QuestManager::hasCompletedQuest(uint32_t questId) const {
    return std::find(completedQuestIds_.begin(), completedQuestIds_.end(), questId)
        != completedQuestIds_.end();
}

const ActiveQuest* QuestManager::getActiveQuest(uint32_t questId) const {
    for (const auto& aq : activeQuests_) {
        if (aq.questId == questId) return &aq;
    }
    return nullptr;
}

const std::vector<ActiveQuest>& QuestManager::getActiveQuests() const {
    return activeQuests_;
}

// ============================================================================
// QuestManager — Lifecycle
// ============================================================================
bool QuestManager::acceptQuest(uint32_t questId, int playerLevel) {
    if (!canAcceptQuest(questId, playerLevel)) return false;

    const auto* def = QuestData::getQuest(questId);
    if (!def) return false;

    ActiveQuest aq;
    aq.questId = questId;
    aq.objectiveProgress.resize(def->objectives.size(), 0);
    activeQuests_.push_back(std::move(aq));

    if (onQuestAccepted) onQuestAccepted(questId);
    return true;
}

bool QuestManager::abandonQuest(uint32_t questId) {
    auto it = std::find_if(activeQuests_.begin(), activeQuests_.end(),
        [questId](const ActiveQuest& aq) { return aq.questId == questId; });
    if (it == activeQuests_.end()) return false;
    activeQuests_.erase(it);
    return true;
}

bool QuestManager::turnInQuest(uint32_t questId, CharacterStats& stats, Inventory& inventory) {
    auto it = std::find_if(activeQuests_.begin(), activeQuests_.end(),
        [questId](const ActiveQuest& aq) { return aq.questId == questId; });
    if (it == activeQuests_.end()) return false;

    const auto* def = QuestData::getQuest(questId);
    if (!def) return false;

    if (!it->isReadyToTurnIn(*def)) return false;

    // Check and consume deliver items from inventory
    for (size_t i = 0; i < def->objectives.size(); ++i) {
        if (def->objectives[i].type == ObjectiveType::Deliver) {
            const auto& targetId = def->objectives[i].targetId;
            int required = def->objectives[i].requiredCount;
            if (inventory.countItem(targetId) < required) return false;
        }
    }

    // Actually remove deliver items
    for (size_t i = 0; i < def->objectives.size(); ++i) {
        if (def->objectives[i].type == ObjectiveType::Deliver) {
            const auto& targetId = def->objectives[i].targetId;
            int remaining = def->objectives[i].requiredCount;
            while (remaining > 0) {
                int slot = inventory.findItemById(targetId);
                if (slot < 0) break;
                auto item = inventory.getSlot(slot);
                int toRemove = std::min(remaining, item.quantity);
                inventory.removeItemQuantity(slot, toRemove);
                remaining -= toRemove;
            }
        }
    }

    // Grant rewards
    if (def->rewards.xp > 0) {
        stats.addXP(static_cast<int64_t>(def->rewards.xp));
    }
    if (def->rewards.gold > 0) {
        inventory.addGold(def->rewards.gold);
    }
    // Item rewards would be granted here via inventory.addItem() if needed

    // Mark completed
    completedQuestIds_.push_back(questId);
    activeQuests_.erase(it);

    if (onQuestCompleted) onQuestCompleted(questId);
    return true;
}

// ============================================================================
// QuestManager — Progress Events
// ============================================================================
void QuestManager::onMobKilled(const std::string& mobId) {
    progressObjectives(ObjectiveType::Kill, mobId);
}

void QuestManager::onItemCollected(const std::string& itemId) {
    progressObjectives(ObjectiveType::Collect, itemId);
}

void QuestManager::onNPCTalkedTo(const std::string& npcId) {
    progressObjectives(ObjectiveType::TalkTo, npcId);
}

void QuestManager::onPvPKill() {
    progressObjectives(ObjectiveType::PvPKills, "");
}

void QuestManager::onDeliverAttempt(const std::string& npcId, Inventory& inventory) {
    for (auto& aq : activeQuests_) {
        const auto* def = QuestData::getQuest(aq.questId);
        if (!def) continue;
        if (def->turnInNpcId != npcId) continue;

        for (size_t i = 0; i < def->objectives.size(); ++i) {
            if (def->objectives[i].type != ObjectiveType::Deliver) continue;
            const auto& targetId = def->objectives[i].targetId;
            int count = inventory.countItem(targetId);
            uint16_t progress = static_cast<uint16_t>(
                std::min(count, static_cast<int>(def->objectives[i].requiredCount)));
            if (progress != aq.objectiveProgress[i]) {
                aq.objectiveProgress[i] = progress;
                if (onObjectiveProgress) {
                    onObjectiveProgress(aq.questId, static_cast<int>(i),
                        progress, def->objectives[i].requiredCount);
                }
            }
        }
    }
}

void QuestManager::progressObjectives(ObjectiveType type, const std::string& targetId) {
    for (auto& aq : activeQuests_) {
        const auto* def = QuestData::getQuest(aq.questId);
        if (!def) continue;

        for (size_t i = 0; i < def->objectives.size(); ++i) {
            if (def->objectives[i].type != type) continue;

            // PvPKills has no targetId to match
            if (type != ObjectiveType::PvPKills && def->objectives[i].targetId != targetId) continue;

            if (aq.objectiveProgress[i] < def->objectives[i].requiredCount) {
                aq.objectiveProgress[i]++;
                if (onObjectiveProgress) {
                    onObjectiveProgress(aq.questId, static_cast<int>(i),
                        aq.objectiveProgress[i], def->objectives[i].requiredCount);
                }
            }
        }
    }
}

} // namespace fate
