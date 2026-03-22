#include "server/db/skill_repository.h"
#include "engine/core/logger.h"

namespace fate {

SkillPointsRecord SkillRepository::loadSkillPoints(const std::string& characterId) {
    SkillPointsRecord rec;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT total_earned, total_spent FROM character_skill_points "
            "WHERE character_id = $1", characterId);
        txn.commit();
        if (!result.empty()) {
            rec.totalEarned = result[0]["total_earned"].as<int>();
            rec.totalSpent  = result[0]["total_spent"].as<int>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("SkillRepo", "loadSkillPoints failed for %s: %s", characterId.c_str(), e.what());
    }
    return rec;
}

bool SkillRepository::saveSkillPoints(const std::string& characterId, int earned, int spent) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "INSERT INTO character_skill_points (character_id, total_earned, total_spent, updated_at) "
            "VALUES ($1, $2, $3, NOW()) "
            "ON CONFLICT (character_id) DO UPDATE SET total_earned = $2, total_spent = $3, updated_at = NOW()",
            characterId, earned, spent);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SkillRepo", "saveSkillPoints failed for %s: %s", characterId.c_str(), e.what());
    }
    return false;
}

std::vector<CharacterSkillRecord> SkillRepository::loadCharacterSkills(const std::string& characterId) {
    std::vector<CharacterSkillRecord> skills;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT skill_id, unlocked_rank, activated_rank "
            "FROM character_skills WHERE character_id = $1", characterId);
        txn.commit();
        skills.reserve(result.size());
        for (const auto& row : result) {
            CharacterSkillRecord r;
            r.skillId      = row["skill_id"].as<std::string>();
            r.unlockedRank = row["unlocked_rank"].as<int>();
            r.activatedRank = row["activated_rank"].as<int>();
            skills.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("SkillRepo", "loadCharacterSkills failed for %s: %s", characterId.c_str(), e.what());
    }
    return skills;
}

bool SkillRepository::saveCharacterSkill(const std::string& characterId, const std::string& skillId,
                                          int unlockedRank, int activatedRank) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "INSERT INTO character_skills (character_id, skill_id, unlocked_rank, activated_rank, learned_at) "
            "VALUES ($1, $2, $3, $4, NOW()) "
            "ON CONFLICT (character_id, skill_id) DO UPDATE SET unlocked_rank = $3, activated_rank = $4",
            characterId, skillId, unlockedRank, activatedRank);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SkillRepo", "saveCharacterSkill failed: %s", e.what());
    }
    return false;
}

bool SkillRepository::saveAllCharacterSkills(const std::string& characterId,
                                              const std::vector<CharacterSkillRecord>& skills) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        for (const auto& s : skills) {
            txn.exec_params(
                "INSERT INTO character_skills (character_id, skill_id, unlocked_rank, activated_rank, learned_at) "
                "VALUES ($1, $2, $3, $4, NOW()) "
                "ON CONFLICT (character_id, skill_id) DO UPDATE SET unlocked_rank = $3, activated_rank = $4",
                characterId, s.skillId, s.unlockedRank, s.activatedRank);
        }
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SkillRepo", "saveAllCharacterSkills failed for %s: %s", characterId.c_str(), e.what());
    }
    return false;
}

std::vector<std::string> SkillRepository::loadSkillBar(const std::string& characterId) {
    std::vector<std::string> bar(20, ""); // 20 slots, empty by default
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT slot_index, skill_id FROM character_skill_bar "
            "WHERE character_id = $1", characterId);
        txn.commit();
        for (const auto& row : result) {
            int idx = row["slot_index"].as<int>();
            if (idx >= 0 && idx < 20) {
                bar[idx] = row["skill_id"].is_null() ? "" : row["skill_id"].as<std::string>();
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("SkillRepo", "loadSkillBar failed for %s: %s", characterId.c_str(), e.what());
    }
    return bar;
}

bool SkillRepository::saveSkillBar(const std::string& characterId, const std::vector<std::string>& slots) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params("DELETE FROM character_skill_bar WHERE character_id = $1", characterId);
        for (int i = 0; i < static_cast<int>(slots.size()) && i < 20; ++i) {
            if (slots[i].empty()) continue;
            txn.exec_params(
                "INSERT INTO character_skill_bar (character_id, slot_index, skill_id) "
                "VALUES ($1, $2, $3)",
                characterId, i, slots[i]);
        }
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("SkillRepo", "saveSkillBar failed for %s: %s", characterId.c_str(), e.what());
    }
    return false;
}

} // namespace fate
