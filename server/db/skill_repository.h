#pragma once
#include <string>
#include <vector>
#include <optional>
#include <pqxx/pqxx>
#include "server/db/db_pool.h"

namespace fate {

struct CharacterSkillRecord {
    std::string skillId;
    int unlockedRank = 0;
    int activatedRank = 0;
};

struct SkillPointsRecord {
    int totalEarned = 0;
    int totalSpent = 0;
};

class SkillRepository {
public:
    // Legacy: direct connection (for temp repos in async fibers)
    explicit SkillRepository(pqxx::connection& conn) : connRef_(&conn), pool_(nullptr) {}
    // Pool-based: acquires connection per operation
    explicit SkillRepository(DbPool& pool) : connRef_(nullptr), pool_(&pool) {}

    // Skill points
    SkillPointsRecord loadSkillPoints(const std::string& characterId);
    bool saveSkillPoints(const std::string& characterId, int earned, int spent);

    // Learned skills
    std::vector<CharacterSkillRecord> loadCharacterSkills(const std::string& characterId);
    bool saveCharacterSkill(const std::string& characterId, const std::string& skillId,
                            int unlockedRank, int activatedRank);
    bool saveAllCharacterSkills(const std::string& characterId,
                                const std::vector<CharacterSkillRecord>& skills);

    // Skill bar (20 slots)
    std::vector<std::string> loadSkillBar(const std::string& characterId);
    bool saveSkillBar(const std::string& characterId, const std::vector<std::string>& slots);

private:
    pqxx::connection* connRef_ = nullptr;
    DbPool* pool_ = nullptr;

    DbPool::Guard acquireConn() {
        if (pool_) return pool_->acquire_guard();
        return DbPool::Guard::wrap(*connRef_);
    }
};

} // namespace fate
