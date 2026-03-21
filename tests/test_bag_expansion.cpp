#include <doctest/doctest.h>
#include "game/shared/inventory.h"
#include "game/shared/item_instance.h"
#include "game/shared/bag_definition.h"
using namespace fate;

static ItemInstance makeItem(const std::string& id, const std::string& itemId, int qty = 1) {
    return ItemInstance::createSimple(id, itemId, qty);
}

TEST_SUITE("Bag System - Nested Containers") {

TEST_CASE("inventory is always exactly 15 slots") {
    Inventory inv;
    inv.initialize("test", 0);
    CHECK(inv.totalSlots() == 15);
}

TEST_CASE("setBagCapacity creates sub-slots") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "leather_bag"));
    inv.setBagCapacity(0, 6);
    CHECK(inv.bagSlotCount(0) == 6);
    CHECK(inv.bagUsedSlots(0) == 0);
    CHECK(inv.bagFreeSlots(0) == 6);
}

TEST_CASE("setBagCapacity clamps to max 10") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "big_bag"));
    inv.setBagCapacity(0, 20);
    CHECK(inv.bagSlotCount(0) == 10);
}

TEST_CASE("addItemToBag places item in first empty sub-slot") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "leather_bag"));
    inv.setBagCapacity(0, 4);

    CHECK(inv.addItemToBag(0, makeItem("sword_1", "iron_sword")));
    CHECK(inv.bagUsedSlots(0) == 1);
    CHECK(inv.getBagItem(0, 0).itemId == "iron_sword");
}

TEST_CASE("addItemToBag to full bag fails") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "tiny_bag"));
    inv.setBagCapacity(0, 2);

    CHECK(inv.addItemToBag(0, makeItem("i1", "item_a")));
    CHECK(inv.addItemToBag(0, makeItem("i2", "item_b")));
    CHECK_FALSE(inv.addItemToBag(0, makeItem("i3", "item_c"))); // full
}

TEST_CASE("addItemToBag to non-bag slot fails") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("sword_1", "iron_sword")); // not a bag
    CHECK_FALSE(inv.addItemToBag(0, makeItem("i1", "potion")));
}

TEST_CASE("removeItemFromBag clears sub-slot") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "leather_bag"));
    inv.setBagCapacity(0, 4);
    inv.addItemToBag(0, makeItem("i1", "iron_sword"));

    CHECK(inv.removeItemFromBag(0, 0));
    CHECK(inv.bagUsedSlots(0) == 0);
    CHECK_FALSE(inv.getBagItem(0, 0).isValid());
}

TEST_CASE("removeItemFromBag invalid sub-slot fails") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "leather_bag"));
    inv.setBagCapacity(0, 4);
    CHECK_FALSE(inv.removeItemFromBag(0, 99));
    CHECK_FALSE(inv.removeItemFromBag(0, -1));
}

TEST_CASE("cannot remove bag slot if bag has contents") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "leather_bag"));
    inv.setBagCapacity(0, 4);
    inv.addItemToBag(0, makeItem("i1", "iron_sword"));

    CHECK_FALSE(inv.removeItem(0)); // bag not empty
    CHECK(inv.getSlot(0).itemId == "leather_bag"); // still there
}

TEST_CASE("can remove bag slot after emptying contents") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "leather_bag"));
    inv.setBagCapacity(0, 4);
    inv.addItemToBag(0, makeItem("i1", "iron_sword"));
    inv.removeItemFromBag(0, 0); // empty the bag

    CHECK(inv.removeItem(0)); // now it works
    CHECK_FALSE(inv.getSlot(0).isValid());
    CHECK(inv.bagSlotCount(0) == 0); // bag contents cleared
}

TEST_CASE("clearBagContents removes all sub-items") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "leather_bag"));
    inv.setBagCapacity(0, 4);
    inv.addItemToBag(0, makeItem("i1", "item_a"));
    inv.addItemToBag(0, makeItem("i2", "item_b"));

    inv.clearBagContents(0);
    CHECK(inv.bagSlotCount(0) == 0);
}

TEST_CASE("BagDefinition validates slotCount 1-10") {
    BagDefinition bag;
    bag.slotCount = 0;
    bag.validate();
    CHECK(bag.slotCount == 1);

    bag.slotCount = 15;
    bag.validate();
    CHECK(bag.slotCount == 10);

    bag.slotCount = 7;
    bag.validate();
    CHECK(bag.slotCount == 7);
}

TEST_CASE("getBagContents returns sub-items") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "leather_bag"));
    inv.setBagCapacity(0, 3);
    inv.addItemToBag(0, makeItem("i1", "item_a"));
    inv.addItemToBag(0, makeItem("i2", "item_b"));

    auto& contents = inv.getBagContents(0);
    CHECK(contents.size() == 3);
    CHECK(contents[0].itemId == "item_a");
    CHECK(contents[1].itemId == "item_b");
    CHECK_FALSE(contents[2].isValid()); // empty sub-slot
}

TEST_CASE("multiple bags in different slots") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.addItemToSlot(0, makeItem("bag_1", "small_bag"));
    inv.addItemToSlot(1, makeItem("bag_2", "large_bag"));
    inv.setBagCapacity(0, 4);
    inv.setBagCapacity(1, 8);

    CHECK(inv.bagSlotCount(0) == 4);
    CHECK(inv.bagSlotCount(1) == 8);

    inv.addItemToBag(0, makeItem("i1", "potion"));
    inv.addItemToBag(1, makeItem("i2", "sword"));

    CHECK(inv.bagUsedSlots(0) == 1);
    CHECK(inv.bagUsedSlots(1) == 1);
    CHECK(inv.getBagItem(0, 0).itemId == "potion");
    CHECK(inv.getBagItem(1, 0).itemId == "sword");
}

} // TEST_SUITE
