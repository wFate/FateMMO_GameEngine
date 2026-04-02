#include "server/validation/content_validator.h"
#include "server/cache/item_definition_cache.h"
#include "server/cache/loot_table_cache.h"
#include "server/db/definition_caches.h"
#include "server/db/spawn_zone_cache.h"
#include <algorithm>
#include <unordered_set>

namespace fate {

void ContentValidator::setCaches(const ItemDefinitionCache* items,
                                 const MobDefCache* mobs,
                                 const LootTableCache* loot,
                                 const SpawnZoneCache* spawns,
                                 const SkillDefCache* skills) {
    items_ = items;
    mobs_ = mobs;
    loot_ = loot;
    spawns_ = spawns;
    skills_ = skills;
}

std::vector<ValidationIssue> ContentValidator::runAll() const {
    std::vector<ValidationIssue> all;

    auto append = [&](std::vector<ValidationIssue>&& issues) {
        all.insert(all.end(),
                   std::make_move_iterator(issues.begin()),
                   std::make_move_iterator(issues.end()));
    };

    // Error rules
    append(checkLootItemRefs());
    append(checkSpawnMobRefs());
    append(checkMobLootTableRefs());
    append(checkSkillRankRefs());

    // Warning rules
    append(checkOrphanedItems());
    append(checkUnspawnedMobs());
    append(checkLootDropChances());
    append(checkSpawnZoneCounts());
    append(checkMobZeroHP());

    // Info rules
    append(checkMobSpawnLevelRange());
    append(checkItemDescriptions());

    // Sort by severity (errors first)
    std::stable_sort(all.begin(), all.end(),
        [](const ValidationIssue& a, const ValidationIssue& b) {
            return a.severity < b.severity;
        });

    return all;
}

// ============================================================================
// Error rules (severity 0)
// ============================================================================

std::vector<ValidationIssue> ContentValidator::checkLootItemRefs() const {
    std::vector<ValidationIssue> issues;
    if (!loot_ || !items_) return issues;

    for (const auto& [tableId, drops] : loot_->allTables()) {
        for (const auto& drop : drops) {
            if (!items_->getDefinition(drop.itemId)) {
                issues.push_back({0, "Loot table '" + tableId +
                    "' references non-existent item '" + drop.itemId + "'"});
            }
        }
    }
    return issues;
}

std::vector<ValidationIssue> ContentValidator::checkSpawnMobRefs() const {
    std::vector<ValidationIssue> issues;
    if (!spawns_ || !mobs_) return issues;

    for (const auto& [sceneId, zones] : spawns_->allZones()) {
        for (const auto& zone : zones) {
            if (!mobs_->has(zone.mobDefId)) {
                issues.push_back({0, "Spawn zone '" + zone.zoneName +
                    "' (scene '" + sceneId + "') references non-existent mob '" +
                    zone.mobDefId + "'"});
            }
        }
    }
    return issues;
}

std::vector<ValidationIssue> ContentValidator::checkMobLootTableRefs() const {
    std::vector<ValidationIssue> issues;
    if (!mobs_ || !loot_) return issues;

    for (const auto& [mobId, mob] : mobs_->allMobs()) {
        if (!mob.lootTableId.empty() && !loot_->hasTable(mob.lootTableId)) {
            issues.push_back({0, "Mob '" + mob.displayName +
                "' (" + mobId + ") references non-existent loot table '" +
                mob.lootTableId + "'"});
        }
    }
    return issues;
}

std::vector<ValidationIssue> ContentValidator::checkSkillRankRefs() const {
    std::vector<ValidationIssue> issues;
    if (!skills_) return issues;

    for (const auto& [rankKey, rank] : skills_->allRanks()) {
        if (!skills_->getSkill(rank.skillId)) {
            issues.push_back({0, "Skill rank '" + rankKey +
                "' references non-existent skill '" + rank.skillId + "'"});
        }
    }
    return issues;
}

// ============================================================================
// Warning rules (severity 1)
// ============================================================================

std::vector<ValidationIssue> ContentValidator::checkOrphanedItems() const {
    std::vector<ValidationIssue> issues;
    if (!items_ || !loot_) return issues;

    // Collect all item IDs referenced in loot tables
    std::unordered_set<std::string> referencedItems;
    for (const auto& [tableId, drops] : loot_->allTables()) {
        for (const auto& drop : drops) {
            referencedItems.insert(drop.itemId);
        }
    }

    for (const auto& [itemId, def] : items_->allItems()) {
        if (referencedItems.find(itemId) == referencedItems.end()) {
            issues.push_back({1, "Item '" + def.displayName +
                "' (" + itemId + ") is not in any loot table"});
        }
    }
    return issues;
}

std::vector<ValidationIssue> ContentValidator::checkUnspawnedMobs() const {
    std::vector<ValidationIssue> issues;
    if (!mobs_ || !spawns_) return issues;

    // Collect all mob IDs referenced in spawn zones
    std::unordered_set<std::string> spawnedMobs;
    for (const auto& [sceneId, zones] : spawns_->allZones()) {
        for (const auto& zone : zones) {
            spawnedMobs.insert(zone.mobDefId);
        }
    }

    for (const auto& [mobId, mob] : mobs_->allMobs()) {
        if (spawnedMobs.find(mobId) == spawnedMobs.end()) {
            issues.push_back({1, "Mob '" + mob.displayName +
                "' (" + mobId + ") is not in any spawn zone"});
        }
    }
    return issues;
}

std::vector<ValidationIssue> ContentValidator::checkLootDropChances() const {
    std::vector<ValidationIssue> issues;
    if (!loot_) return issues;

    for (const auto& [tableId, drops] : loot_->allTables()) {
        for (const auto& drop : drops) {
            if (drop.dropChance <= 0.0f || drop.dropChance > 1.0f) {
                issues.push_back({1, "Loot table '" + tableId +
                    "' item '" + drop.itemId + "' has invalid drop_chance " +
                    std::to_string(drop.dropChance)});
            }
        }
    }
    return issues;
}

std::vector<ValidationIssue> ContentValidator::checkSpawnZoneCounts() const {
    std::vector<ValidationIssue> issues;
    if (!spawns_) return issues;

    for (const auto& [sceneId, zones] : spawns_->allZones()) {
        for (const auto& zone : zones) {
            if (zone.targetCount <= 0) {
                issues.push_back({1, "Spawn zone '" + zone.zoneName +
                    "' (scene '" + sceneId + "') has target_count <= 0"});
            }
        }
    }
    return issues;
}

std::vector<ValidationIssue> ContentValidator::checkMobZeroHP() const {
    std::vector<ValidationIssue> issues;
    if (!mobs_) return issues;

    for (const auto& [mobId, mob] : mobs_->allMobs()) {
        if (mob.baseHP <= 0) {
            issues.push_back({1, "Mob '" + mob.displayName +
                "' (" + mobId + ") has base_hp <= 0"});
        }
    }
    return issues;
}

// ============================================================================
// Info rules (severity 2)
// ============================================================================

std::vector<ValidationIssue> ContentValidator::checkMobSpawnLevelRange() const {
    std::vector<ValidationIssue> issues;
    if (!mobs_) return issues;

    for (const auto& [mobId, mob] : mobs_->allMobs()) {
        if (mob.minSpawnLevel > mob.maxSpawnLevel) {
            issues.push_back({2, "Mob '" + mob.displayName +
                "' (" + mobId + ") has min_spawn_level (" +
                std::to_string(mob.minSpawnLevel) + ") > max_spawn_level (" +
                std::to_string(mob.maxSpawnLevel) + ")"});
        }
    }
    return issues;
}

std::vector<ValidationIssue> ContentValidator::checkItemDescriptions() const {
    std::vector<ValidationIssue> issues;
    if (!items_) return issues;

    for (const auto& [itemId, def] : items_->allItems()) {
        if (def.description.empty()) {
            issues.push_back({2, "Item '" + def.displayName +
                "' (" + itemId + ") has empty description"});
        }
    }
    return issues;
}

} // namespace fate
