#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fate {

// Forward declarations — no pqxx in this header
class ItemDefinitionCache;
class MobDefCache;
class LootTableCache;
class SpawnZoneCache;
class SkillDefCache;

struct ValidationIssue {
    uint8_t severity;   // 0 = Error, 1 = Warning, 2 = Info
    std::string message;
};

class ContentValidator {
public:
    void setCaches(const ItemDefinitionCache* items,
                   const MobDefCache* mobs,
                   const LootTableCache* loot,
                   const SpawnZoneCache* spawns,
                   const SkillDefCache* skills);

    // Run all rules, return issues sorted by severity (errors first)
    std::vector<ValidationIssue> runAll() const;

    // Individual rule methods (public for unit testing)
    // Error (severity 0)
    std::vector<ValidationIssue> checkLootItemRefs() const;
    std::vector<ValidationIssue> checkSpawnMobRefs() const;
    std::vector<ValidationIssue> checkMobLootTableRefs() const;
    std::vector<ValidationIssue> checkSkillRankRefs() const;

    // Warning (severity 1)
    std::vector<ValidationIssue> checkOrphanedItems() const;
    std::vector<ValidationIssue> checkUnspawnedMobs() const;
    std::vector<ValidationIssue> checkLootDropChances() const;
    std::vector<ValidationIssue> checkSpawnZoneCounts() const;
    std::vector<ValidationIssue> checkMobZeroHP() const;

    // Info (severity 2)
    std::vector<ValidationIssue> checkMobSpawnLevelRange() const;
    std::vector<ValidationIssue> checkItemDescriptions() const;

private:
    const ItemDefinitionCache* items_ = nullptr;
    const MobDefCache* mobs_ = nullptr;
    const LootTableCache* loot_ = nullptr;
    const SpawnZoneCache* spawns_ = nullptr;
    const SkillDefCache* skills_ = nullptr;
};

} // namespace fate
