#include <doctest/doctest.h>
#include "game/shared/inventory.h"

using namespace fate;

TEST_SUITE("Inventory Slot Safety") {

TEST_CASE("addItemToSlot to empty slot succeeds") {
    Inventory inv;
    inv.initialize("test_char", 0);
    ItemInstance item;
    item.itemId = "sword_01";
    item.instanceId = "inst_001";
    item.quantity = 1;
    CHECK(inv.addItemToSlot(0, item));
    CHECK(inv.getSlot(0).itemId == "sword_01");
}

TEST_CASE("addItemToSlot to occupied slot fails") {
    Inventory inv;
    inv.initialize("test_char", 0);
    ItemInstance item1, item2;
    item1.itemId = "sword_01"; item1.instanceId = "inst_001"; item1.quantity = 1;
    item2.itemId = "shield_01"; item2.instanceId = "inst_002"; item2.quantity = 1;
    REQUIRE(inv.addItemToSlot(0, item1));
    CHECK_FALSE(inv.addItemToSlot(0, item2));
    CHECK(inv.getSlot(0).itemId == "sword_01"); // original preserved
}

TEST_CASE("addItemToSlot to out-of-range slot fails") {
    Inventory inv;
    inv.initialize("test_char", 0);
    ItemInstance item;
    item.itemId = "potion_01";
    item.instanceId = "inst_001";
    item.quantity = 1;
    CHECK_FALSE(inv.addItemToSlot(-1, item));
    CHECK_FALSE(inv.addItemToSlot(9999, item));
}

TEST_CASE("addItemToSlot to trade-locked slot fails") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.lockSlotForTrade(0);
    ItemInstance item;
    item.itemId = "gem_01";
    item.instanceId = "inst_001";
    item.quantity = 1;
    CHECK_FALSE(inv.addItemToSlot(0, item));
    inv.unlockSlotForTrade(0);
}

TEST_CASE("remove then add to same slot succeeds") {
    Inventory inv;
    inv.initialize("test_char", 0);
    ItemInstance item1, item2;
    item1.itemId = "sword_01"; item1.instanceId = "inst_001"; item1.quantity = 1;
    item2.itemId = "axe_01"; item2.instanceId = "inst_002"; item2.quantity = 1;
    REQUIRE(inv.addItemToSlot(0, item1));
    REQUIRE(inv.removeItem(0));
    CHECK(inv.addItemToSlot(0, item2));
    CHECK(inv.getSlot(0).itemId == "axe_01");
}

} // TEST_SUITE
