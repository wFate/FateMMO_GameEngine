#include <doctest/doctest.h>
#include "game/shared/battlefield_manager.h"

using namespace fate;

TEST_SUITE("BattlefieldManager") {

TEST_CASE("Can register player") {
    BattlefieldManager bf;
    CHECK(bf.registerPlayer(1, "char1", Faction::Xyros, Vec2{100,100}, "zone_village"));
    CHECK(bf.playerCount() == 1);
}

TEST_CASE("Cannot register same player twice") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    CHECK_FALSE(bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z"));
}

TEST_CASE("Unregister removes player") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.unregisterPlayer(1);
    CHECK(bf.playerCount() == 0);
}

TEST_CASE("Kill increments faction counter") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    CHECK(bf.getFactionKills(Faction::Xyros) == 1);
    CHECK(bf.getFactionKills(Faction::Fenor) == 0);
}

TEST_CASE("Personal kills tracked") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    bf.onPlayerKill(1, 2);
    CHECK(bf.getPersonalKills(1) == 2);
}

TEST_CASE("Winning faction by most kills") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    bf.onPlayerKill(1, 2);
    bf.onPlayerKill(2, 1);
    CHECK(bf.getWinningFaction() == Faction::Xyros);
}

TEST_CASE("Tie when equal kills") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    bf.onPlayerKill(2, 1);
    CHECK(bf.getWinningFaction() == Faction::None);
}

TEST_CASE("hasMinimumPlayers requires 2+ factions") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    CHECK_FALSE(bf.hasMinimumPlayers());
    bf.registerPlayer(2, "c2", Faction::Xyros, Vec2{}, "z");
    CHECK_FALSE(bf.hasMinimumPlayers());
    bf.registerPlayer(3, "c3", Faction::Fenor, Vec2{}, "z");
    CHECK(bf.hasMinimumPlayers());
}

TEST_CASE("removePlayer preserves kills") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.registerPlayer(2, "c2", Faction::Fenor, Vec2{}, "z");
    bf.onPlayerKill(1, 2);
    bf.removePlayer(1);
    CHECK(bf.getFactionKills(Faction::Xyros) == 1);
}

TEST_CASE("Reset clears everything") {
    BattlefieldManager bf;
    bf.registerPlayer(1, "c1", Faction::Xyros, Vec2{}, "z");
    bf.onPlayerKill(1, 1);
    bf.reset();
    CHECK(bf.playerCount() == 0);
    CHECK(bf.getFactionKills(Faction::Xyros) == 0);
}

} // TEST_SUITE
