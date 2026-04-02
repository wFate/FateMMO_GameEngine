#include <doctest/doctest.h>
#include "server/validation/content_validator.h"
#include "server/cache/item_definition_cache.h"
#include "server/cache/loot_table_cache.h"
#include "server/db/definition_caches.h"
#include "server/db/spawn_zone_cache.h"

// Include the .cpp directly — test target doesn't link server sources
#include "server/validation/content_validator.cpp"

using namespace fate;

// Helper: get writable reference to private map via const_cast on const accessor
template<typename Cache, typename Accessor>
static auto& writable(Cache& cache, Accessor accessor) {
    return const_cast<std::remove_const_t<std::remove_reference_t<
        decltype((cache.*accessor)())>>&>((cache.*accessor)());
}

TEST_SUITE("Validator") {

// ============================================================================
// checkLootItemRefs
// ============================================================================

TEST_CASE("Validator: checkLootItemRefs valid refs") {
    ItemDefinitionCache items;
    LootTableCache loot;

    auto& itemMap = writable(items, &ItemDefinitionCache::allItems);
    CachedItemDefinition sword;
    sword.itemId = "item_sword";
    sword.displayName = "Sword";
    itemMap["item_sword"] = sword;

    auto& tableMap = writable(loot, &LootTableCache::allTables);
    tableMap["table_1"] = {{{"item_sword", 0.5f, 1, 1}}};

    ContentValidator v;
    v.setCaches(&items, nullptr, &loot, nullptr, nullptr);
    auto issues = v.checkLootItemRefs();
    CHECK(issues.empty());
}

TEST_CASE("Validator: checkLootItemRefs broken ref") {
    ItemDefinitionCache items;
    LootTableCache loot;

    auto& tableMap = writable(loot, &LootTableCache::allTables);
    LootDropEntry drop;
    drop.itemId = "item_missing";
    drop.dropChance = 0.5f;
    tableMap["table_1"] = {drop};

    ContentValidator v;
    v.setCaches(&items, nullptr, &loot, nullptr, nullptr);
    auto issues = v.checkLootItemRefs();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == 0);
    CHECK(issues[0].message.find("item_missing") != std::string::npos);
}

// ============================================================================
// checkMobLootTableRefs
// ============================================================================

TEST_CASE("Validator: checkMobLootTableRefs broken ref") {
    MobDefCache mobs;
    LootTableCache loot;

    auto& mobMap = writable(mobs, &MobDefCache::allMobs);
    CachedMobDef goblin;
    goblin.mobDefId = "mob_goblin";
    goblin.displayName = "Goblin";
    goblin.lootTableId = "table_missing";
    mobMap["mob_goblin"] = goblin;

    ContentValidator v;
    v.setCaches(nullptr, &mobs, &loot, nullptr, nullptr);
    auto issues = v.checkMobLootTableRefs();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == 0);
    CHECK(issues[0].message.find("table_missing") != std::string::npos);
}

TEST_CASE("Validator: checkMobLootTableRefs empty lootTableId is OK") {
    MobDefCache mobs;
    LootTableCache loot;

    auto& mobMap = writable(mobs, &MobDefCache::allMobs);
    CachedMobDef rat;
    rat.mobDefId = "mob_rat";
    rat.displayName = "Rat";
    rat.lootTableId = "";  // empty is fine
    mobMap["mob_rat"] = rat;

    ContentValidator v;
    v.setCaches(nullptr, &mobs, &loot, nullptr, nullptr);
    auto issues = v.checkMobLootTableRefs();
    CHECK(issues.empty());
}

// ============================================================================
// checkLootDropChances
// ============================================================================

TEST_CASE("Validator: checkLootDropChances invalid values") {
    LootTableCache loot;

    auto& tableMap = writable(loot, &LootTableCache::allTables);
    LootDropEntry zero;
    zero.itemId = "item_a";
    zero.dropChance = 0.0f;

    LootDropEntry negative;
    negative.itemId = "item_b";
    negative.dropChance = -0.5f;

    LootDropEntry tooHigh;
    tooHigh.itemId = "item_c";
    tooHigh.dropChance = 1.5f;

    LootDropEntry valid;
    valid.itemId = "item_d";
    valid.dropChance = 0.5f;

    tableMap["table_1"] = {zero, negative, tooHigh, valid};

    ContentValidator v;
    v.setCaches(nullptr, nullptr, &loot, nullptr, nullptr);
    auto issues = v.checkLootDropChances();
    CHECK(issues.size() == 3);  // zero, negative, and too-high are flagged
    for (const auto& issue : issues) {
        CHECK(issue.severity == 1);
    }
}

// ============================================================================
// checkMobZeroHP
// ============================================================================

TEST_CASE("Validator: checkMobZeroHP") {
    MobDefCache mobs;

    auto& mobMap = writable(mobs, &MobDefCache::allMobs);
    CachedMobDef zeroHP;
    zeroHP.mobDefId = "mob_ghost";
    zeroHP.displayName = "Ghost";
    zeroHP.baseHP = 0;
    mobMap["mob_ghost"] = zeroHP;

    CachedMobDef healthy;
    healthy.mobDefId = "mob_wolf";
    healthy.displayName = "Wolf";
    healthy.baseHP = 100;
    mobMap["mob_wolf"] = healthy;

    ContentValidator v;
    v.setCaches(nullptr, &mobs, nullptr, nullptr, nullptr);
    auto issues = v.checkMobZeroHP();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == 1);
    CHECK(issues[0].message.find("Ghost") != std::string::npos);
}

// ============================================================================
// checkSpawnMobRefs
// ============================================================================

TEST_CASE("Validator: checkSpawnMobRefs broken ref") {
    MobDefCache mobs;
    SpawnZoneCache spawns;

    auto& zoneMap = writable(spawns, &SpawnZoneCache::allZones);
    SpawnZoneRow zone;
    zone.zoneId = 1;
    zone.sceneId = "scene_forest";
    zone.zoneName = "Forest Edge";
    zone.mobDefId = "mob_nonexistent";
    zone.targetCount = 3;
    zoneMap["scene_forest"] = {zone};

    ContentValidator v;
    v.setCaches(nullptr, &mobs, nullptr, &spawns, nullptr);
    auto issues = v.checkSpawnMobRefs();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == 0);
    CHECK(issues[0].message.find("mob_nonexistent") != std::string::npos);
}

// ============================================================================
// runAll aggregation + sorting
// ============================================================================

TEST_CASE("Validator: runAll aggregates and sorts by severity") {
    ItemDefinitionCache items;
    MobDefCache mobs;
    LootTableCache loot;
    SpawnZoneCache spawns;
    SkillDefCache skills;

    // Set up a broken loot ref (error, severity 0)
    auto& tableMap = writable(loot, &LootTableCache::allTables);
    LootDropEntry badDrop;
    badDrop.itemId = "item_missing";
    badDrop.dropChance = 0.5f;
    tableMap["table_1"] = {badDrop};

    // Set up an item with empty description (info, severity 2)
    auto& itemMap = writable(items, &ItemDefinitionCache::allItems);
    CachedItemDefinition noDesc;
    noDesc.itemId = "item_nodesc";
    noDesc.displayName = "No Description Item";
    noDesc.description = "";
    itemMap["item_nodesc"] = noDesc;

    // Set up a mob with 0 HP (warning, severity 1)
    auto& mobMap = writable(mobs, &MobDefCache::allMobs);
    CachedMobDef zeroMob;
    zeroMob.mobDefId = "mob_zero";
    zeroMob.displayName = "Zero HP Mob";
    zeroMob.baseHP = 0;
    mobMap["mob_zero"] = zeroMob;

    ContentValidator v;
    v.setCaches(&items, &mobs, &loot, &spawns, &skills);
    auto issues = v.runAll();

    // Should have at least the 3 issues we set up
    CHECK(issues.size() >= 3);

    // Verify sorted: errors (0) before warnings (1) before info (2)
    for (size_t i = 1; i < issues.size(); ++i) {
        CHECK(issues[i - 1].severity <= issues[i].severity);
    }

    // Verify our specific error is present
    bool foundError = false;
    bool foundWarning = false;
    bool foundInfo = false;
    for (const auto& issue : issues) {
        if (issue.severity == 0 && issue.message.find("item_missing") != std::string::npos) foundError = true;
        if (issue.severity == 1 && issue.message.find("Zero HP Mob") != std::string::npos) foundWarning = true;
        if (issue.severity == 2 && issue.message.find("No Description Item") != std::string::npos) foundInfo = true;
    }
    CHECK(foundError);
    CHECK(foundWarning);
    CHECK(foundInfo);
}

// ============================================================================
// checkMobSpawnLevelRange
// ============================================================================

TEST_CASE("Validator: checkMobSpawnLevelRange inverted") {
    MobDefCache mobs;

    auto& mobMap = writable(mobs, &MobDefCache::allMobs);
    CachedMobDef inverted;
    inverted.mobDefId = "mob_inv";
    inverted.displayName = "Inverted Mob";
    inverted.minSpawnLevel = 50;
    inverted.maxSpawnLevel = 10;
    mobMap["mob_inv"] = inverted;

    CachedMobDef normal;
    normal.mobDefId = "mob_norm";
    normal.displayName = "Normal Mob";
    normal.minSpawnLevel = 1;
    normal.maxSpawnLevel = 10;
    mobMap["mob_norm"] = normal;

    ContentValidator v;
    v.setCaches(nullptr, &mobs, nullptr, nullptr, nullptr);
    auto issues = v.checkMobSpawnLevelRange();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == 2);
    CHECK(issues[0].message.find("Inverted Mob") != std::string::npos);
}

// ============================================================================
// checkSkillRankRefs
// ============================================================================

TEST_CASE("Validator: checkSkillRankRefs orphan rank") {
    SkillDefCache skills;

    // Add a skill definition
    auto& skillMap = writable(skills, &SkillDefCache::allSkills);
    CachedSkillDef fireball;
    fireball.skillId = "skill_fireball";
    fireball.skillName = "Fireball";
    skillMap["skill_fireball"] = fireball;

    // Add a rank that references a non-existent skill
    auto& rankMap = writable(skills, &SkillDefCache::allRanks);
    CachedSkillRank orphanRank;
    orphanRank.skillId = "skill_nonexistent";
    orphanRank.rank = 1;
    rankMap["skill_nonexistent:1"] = orphanRank;

    // Add a valid rank
    CachedSkillRank validRank;
    validRank.skillId = "skill_fireball";
    validRank.rank = 1;
    rankMap["skill_fireball:1"] = validRank;

    ContentValidator v;
    v.setCaches(nullptr, nullptr, nullptr, nullptr, &skills);
    auto issues = v.checkSkillRankRefs();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].severity == 0);
    CHECK(issues[0].message.find("skill_nonexistent") != std::string::npos);
}

} // TEST_SUITE
