#include <doctest/doctest.h>
#include "game/shared/quest_manager.h"
#include "game/shared/quest_data.h"
#include "game/shared/character_stats.h"
#include "game/shared/inventory.h"

using namespace fate;

static CharacterStats makeStats(int level = 1) {
    CharacterStats s;
    s.level = level;
    s.recalculateXPRequirement();
    s.recalculateStats();
    s.currentHP = s.maxHP;
    s.currentMP = s.maxMP;
    return s;
}

static Inventory makeInventory(int64_t gold = 0) {
    Inventory inv;
    inv.initialize("test_char", gold);
    return inv;
}

TEST_CASE("Breadcrumb quests: all 28 exist with TalkTo objectives") {
    for (uint32_t id = 400; id <= 427; ++id) {
        const auto* def = QuestData::getQuest(id);
        REQUIRE_MESSAGE(def != nullptr, "Quest ", id, " not found");
        CHECK(def->questType == QuestType::Side);
        CHECK(def->objectives.size() == 1);
        CHECK(def->objectives[0].type == ObjectiveType::TalkTo);
        CHECK_FALSE(def->turnInNpcId.empty());
        CHECK(def->rewards.xp > 0);
    }
}

TEST_CASE("Breadcrumb quests: faction groups share same zone NPC") {
    struct ZoneGroup { uint32_t start; std::string expectedNpc; };
    ZoneGroup groups[] = {
        {400, "npc_herbalist_maren"}, {404, "npc_fisherman_ged"},
        {408, "npc_foreman_garrick"}, {412, "npc_guard_malik"},
        {416, "npc_apothecary_nessa"}, {420, "npc_keeper_aelith"},
        {424, "npc_void_walker_kais"},
    };
    for (const auto& g : groups) {
        for (uint32_t off = 0; off < 4; ++off) {
            const auto* def = QuestData::getQuest(g.start + off);
            REQUIRE(def != nullptr);
            CHECK(def->turnInNpcId == g.expectedNpc);
        }
    }
}

TEST_CASE("Secret keeper quests: prerequisites match source quests") {
    struct SK { uint32_t qid; uint32_t prereq; };
    SK keepers[] = {{500,101},{501,132},{502,133},{503,143},{504,162}};
    for (const auto& sk : keepers) {
        const auto* def = QuestData::getQuest(sk.qid);
        REQUIRE_MESSAGE(def != nullptr, "Quest ", sk.qid, " not found");
        REQUIRE(def->prerequisiteQuestIds.size() == 1);
        CHECK(def->prerequisiteQuestIds[0] == sk.prereq);
        CHECK(def->objectives[0].type == ObjectiveType::Deliver);
        CHECK(QuestData::getQuest(sk.prereq) != nullptr);
    }
}

TEST_CASE("Pure progression quests: deliver objectives correct") {
    struct PP { uint32_t qid; uint32_t prereq; std::string itemId; };
    PP progs[] = {
        {505, 113, "item_tidecallers_pearl"}, {506, 121, "item_miners_headlamp"},
        {507, 122, "item_resonant_shard"}, {508, 151, "item_aether_plume"},
    };
    for (const auto& pp : progs) {
        const auto* def = QuestData::getQuest(pp.qid);
        REQUIRE_MESSAGE(def != nullptr, "Quest ", pp.qid, " not found");
        CHECK(def->prerequisiteQuestIds[0] == pp.prereq);
        CHECK(def->objectives[0].targetId == pp.itemId);
    }
}

TEST_CASE("Faction fork quests: all 12 exist with correct prerequisites") {
    struct FQ { uint32_t qid; uint32_t prereq; std::string npc; };
    FQ forks[] = {
        {510,203,"npc_scholar_xyros"}, {511,203,"npc_scholar_fenor"},
        {512,203,"npc_scholar_zethos"}, {513,203,"npc_scholar_solis"},
        {520,152,"npc_scholar_xyros"}, {521,152,"npc_scholar_fenor"},
        {522,152,"npc_scholar_zethos"}, {523,152,"npc_scholar_solis"},
        {530,161,"npc_scholar_xyros"}, {531,161,"npc_scholar_fenor"},
        {532,161,"npc_scholar_zethos"}, {533,161,"npc_scholar_solis"},
    };
    for (const auto& fq : forks) {
        const auto* def = QuestData::getQuest(fq.qid);
        REQUIRE_MESSAGE(def != nullptr, "Quest ", fq.qid, " not found");
        CHECK(def->prerequisiteQuestIds[0] == fq.prereq);
        CHECK(def->turnInNpcId == fq.npc);
        CHECK(def->objectives[0].type == ObjectiveType::Deliver);
    }
}

TEST_CASE("Compass turn-in flow: accept after prereq, deliver, complete") {
    QuestManager qm;
    auto stats = makeStats(50);
    auto inv = makeInventory(0);

    // Complete prereqs: quest 100 then 101
    qm.acceptQuest(100, 50);
    for (int i = 0; i < 5; i++) qm.onItemCollected("item_toxic_spore");
    qm.turnInQuest(100, stats, inv);

    qm.acceptQuest(101, 50);
    for (int i = 0; i < 8; i++) qm.onMobKilled("timber_wolf");
    qm.turnInQuest(101, stats, inv);

    // Quest 101 rewards item_cades_compass, so inventory should have it now
    CHECK(inv.countItem("item_cades_compass") == 1);

    // Quest 500 now available
    CHECK(qm.canAcceptQuest(500, 50));
    qm.acceptQuest(500, 50);

    // Deliver the compass to npc_scholar_isen
    qm.onDeliverAttempt("npc_scholar_isen", inv);
    CHECK(qm.isQuestComplete(500));
    CHECK(qm.turnInQuest(500, stats, inv));
    CHECK(qm.hasCompletedQuest(500));
    CHECK(inv.countItem("item_cades_compass") == 0);
}
