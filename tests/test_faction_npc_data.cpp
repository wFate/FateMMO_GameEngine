#include <doctest/doctest.h>
#include "game/shared/faction_npc_data.h"
#include <set>

using namespace fate;

TEST_SUITE("FactionNPCData") {

TEST_CASE("Each faction has 8 named NPCs") {
    CHECK(FactionNPCData::getNPCs(Faction::Xyros).size()  == 8);
    CHECK(FactionNPCData::getNPCs(Faction::Fenor).size()  == 8);
    CHECK(FactionNPCData::getNPCs(Faction::Zethos).size() == 8);
    CHECK(FactionNPCData::getNPCs(Faction::Solis).size()  == 8);
    CHECK(FactionNPCData::getNPCs(Faction::None).empty());
}

TEST_CASE("All NPCs have non-empty name and greeting") {
    const Faction factions[] = { Faction::Xyros, Faction::Fenor, Faction::Zethos, Faction::Solis };
    for (auto f : factions) {
        for (const auto& npc : FactionNPCData::getNPCs(f)) {
            CHECK_FALSE(npc.name.empty());
            CHECK_FALSE(npc.greeting.empty());
        }
    }
}

TEST_CASE("Each faction has exactly one of each role") {
    const Faction factions[] = { Faction::Xyros, Faction::Fenor, Faction::Zethos, Faction::Solis };
    for (auto f : factions) {
        std::set<NPCRole> roles;
        for (const auto& npc : FactionNPCData::getNPCs(f)) {
            roles.insert(npc.role);
        }
        CHECK(roles.size() == 8);
        CHECK(roles.count(NPCRole::Shopkeeper)       == 1);
        CHECK(roles.count(NPCRole::Banker)            == 1);
        CHECK(roles.count(NPCRole::Teleporter)        == 1);
        CHECK(roles.count(NPCRole::ArenaMaster)       == 1);
        CHECK(roles.count(NPCRole::BattlefieldHerald) == 1);
        CHECK(roles.count(NPCRole::DungeonGuide)      == 1);
        CHECK(roles.count(NPCRole::QuestGiver)        == 1);
        CHECK(roles.count(NPCRole::Marketplace)       == 1);
    }
}

TEST_CASE("Veylan is the marketplace NPC name for all factions") {
    const Faction factions[] = { Faction::Xyros, Faction::Fenor, Faction::Zethos, Faction::Solis };
    for (auto f : factions) {
        for (const auto& npc : FactionNPCData::getNPCs(f)) {
            if (npc.role == NPCRole::Marketplace) {
                CHECK(npc.name == "Veylan");
            }
        }
    }
}

TEST_CASE("Xyros shopkeeper is Cindrik") {
    for (const auto& npc : FactionNPCData::getNPCs(Faction::Xyros)) {
        if (npc.role == NPCRole::Shopkeeper) {
            CHECK(npc.name == "Cindrik");
        }
    }
}

TEST_CASE("Leaderboard NPC data exists for all factions") {
    const Faction factions[] = { Faction::Xyros, Faction::Fenor, Faction::Zethos, Faction::Solis };
    for (auto f : factions) {
        const auto& lb = FactionNPCData::getLeaderboardNPC(f);
        CHECK_FALSE(lb.name.empty());
        CHECK_FALSE(lb.greeting.empty());
    }

    // None faction returns empty default
    const auto& none = FactionNPCData::getLeaderboardNPC(Faction::None);
    CHECK(none.name.empty());
    CHECK(none.greeting.empty());
}

} // TEST_SUITE
