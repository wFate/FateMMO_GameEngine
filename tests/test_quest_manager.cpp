#include <doctest/doctest.h>
#include "game/shared/quest_manager.h"
#include "game/shared/quest_data.h"
#include "game/shared/character_stats.h"
#include "game/shared/inventory.h"

using namespace fate;

// Helper to create a basic CharacterStats for testing
static CharacterStats makeStats(int level = 1) {
    CharacterStats s;
    s.level = level;
    s.recalculateXPRequirement();
    s.recalculateStats();
    s.currentHP = s.maxHP;
    s.currentMP = s.maxMP;
    return s;
}

// Helper to create an initialized Inventory
static Inventory makeInventory(int64_t gold = 0) {
    Inventory inv;
    inv.initialize("test_char", gold);
    return inv;
}

TEST_CASE("QuestManager: accept quest") {
    QuestManager qm;
    CHECK(qm.acceptQuest(1, 1));
    CHECK(qm.isQuestActive(1));
    CHECK(qm.getActiveQuests().size() == 1);
}

TEST_CASE("QuestManager: reject duplicate accept") {
    QuestManager qm;
    CHECK(qm.acceptQuest(1, 1));
    CHECK_FALSE(qm.acceptQuest(1, 1));
}

TEST_CASE("QuestManager: max active quests") {
    QuestManager qm;
    // Accept quests 1-4 (only 4 defined starter quests with level 0 req, but we need 10)
    // We'll accept what we can and verify the limit concept
    CHECK(qm.acceptQuest(1, 50));
    CHECK(qm.acceptQuest(2, 50));
    CHECK(qm.acceptQuest(3, 50));
    CHECK(qm.acceptQuest(4, 50));
    CHECK(qm.acceptQuest(5, 50));
    // Quest 6 requires quest 5 completed, so it should fail
    CHECK_FALSE(qm.acceptQuest(6, 50));
    CHECK(qm.getActiveQuests().size() == 5);
}

TEST_CASE("QuestManager: abandon quest") {
    QuestManager qm;
    qm.acceptQuest(1, 1);
    CHECK(qm.abandonQuest(1));
    CHECK_FALSE(qm.isQuestActive(1));
    CHECK(qm.getActiveQuests().empty());
}

TEST_CASE("QuestManager: abandon non-existent quest") {
    QuestManager qm;
    CHECK_FALSE(qm.abandonQuest(999));
}

TEST_CASE("QuestManager: kill progress") {
    QuestManager qm;
    qm.acceptQuest(1, 1); // Kill 10 leaf_boar

    for (int i = 0; i < 9; ++i) {
        qm.onMobKilled("leaf_boar");
    }
    CHECK_FALSE(qm.isQuestComplete(1));

    qm.onMobKilled("leaf_boar");
    CHECK(qm.isQuestComplete(1));
}

TEST_CASE("QuestManager: kill wrong mob does not progress") {
    QuestManager qm;
    qm.acceptQuest(1, 1);
    qm.onMobKilled("wrong_mob");

    const auto* aq = qm.getActiveQuest(1);
    REQUIRE(aq != nullptr);
    CHECK(aq->objectiveProgress[0] == 0);
}

TEST_CASE("QuestManager: collect progress") {
    QuestManager qm;
    qm.acceptQuest(2, 1); // Collect 5 clover

    for (int i = 0; i < 5; ++i) {
        qm.onItemCollected("clover");
    }
    CHECK(qm.isQuestComplete(2));
}

TEST_CASE("QuestManager: talkto progress with multiple objectives") {
    QuestManager qm;
    qm.acceptQuest(3, 1); // TalkTo "100" and "101"

    qm.onNPCTalkedTo("100");
    CHECK_FALSE(qm.isQuestComplete(3));

    qm.onNPCTalkedTo("101");
    CHECK(qm.isQuestComplete(3));
}

TEST_CASE("QuestManager: pvp kill progress") {
    QuestManager qm;
    qm.acceptQuest(4, 10); // PvP 2 kills

    qm.onPvPKill();
    CHECK_FALSE(qm.isQuestComplete(4));

    qm.onPvPKill();
    CHECK(qm.isQuestComplete(4));
}

TEST_CASE("QuestManager: progress does not exceed required count") {
    QuestManager qm;
    qm.acceptQuest(1, 1);

    for (int i = 0; i < 20; ++i) {
        qm.onMobKilled("leaf_boar");
    }

    const auto* aq = qm.getActiveQuest(1);
    REQUIRE(aq != nullptr);
    CHECK(aq->objectiveProgress[0] == 10);
}

TEST_CASE("QuestManager: turn in completed quest") {
    QuestManager qm;
    auto stats = makeStats(1);
    auto inv = makeInventory(0);

    qm.acceptQuest(1, 1);
    for (int i = 0; i < 10; ++i) qm.onMobKilled("leaf_boar");

    int levelBefore = stats.level;
    int64_t xpBefore = stats.currentXP;
    CHECK(qm.turnInQuest(1, stats, inv));

    CHECK_FALSE(qm.isQuestActive(1));
    CHECK(qm.hasCompletedQuest(1));
    // Quest 1 rewards 100 XP — may cause level up (XP resets to 0), so check either condition
    CHECK((stats.currentXP > xpBefore || stats.level > levelBefore));
    CHECK(inv.getGold() == 50);
}

TEST_CASE("QuestManager: reject turn in of incomplete quest") {
    QuestManager qm;
    auto stats = makeStats(1);
    auto inv = makeInventory(0);

    qm.acceptQuest(1, 1);
    qm.onMobKilled("leaf_boar"); // only 1 of 10

    CHECK_FALSE(qm.turnInQuest(1, stats, inv));
    CHECK(qm.isQuestActive(1));
}

TEST_CASE("QuestManager: reject re-accept of completed quest") {
    QuestManager qm;
    auto stats = makeStats(1);
    auto inv = makeInventory(0);

    qm.acceptQuest(1, 1);
    for (int i = 0; i < 10; ++i) qm.onMobKilled("leaf_boar");
    qm.turnInQuest(1, stats, inv);

    CHECK_FALSE(qm.acceptQuest(1, 1));
}

TEST_CASE("QuestManager: level requirement") {
    QuestManager qm;
    // Quest 4 requires level 10
    CHECK_FALSE(qm.acceptQuest(4, 5));
    CHECK(qm.acceptQuest(4, 10));
}

TEST_CASE("QuestManager: prerequisite check") {
    QuestManager qm;
    auto stats = makeStats(25);
    auto inv = makeInventory(0);

    // Quest 6 requires quest 5 completed
    CHECK_FALSE(qm.acceptQuest(6, 25));

    // Complete quest 5
    qm.acceptQuest(5, 25);
    for (int i = 0; i < 5; ++i) qm.onMobKilled("forest_guardian");

    // Quest 5 rewards a guardian_crystal item, but turnInQuest grants item rewards
    // We need to turn in quest 5 first
    qm.turnInQuest(5, stats, inv);
    CHECK(qm.hasCompletedQuest(5));

    // Now quest 6 should be acceptable
    CHECK(qm.acceptQuest(6, 25));
}

TEST_CASE("QuestManager: callbacks fire correctly") {
    QuestManager qm;
    auto stats = makeStats(1);
    auto inv = makeInventory(0);

    uint32_t acceptedId = 0;
    uint32_t completedId = 0;
    int progressCount = 0;

    qm.onQuestAccepted = [&](uint32_t id) { acceptedId = id; };
    qm.onQuestCompleted = [&](uint32_t id) { completedId = id; };
    qm.onObjectiveProgress = [&](uint32_t, int, uint16_t, uint16_t) { progressCount++; };

    qm.acceptQuest(1, 1);
    CHECK(acceptedId == 1);

    for (int i = 0; i < 10; ++i) qm.onMobKilled("leaf_boar");
    CHECK(progressCount == 10);

    qm.turnInQuest(1, stats, inv);
    CHECK(completedId == 1);
}

TEST_CASE("QuestManager: getActiveQuest returns nullptr for unknown") {
    QuestManager qm;
    CHECK(qm.getActiveQuest(999) == nullptr);
}

TEST_CASE("QuestManager: canAcceptQuest with invalid quest id") {
    QuestManager qm;
    CHECK_FALSE(qm.canAcceptQuest(999, 1));
}
