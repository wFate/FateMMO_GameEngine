#include <doctest/doctest.h>
#include "game/shared/profanity_filter.h"

using namespace fate;

TEST_CASE("ProfanityFilter: censors bad words in chat") {
    auto result = ProfanityFilter::filterChatMessage("you are a damn fool", FilterMode::Censor);
    CHECK(result.isClean == true); // Censor mode returns clean with asterisked text
    CHECK(result.filteredText.find("****") != std::string::npos);
}

TEST_CASE("ProfanityFilter: validate mode rejects bad words") {
    auto result = ProfanityFilter::filterChatMessage("you are a damn fool", FilterMode::Validate);
    CHECK(result.isClean == false);
}

TEST_CASE("ProfanityFilter: rejects blocked phrases") {
    auto result = ProfanityFilter::filterChatMessage("kys noob", FilterMode::Validate);
    CHECK(result.isClean == false);
}

TEST_CASE("ProfanityFilter: passes clean messages") {
    auto result = ProfanityFilter::filterChatMessage("hello friend, nice day!", FilterMode::Censor);
    CHECK(result.isClean == true);
    CHECK(result.filteredText == "hello friend, nice day!");
}

TEST_CASE("ProfanityFilter: enforces max message length") {
    std::string longMsg(250, 'a');
    auto result = ProfanityFilter::filterChatMessage(longMsg, FilterMode::Validate);
    CHECK(result.isClean == false);
}
