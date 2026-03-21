#include <doctest/doctest.h>
#include "engine/net/protocol.h"
using namespace fate;

TEST_CASE("SvSkillSyncMsg round-trip") {
    SvSkillSyncMsg src;
    src.skills.push_back({"fireball", 3, 2});
    src.skills.push_back({"heal", 1, 1});
    src.skillBar = {"fireball", "", "heal", "", "", "", "", "", "", "",
                    "", "", "", "", "", "", "", "", "", ""};

    uint8_t buf[1024];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvSkillSyncMsg::read(r);

    CHECK(dst.skills.size() == 2);
    CHECK(dst.skills[0].skillId == "fireball");
    CHECK(dst.skills[0].unlockedRank == 3);
    CHECK(dst.skills[0].activatedRank == 2);
    CHECK(dst.skills[1].skillId == "heal");
    CHECK(dst.skillBar.size() == 20);
    CHECK(dst.skillBar[0] == "fireball");
    CHECK(dst.skillBar[2] == "heal");
}

TEST_CASE("SvQuestSyncMsg round-trip") {
    SvQuestSyncMsg src;
    QuestSyncEntry q;
    q.questId = "kill_rats";
    q.state = 0;
    q.objectives = {{3, 10}, {0, 1}};
    src.quests.push_back(q);

    uint8_t buf[512];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvQuestSyncMsg::read(r);

    CHECK(dst.quests.size() == 1);
    CHECK(dst.quests[0].questId == "kill_rats");
    CHECK(dst.quests[0].objectives.size() == 2);
    CHECK(dst.quests[0].objectives[0].first == 3);
    CHECK(dst.quests[0].objectives[0].second == 10);
}

TEST_CASE("SvInventorySyncMsg round-trip") {
    SvInventorySyncMsg src;
    src.slots.push_back({0, "iron_sword", 1, 3, "{}", "strength", 5});
    src.slots.push_back({5, "health_potion", 10, 0, "", "", 0});
    src.equipment.push_back({1, "iron_helm", 1, 0, "", "", 0});

    uint8_t buf[1024];
    ByteWriter w(buf, sizeof(buf));
    src.write(w);
    ByteReader r(buf, w.size());
    auto dst = SvInventorySyncMsg::read(r);

    CHECK(dst.slots.size() == 2);
    CHECK(dst.slots[0].itemId == "iron_sword");
    CHECK(dst.slots[0].enchantLevel == 3);
    CHECK(dst.slots[1].quantity == 10);
    CHECK(dst.equipment.size() == 1);
    CHECK(dst.equipment[0].itemId == "iron_helm");
}
