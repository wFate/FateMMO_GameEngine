#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fate {

// Forward declarations
class CharacterStats;
class Inventory;

// ============================================================================
// Quest Enums
// ============================================================================
enum class QuestTier : uint8_t {
    Starter    = 0,
    Novice     = 1,
    Apprentice = 2,
    Adept      = 3
};

enum class ObjectiveType : uint8_t {
    Kill,
    Collect,
    Deliver,
    TalkTo,
    PvPKills
};

// ============================================================================
// Quest Data Structures
// ============================================================================
struct QuestObjective {
    ObjectiveType type{};
    std::string description;
    std::string targetId;
    uint16_t requiredCount = 1;
};

struct ItemReward {
    std::string itemId;
    uint16_t quantity = 1;
};

struct QuestRewards {
    uint32_t xp = 0;
    int64_t gold = 0;
    std::vector<ItemReward> items;
};

struct QuestDefinition {
    uint32_t questId = 0;
    std::string questName;
    std::string description;
    std::string offerDialogue;
    std::string inProgressDialogue;
    std::string turnInDialogue;
    QuestTier tier = QuestTier::Starter;
    int requiredLevel = 0;
    std::string turnInNpcId;
    std::vector<uint32_t> prerequisiteQuestIds;
    std::vector<QuestObjective> objectives;
    QuestRewards rewards;
};

struct ActiveQuest {
    uint32_t questId = 0;
    std::vector<uint16_t> objectiveProgress;

    [[nodiscard]] bool isReadyToTurnIn(const QuestDefinition& def) const;
};

// ============================================================================
// QuestManager — Tracks active/completed quests and handles progression
// ============================================================================
class QuestManager {
public:
    static constexpr int MAX_ACTIVE_QUESTS = 10;

    // ---- Query -------------------------------------------------------------
    [[nodiscard]] bool canAcceptQuest(uint32_t questId, int playerLevel) const;
    [[nodiscard]] bool isQuestActive(uint32_t questId) const;
    [[nodiscard]] bool isQuestComplete(uint32_t questId) const;
    [[nodiscard]] bool hasCompletedQuest(uint32_t questId) const;
    [[nodiscard]] const ActiveQuest* getActiveQuest(uint32_t questId) const;
    [[nodiscard]] const std::vector<ActiveQuest>& getActiveQuests() const;

    // ---- Quest Lifecycle ---------------------------------------------------
    bool acceptQuest(uint32_t questId, int playerLevel);
    bool abandonQuest(uint32_t questId);
    bool turnInQuest(uint32_t questId, CharacterStats& stats, Inventory& inventory);

    // ---- Progress Events ---------------------------------------------------
    void onMobKilled(const std::string& mobId);
    void onItemCollected(const std::string& itemId);
    void onNPCTalkedTo(const std::string& npcId);
    void onPvPKill();
    void onDeliverAttempt(const std::string& npcId, Inventory& inventory);

    // ---- Client-side state updates (from server messages) ------------------
    void markCompleted(uint32_t questId);
    void setProgress(uint32_t questId, int32_t currentCount, int32_t targetCount);

    // ---- Serialization accessors -------------------------------------------
    [[nodiscard]] const std::vector<uint32_t>& getCompletedQuestIds() const { return completedQuestIds_; }
    void setSerializedState(std::vector<uint32_t> completed, std::vector<ActiveQuest> active) {
        completedQuestIds_ = std::move(completed);
        activeQuests_ = std::move(active);
    }

    // ---- Callbacks ---------------------------------------------------------
    std::function<void(uint32_t questId)> onQuestAccepted;
    std::function<void(uint32_t questId)> onQuestCompleted;
    std::function<void(uint32_t questId, int objectiveIndex, uint16_t current, uint16_t required)> onObjectiveProgress;

private:
    std::vector<ActiveQuest> activeQuests_;
    std::vector<uint32_t> completedQuestIds_;

    void progressObjectives(ObjectiveType type, const std::string& targetId);
};

} // namespace fate
