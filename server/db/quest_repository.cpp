#include "server/db/quest_repository.h"
#include "engine/core/logger.h"

namespace fate {

std::vector<QuestProgressRecord> QuestRepository::loadQuestProgress(const std::string& characterId) {
    std::vector<QuestProgressRecord> quests;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT progress_id, character_id, quest_id, status, current_count, target_count "
            "FROM quest_progress WHERE character_id = $1 AND status = 'active'",
            characterId);
        txn.commit();
        quests.reserve(result.size());
        for (const auto& row : result) {
            QuestProgressRecord r;
            r.progressId   = row["progress_id"].as<int>();
            r.characterId  = row["character_id"].as<std::string>();
            r.questId      = row["quest_id"].as<std::string>();
            r.status       = row["status"].as<std::string>();
            r.currentCount = row["current_count"].as<int>();
            r.targetCount  = row["target_count"].as<int>();
            quests.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("QuestRepo", "loadQuestProgress failed for %s: %s", characterId.c_str(), e.what());
    }
    return quests;
}

std::vector<std::string> QuestRepository::loadCompletedQuests(const std::string& characterId) {
    std::vector<std::string> completed;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT quest_id FROM quest_progress "
            "WHERE character_id = $1 AND status = 'completed'",
            characterId);
        txn.commit();
        completed.reserve(result.size());
        for (const auto& row : result) {
            completed.push_back(row[0].as<std::string>());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("QuestRepo", "loadCompletedQuests failed for %s: %s", characterId.c_str(), e.what());
    }
    return completed;
}

bool QuestRepository::saveQuestProgress(const std::string& characterId, const std::string& questId,
                                         const std::string& status, int currentCount, int targetCount) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "INSERT INTO quest_progress (character_id, quest_id, status, current_count, target_count, started_at) "
            "VALUES ($1, $2, $3, $4, $5, NOW()) "
            "ON CONFLICT (progress_id) DO UPDATE SET status = $3, current_count = $4, target_count = $5",
            characterId, questId, status, currentCount, targetCount);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("QuestRepo", "saveQuestProgress failed: %s", e.what());
    }
    return false;
}

bool QuestRepository::completeQuest(const std::string& characterId, const std::string& questId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "UPDATE quest_progress SET status = 'completed', completed_at = NOW() "
            "WHERE character_id = $1 AND quest_id = $2",
            characterId, questId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("QuestRepo", "completeQuest failed: %s", e.what());
    }
    return false;
}

bool QuestRepository::abandonQuest(const std::string& characterId, const std::string& questId) {
    try {
        pqxx::work txn(conn_);
        txn.exec_params(
            "DELETE FROM quest_progress WHERE character_id = $1 AND quest_id = $2 AND status = 'active'",
            characterId, questId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("QuestRepo", "abandonQuest failed: %s", e.what());
    }
    return false;
}

bool QuestRepository::saveAllQuestProgress(const std::string& characterId,
                                            const std::vector<QuestProgressRecord>& quests) {
    try {
        pqxx::work txn(conn_);
        for (const auto& q : quests) {
            txn.exec_params(
                "INSERT INTO quest_progress (character_id, quest_id, status, current_count, target_count, started_at) "
                "VALUES ($1, $2, $3, $4, $5, NOW()) "
                "ON CONFLICT ON CONSTRAINT quest_progress_pkey DO UPDATE "
                "SET status = $3, current_count = $4, target_count = $5",
                characterId, q.questId, q.status, q.currentCount, q.targetCount);
        }
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("QuestRepo", "saveAllQuestProgress failed for %s: %s", characterId.c_str(), e.what());
    }
    return false;
}

} // namespace fate
