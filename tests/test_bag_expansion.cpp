#include <doctest/doctest.h>
#include "game/shared/inventory.h"
#include "game/shared/item_instance.h"
#include "game/shared/game_types.h"
#include "game/shared/bag_definition.h"

using namespace fate;

static ItemInstance makeItem(const std::string& instanceId,
                             const std::string& itemId,
                             int quantity = 1) {
    return ItemInstance::createSimple(instanceId, itemId, quantity);
}

TEST_SUITE("Bag Expansion") {

// ============================================================================
// expandSlots
// ============================================================================

TEST_CASE("expandSlots adds extra inventory slots") {
    Inventory inv;
    inv.initialize("test_char", 0);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS);

    inv.expandSlots(6);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 6);
    CHECK(inv.freeSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 6);
}

TEST_CASE("expandSlots with zero does nothing") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(0);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS);
}

TEST_CASE("expandSlots with negative does nothing") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(-5);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS);
}

TEST_CASE("expandSlots caps at BASE + MAX_BAG_SLOTS") {
    Inventory inv;
    inv.initialize("test_char", 0);
    int maxTotal = InventoryConstants::BASE_INVENTORY_SLOTS + InventoryConstants::MAX_BAG_SLOTS;

    inv.expandSlots(100);
    CHECK(inv.totalSlots() == maxTotal);
}

TEST_CASE("expandSlots cumulative from multiple bags") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.expandSlots(4);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 4);

    inv.expandSlots(6);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 10);
}

TEST_CASE("expanded slots can hold items") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(5);

    // Fill base slots
    for (int i = 0; i < InventoryConstants::BASE_INVENTORY_SLOTS; ++i) {
        CHECK(inv.addItem(makeItem("inst_" + std::to_string(i), "item_" + std::to_string(i))));
    }
    CHECK(inv.freeSlots() == 5);

    // Fill expanded slots
    for (int i = 0; i < 5; ++i) {
        CHECK(inv.addItem(makeItem("bag_inst_" + std::to_string(i), "bag_item_" + std::to_string(i))));
    }
    CHECK(inv.freeSlots() == 0);
    CHECK(inv.usedSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 5);
}

// ============================================================================
// shrinkSlots
// ============================================================================

TEST_CASE("shrinkSlots removes empty expanded slots") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(6);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 6);

    CHECK(inv.shrinkSlots(6));
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS);
}

TEST_CASE("shrinkSlots rejects when extended slots contain items") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(4);

    // Fill all slots including expanded ones
    for (int i = 0; i < InventoryConstants::BASE_INVENTORY_SLOTS + 4; ++i) {
        inv.addItem(makeItem("inst_" + std::to_string(i), "item_" + std::to_string(i)));
    }

    CHECK_FALSE(inv.shrinkSlots(4));
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 4);
}

TEST_CASE("shrinkSlots with zero returns false") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(6);
    CHECK_FALSE(inv.shrinkSlots(0));
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 6);
}

TEST_CASE("shrinkSlots with negative returns false") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(6);
    CHECK_FALSE(inv.shrinkSlots(-3));
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 6);
}

TEST_CASE("shrinkSlots does not go below BASE_INVENTORY_SLOTS") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(4);

    // Try to shrink more than expanded
    CHECK(inv.shrinkSlots(10));
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS);
}

TEST_CASE("shrinkSlots partial: only empty tail removed") {
    Inventory inv;
    inv.initialize("test_char", 0);
    inv.expandSlots(8);

    // Put an item in expanded slot 16 (index 15 is first expanded)
    inv.addItemToSlot(InventoryConstants::BASE_INVENTORY_SLOTS, makeItem("inst_X", "item_X"));

    // Can shrink by 7 (slots 16-22 are empty) but not by 8 (slot 15 has item)
    CHECK(inv.shrinkSlots(7));
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 1);

    // Cannot shrink further since that slot has an item
    CHECK_FALSE(inv.shrinkSlots(1));
}

// ============================================================================
// BagDefinition validation
// ============================================================================

TEST_CASE("BagDefinition validate clamps slotCount") {
    BagDefinition bag;
    bag.slotCount = 0;
    bag.validate();
    CHECK(bag.slotCount == 1);

    bag.slotCount = 25;
    bag.validate();
    CHECK(bag.slotCount == 20);

    bag.slotCount = 10;
    bag.validate();
    CHECK(bag.slotCount == 10);
}

// ============================================================================
// totalSlots reflects actual vector size
// ============================================================================

TEST_CASE("totalSlots returns actual slots vector size") {
    Inventory inv;
    inv.initialize("test_char", 0);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS);

    inv.expandSlots(5);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 5);

    inv.shrinkSlots(3);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 2);
}

// ============================================================================
// freeSlots with expansion
// ============================================================================

TEST_CASE("freeSlots reflects expansion correctly") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.addItem(makeItem("inst_1", "item_1"));
    CHECK(inv.freeSlots() == InventoryConstants::BASE_INVENTORY_SLOTS - 1);

    inv.expandSlots(6);
    CHECK(inv.freeSlots() == InventoryConstants::BASE_INVENTORY_SLOTS + 6 - 1);
}

} // TEST_SUITE
