#include <doctest/doctest.h>
#include "game/shared/faction.h"
#include <string>

using namespace fate;

TEST_CASE("FactionBackstory: each faction has a non-empty description") {
    const auto* xyros = FactionRegistry::get(Faction::Xyros);
    REQUIRE(xyros != nullptr);
    CHECK(!xyros->description.empty());

    const auto* fenor = FactionRegistry::get(Faction::Fenor);
    REQUIRE(fenor != nullptr);
    CHECK(!fenor->description.empty());

    const auto* zethos = FactionRegistry::get(Faction::Zethos);
    REQUIRE(zethos != nullptr);
    CHECK(!zethos->description.empty());

    const auto* solis = FactionRegistry::get(Faction::Solis);
    REQUIRE(solis != nullptr);
    CHECK(!solis->description.empty());
}

TEST_CASE("FactionBackstory: Xyros mentions volcanic") {
    const auto* xyros = FactionRegistry::get(Faction::Xyros);
    REQUIRE(xyros != nullptr);
    CHECK(xyros->description.find("volcanic") != std::string::npos);
}

TEST_CASE("FactionBackstory: None returns nullptr") {
    CHECK(FactionRegistry::get(Faction::None) == nullptr);
}
