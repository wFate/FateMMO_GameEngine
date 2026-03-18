#pragma once
#include <string>
#include <vector>
#include <pqxx/pqxx>

namespace fate {

struct QuestProgressRecord {
    int progressId = 0;
    std::string characterId;
    std::string questId;
    std::string status;      // "not_started", "active", "completed"
    int currentCount = 0;
    int targetCount = 1;
};

class QuestRepository {
public:
    explicit QuestRepository(pqxx::connection& conn) : conn_(conn) {}

    // Load all quest progress for a character
    std::vector<QuestProgressRecord> loadQuestProgress(const std::string& characterId);

    // Load completed quest IDs
    std::vector<std::string> loadCompletedQuests(const std::string& characterId);

    // Save/update quest progress (upsert by character_id + quest_id)
    bool saveQuestProgress(const std::string& characterId, const std::string& questId,
                           const std::string& status, int currentCount, int targetCount);

    // Mark quest completed
    bool completeQuest(const std::string& characterId, const std::string& questId);

    // Remove quest progress (abandon)
    bool abandonQuest(const std::string& characterId, const std::string& questId);

    // Save all active quests in batch
    bool saveAllQuestProgress(const std::string& characterId,
                              const std::vector<QuestProgressRecord>& quests);

private:
    pqxx::connection& conn_;
};

} // namespace fate
