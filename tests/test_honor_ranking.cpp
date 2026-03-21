#include <doctest/doctest.h>
#include "game/shared/honor_system.h"

using namespace fate;

TEST_SUITE("HonorRanking") {

TEST_CASE("Honor 0 = Recruit") {
    CHECK(HonorSystem::getHonorRank(0) == HonorRank::Recruit);
}
TEST_CASE("Honor 99 = Recruit") {
    CHECK(HonorSystem::getHonorRank(99) == HonorRank::Recruit);
}
TEST_CASE("Honor 100 = Scout") {
    CHECK(HonorSystem::getHonorRank(100) == HonorRank::Scout);
}
TEST_CASE("Honor 500 = CombatSoldier") {
    CHECK(HonorSystem::getHonorRank(500) == HonorRank::CombatSoldier);
}
TEST_CASE("Honor 2000 = VeteranSoldier") {
    CHECK(HonorSystem::getHonorRank(2000) == HonorRank::VeteranSoldier);
}
TEST_CASE("Honor 5000 = ApprenticeKnight") {
    CHECK(HonorSystem::getHonorRank(5000) == HonorRank::ApprenticeKnight);
}
TEST_CASE("Honor 10000 = Fighter") {
    CHECK(HonorSystem::getHonorRank(10000) == HonorRank::Fighter);
}
TEST_CASE("Honor 25000 = EliteFighter") {
    CHECK(HonorSystem::getHonorRank(25000) == HonorRank::EliteFighter);
}
TEST_CASE("Honor 50000 = FieldCommander") {
    CHECK(HonorSystem::getHonorRank(50000) == HonorRank::FieldCommander);
}
TEST_CASE("Honor 75000 = Commander") {
    CHECK(HonorSystem::getHonorRank(75000) == HonorRank::Commander);
}
TEST_CASE("Honor 99999 = General") {
    CHECK(HonorSystem::getHonorRank(99999) == HonorRank::General);
}
TEST_CASE("Honor 500000 = General (above max threshold)") {
    CHECK(HonorSystem::getHonorRank(500000) == HonorRank::General);
}
TEST_CASE("All rank names are non-empty") {
    for (int i = 0; i <= 9; ++i) {
        auto name = HonorSystem::getHonorRankName(static_cast<HonorRank>(i));
        CHECK(std::string(name).length() > 0);
    }
}
TEST_CASE("Recruit name is correct") {
    CHECK(std::string(HonorSystem::getHonorRankName(HonorRank::Recruit)) == "Recruit");
}
TEST_CASE("General name is correct") {
    CHECK(std::string(HonorSystem::getHonorRankName(HonorRank::General)) == "General");
}

} // TEST_SUITE
