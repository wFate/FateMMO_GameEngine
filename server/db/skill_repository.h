#pragma once
#include <string>
#include <vector>
#include <optional>
#include <pqxx/pqxx>

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
    explicit SkillRepository(pqxx::connection& conn) : conn_(conn) {}

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
    pqxx::connection& conn_;
};

} // namespace fate
