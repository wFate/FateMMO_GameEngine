#include <doctest/doctest.h>
#include "game/shared/core_extraction.h"

using namespace fate;

TEST_SUITE("CoreExtraction") {

TEST_CASE("Common items cannot be extracted") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Common, 5, 0);
    CHECK_FALSE(result.success);
}

TEST_CASE("Green Lv5 yields 1st Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 5, 0);
    CHECK(result.success);
    CHECK(result.coreItemId == "mat_core_1st");
    CHECK(result.quantity == 1);
}

TEST_CASE("Green Lv15 yields 2nd Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 15, 0);
    CHECK(result.coreItemId == "mat_core_2nd");
}

TEST_CASE("Green Lv25 yields 3rd Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 25, 0);
    CHECK(result.coreItemId == "mat_core_3rd");
}

TEST_CASE("Green Lv35 yields 4th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 35, 0);
    CHECK(result.coreItemId == "mat_core_4th");
}

TEST_CASE("Green Lv45 yields 5th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 45, 0);
    CHECK(result.coreItemId == "mat_core_5th");
}

TEST_CASE("Blue item yields 6th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Rare, 10, 0);
    CHECK(result.coreItemId == "mat_core_6th");
}

TEST_CASE("Purple item yields 7th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Epic, 10, 0);
    CHECK(result.coreItemId == "mat_core_7th");
}

TEST_CASE("Legendary item yields 7th Core") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Legendary, 10, 0);
    CHECK(result.coreItemId == "mat_core_7th");
}

TEST_CASE("Enchant +9 yields bonus cores (1 + 9/3 = 4)") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 5, 9);
    CHECK(result.quantity == 4);
}

TEST_CASE("Enchant +3 yields 2 cores (1 + 3/3 = 2)") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 5, 3);
    CHECK(result.quantity == 2);
}

TEST_CASE("Enchant +1 yields 1 core (1 + 1/3 = 1)") {
    auto result = CoreExtraction::determineCoreResult(ItemRarity::Uncommon, 5, 1);
    CHECK(result.quantity == 1);
}

} // TEST_SUITE
