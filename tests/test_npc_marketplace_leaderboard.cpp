#include <doctest/doctest.h>
#include "game/shared/npc_types.h"

using namespace fate;

TEST_SUITE("MarketplaceLeaderboardNPC") {
    TEST_CASE("NPCTemplate has marketplace flag") {
        NPCTemplate tmpl;
        CHECK(tmpl.isMarketplaceNPC == false);
        tmpl.isMarketplaceNPC = true;
        CHECK(tmpl.isMarketplaceNPC == true);
    }

    TEST_CASE("NPCTemplate has leaderboard flag") {
        NPCTemplate tmpl;
        CHECK(tmpl.isLeaderboardNPC == false);
        tmpl.isLeaderboardNPC = true;
        CHECK(tmpl.isLeaderboardNPC == true);
    }
}
