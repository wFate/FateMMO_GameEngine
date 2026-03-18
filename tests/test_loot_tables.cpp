#include <doctest/doctest.h>
#include "server/cache/loot_table_cache.h"

TEST_CASE("rollEnchantLevel returns valid range") {
    for (int i = 0; i < 100; ++i) {
        int level = fate::LootTableCache::rollEnchantLevel("Sword");
        CHECK(level >= 0);
        CHECK(level <= 7);
    }
}

TEST_CASE("rollEnchantLevel returns 0 for non-enchantable subtypes") {
    CHECK(fate::LootTableCache::rollEnchantLevel("Necklace") == 0);
    CHECK(fate::LootTableCache::rollEnchantLevel("Ring") == 0);
    CHECK(fate::LootTableCache::rollEnchantLevel("Cloak") == 0);
    CHECK(fate::LootTableCache::rollEnchantLevel("Belt") == 0);
}

TEST_CASE("rollEnchantLevel enchantable subtypes") {
    bool gotNonZero = false;
    for (int i = 0; i < 200; ++i) {
        if (fate::LootTableCache::rollEnchantLevel("Armor") > 0) {
            gotNonZero = true;
            break;
        }
    }
    CHECK(gotNonZero);
}
