#include <doctest/doctest.h>
#include "game/shared/faction.h"

using namespace fate;

TEST_CASE("FactionRegistry: all 4 factions defined") {
    const auto* xyros = FactionRegistry::get(Faction::Xyros);
    REQUIRE(xyros != nullptr);
    CHECK(xyros->displayName == "Xyros");

    const auto* fenor = FactionRegistry::get(Faction::Fenor);
    REQUIRE(fenor != nullptr);
    CHECK(fenor->displayName == "Fenor");

    const auto* zethos = FactionRegistry::get(Faction::Zethos);
    REQUIRE(zethos != nullptr);
    CHECK(zethos->displayName == "Zethos");

    const auto* solis = FactionRegistry::get(Faction::Solis);
    REQUIRE(solis != nullptr);
    CHECK(solis->displayName == "Solis");
}

TEST_CASE("FactionRegistry: None returns nullptr") {
    CHECK(FactionRegistry::get(Faction::None) == nullptr);
}

TEST_CASE("Faction: same-faction check") {
    CHECK(FactionRegistry::isSameFaction(Faction::Xyros, Faction::Xyros) == true);
    CHECK(FactionRegistry::isSameFaction(Faction::Xyros, Faction::Fenor) == false);
    CHECK(FactionRegistry::isSameFaction(Faction::None, Faction::None) == false);
    CHECK(FactionRegistry::isSameFaction(Faction::Xyros, Faction::None) == false);
}

TEST_CASE("FactionChatGarbler: garble produces same-length output") {
    std::string original = "Hello enemy!";
    std::string garbled = FactionChatGarbler::garble(original);
    CHECK(garbled.size() == original.size());
    CHECK(garbled != original);
}

TEST_CASE("FactionChatGarbler: garble is deterministic") {
    std::string msg = "Attack the base!";
    CHECK(FactionChatGarbler::garble(msg) == FactionChatGarbler::garble(msg));
}

TEST_CASE("FactionChatGarbler: empty string returns empty") {
    CHECK(FactionChatGarbler::garble("") == "");
}

TEST_CASE("FactionChatGarbler: spaces preserved") {
    std::string garbled = FactionChatGarbler::garble("a b c");
    CHECK(garbled[1] == ' ');
    CHECK(garbled[3] == ' ');
}
