#include <doctest/doctest.h>
#include "game/shared/arena_manager.h"

using namespace fate;

TEST_SUITE("ArenaManager") {

TEST_CASE("Solo registration succeeds") {
    ArenaManager mgr;
    CHECK(mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f));
    CHECK(mgr.isPlayerQueued(1));
}

TEST_CASE("Cannot register same player twice") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    CHECK_FALSE(mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f));
}

TEST_CASE("Duo requires 2 players") {
    ArenaManager mgr;
    CHECK_FALSE(mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Duo, {10}, 0.0f));
    CHECK(mgr.registerGroup({1, 2}, Faction::Xyros, ArenaMode::Duo, {10, 12}, 0.0f));
}

TEST_CASE("Team requires 3 players") {
    ArenaManager mgr;
    CHECK_FALSE(mgr.registerGroup({1, 2}, Faction::Xyros, ArenaMode::Team, {10, 12}, 0.0f));
    CHECK(mgr.registerGroup({1, 2, 3}, Faction::Xyros, ArenaMode::Team, {10, 12, 11}, 0.0f));
}

TEST_CASE("Cross-faction matchmaking succeeds") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Fenor, ArenaMode::Solo, {12}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    CHECK(matches.size() == 1);
}

TEST_CASE("Same-faction matchmaking fails") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Xyros, ArenaMode::Solo, {12}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    CHECK(matches.empty());
}

TEST_CASE("Level range > 5 prevents match") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Fenor, ArenaMode::Solo, {16}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    CHECK(matches.empty());
}

TEST_CASE("Level range exactly 5 allows match") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Fenor, ArenaMode::Solo, {15}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    CHECK(matches.size() == 1);
}

TEST_CASE("Match transitions countdown -> active") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Fenor, ArenaMode::Solo, {10}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    REQUIRE(matches.size() == 1);
    auto* match = mgr.getMatch(matches[0]);
    REQUIRE(match != nullptr);
    CHECK(match->state == ArenaMatchState::Countdown);

    mgr.tickMatches(4.0f); // past 3s countdown
    CHECK(match->state == ArenaMatchState::Active);
}

TEST_CASE("All opponents dead = instant win") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Fenor, ArenaMode::Solo, {10}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    mgr.tickMatches(4.0f); // activate

    bool matchEnded = false;
    bool teamAWon = false;
    mgr.onMatchEnd = [&](uint32_t, bool aWins, bool tie) {
        matchEnded = true;
        teamAWon = aWins;
        (void)tie;
    };

    mgr.onPlayerKill(matches[0], 1, 2); // team A kills team B's last player
    CHECK(matchEnded);
    CHECK(teamAWon);
}

TEST_CASE("Timer expiry with both alive = tie") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Fenor, ArenaMode::Solo, {10}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    mgr.tickMatches(4.0f); // activate (countdown ends at t=3)

    bool wasTie = false;
    mgr.onMatchEnd = [&](uint32_t, bool, bool tie) { wasTie = tie; };

    mgr.tickMatches(4.0f + 181.0f); // past 3 minutes
    CHECK(wasTie);
}

TEST_CASE("AFK living player forfeited after 30s") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Fenor, ArenaMode::Solo, {10}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    mgr.tickMatches(4.0f); // activate

    // Player 1 does an action at t=4
    mgr.onPlayerAction(matches[0], 1, 4.0f);
    // Player 2 never acts — lastActionTime stays at 4.0f (set on activation)

    bool forfeited = false;
    mgr.onPlayerForfeited = [&](uint32_t eid, const std::string&) {
        if (eid == 2) forfeited = true;
    };

    mgr.tickMatches(35.0f); // 31 seconds since match start (active at t=4), player 2 never acted
    CHECK(forfeited);
}

TEST_CASE("Dead player NOT flagged AFK") {
    ArenaManager mgr;
    mgr.registerGroup({1, 10}, Faction::Xyros, ArenaMode::Duo, {10, 10}, 0.0f);
    mgr.registerGroup({2, 20}, Faction::Fenor, ArenaMode::Duo, {10, 10}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    mgr.tickMatches(4.0f); // activate

    // Kill player 10 (team A member)
    mgr.onPlayerKill(matches[0], 2, 10);

    bool player10Forfeited = false;
    mgr.onPlayerForfeited = [&](uint32_t eid, const std::string&) {
        if (eid == 10) player10Forfeited = true;
    };

    // All living players have action at some point; dead player 10 does not
    mgr.onPlayerAction(matches[0], 1,  4.0f);
    mgr.onPlayerAction(matches[0], 2,  4.0f);
    mgr.onPlayerAction(matches[0], 20, 4.0f);

    mgr.tickMatches(35.0f); // 31s since activation
    CHECK_FALSE(player10Forfeited); // dead, exempt from AFK
}

TEST_CASE("0 damage dealt = 0 honor reward") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.registerGroup({2}, Faction::Fenor, ArenaMode::Solo, {10}, 0.0f);
    auto matches = mgr.tryMatchmaking();
    mgr.tickMatches(4.0f); // activate

    // End match by timer — both alive, tie
    mgr.tickMatches(4.0f + 181.0f);

    auto* match = mgr.getMatch(matches[0]);
    REQUIRE(match != nullptr);
    // Both players dealt 0 damage (never fought) — verify stat
    for (auto& [eid, stats] : match->players) {
        CHECK(stats.damageDealt == 0);
    }
}

TEST_CASE("Unregister removes from queue") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);
    mgr.unregisterGroup(1);
    CHECK_FALSE(mgr.isPlayerQueued(1));
}

TEST_CASE("Queue timeout after 5 minutes") {
    ArenaManager mgr;
    mgr.registerGroup({1}, Faction::Xyros, ArenaMode::Solo, {10}, 0.0f);

    bool unregistered = false;
    mgr.onGroupUnregistered = [&](const std::vector<uint32_t>&, const std::string&) {
        unregistered = true;
    };

    mgr.tickMatches(301.0f); // 5 min + 1 second
    CHECK(unregistered);
    CHECK_FALSE(mgr.isPlayerQueued(1));
}

} // TEST_SUITE
