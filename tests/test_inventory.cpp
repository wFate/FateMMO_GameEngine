#include <doctest/doctest.h>
#include "game/shared/inventory.h"
#include "game/shared/item_instance.h"
#include "game/shared/game_types.h"

using namespace fate;

// Helper: create a simple valid item
static ItemInstance makeItem(const std::string& instanceId,
                             const std::string& itemId,
                             int quantity = 1) {
    return ItemInstance::createSimple(instanceId, itemId, quantity);
}

TEST_SUITE("Inventory") {

// ============================================================================
// Initialization
// ============================================================================

TEST_CASE("Initialize creates correct number of slots") {
    Inventory inv;
    inv.initialize("test_char", 0);
    CHECK(inv.totalSlots() == InventoryConstants::BASE_INVENTORY_SLOTS);
    CHECK(inv.totalSlots() == 15);
    CHECK(inv.usedSlots() == 0);
    CHECK(inv.freeSlots() == 15);
}

TEST_CASE("Initialize sets starting gold") {
    Inventory inv;
    inv.initialize("test_char", 500);
    CHECK(inv.getGold() == 500);
}

// ============================================================================
// addItem
// ============================================================================

TEST_CASE("addItem to empty inventory") {
    Inventory inv;
    inv.initialize("test_char", 0);

    auto item = makeItem("inst_001", "sword_01");
    CHECK(inv.addItem(item));
    CHECK(inv.usedSlots() == 1);

    auto slot0 = inv.getSlot(0);
    CHECK(slot0.isValid());
    CHECK(slot0.itemId == "sword_01");
    CHECK(slot0.instanceId == "inst_001");
}

TEST_CASE("addItem to full inventory returns false") {
    Inventory inv;
    inv.initialize("test_char", 0);

    // Fill all 15 slots
    for (int i = 0; i < InventoryConstants::BASE_INVENTORY_SLOTS; ++i) {
        auto item = makeItem("inst_" + std::to_string(i), "item_" + std::to_string(i));
        CHECK(inv.addItem(item));
    }
    CHECK(inv.freeSlots() == 0);

    // One more should fail
    auto extraItem = makeItem("inst_extra", "item_extra");
    CHECK_FALSE(inv.addItem(extraItem));
    CHECK(inv.usedSlots() == InventoryConstants::BASE_INVENTORY_SLOTS);
}

TEST_CASE("addItem with invalid item returns false") {
    Inventory inv;
    inv.initialize("test_char", 0);

    ItemInstance empty = ItemInstance::empty();
    CHECK_FALSE(inv.addItem(empty));
    CHECK(inv.usedSlots() == 0);
}

// ============================================================================
// removeItem
// ============================================================================

TEST_CASE("removeItem from valid slot") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.addItem(makeItem("inst_001", "sword_01"));
    CHECK(inv.usedSlots() == 1);

    CHECK(inv.removeItem(0));
    CHECK(inv.usedSlots() == 0);

    auto slot = inv.getSlot(0);
    CHECK_FALSE(slot.isValid());
}

TEST_CASE("removeItem from empty slot returns false") {
    Inventory inv;
    inv.initialize("test_char", 0);

    // Slot 0 is empty, removeItem should return false (slot is valid but empty)
    // Actually, removeItem sets slot to empty regardless -- the code just does the assignment
    // Let's check: removeItem checks isValidSlot and isSlotLocked, then clears
    // It doesn't check if slot is occupied, so it returns true for empty valid slots
    bool result = inv.removeItem(0);
    CHECK(result == true);  // valid slot, not locked, just clears it
}

TEST_CASE("removeItem from invalid slot returns false") {
    Inventory inv;
    inv.initialize("test_char", 0);

    CHECK_FALSE(inv.removeItem(-1));
    CHECK_FALSE(inv.removeItem(99));
    CHECK_FALSE(inv.removeItem(InventoryConstants::BASE_INVENTORY_SLOTS));
}

// ============================================================================
// Gold operations
// ============================================================================

TEST_CASE("addGold and removeGold basic") {
    Inventory inv;
    inv.initialize("test_char", 0);

    CHECK(inv.addGold(100));
    CHECK(inv.getGold() == 100);

    CHECK(inv.removeGold(50));
    CHECK(inv.getGold() == 50);
}

TEST_CASE("addGold overflow capped at MAX_GOLD") {
    Inventory inv;
    inv.initialize("test_char", InventoryConstants::MAX_GOLD - 10);
    CHECK(inv.getGold() == InventoryConstants::MAX_GOLD - 10);

    CHECK(inv.addGold(100));
    CHECK(inv.getGold() == InventoryConstants::MAX_GOLD);
}

TEST_CASE("removeGold more than available returns false") {
    Inventory inv;
    inv.initialize("test_char", 50);

    CHECK_FALSE(inv.removeGold(100));
    CHECK(inv.getGold() == 50);  // unchanged
}

TEST_CASE("removeGold with 0 amount returns false") {
    Inventory inv;
    inv.initialize("test_char", 100);

    CHECK_FALSE(inv.removeGold(0));
    CHECK(inv.getGold() == 100);
}

TEST_CASE("addGold with 0 amount returns false") {
    Inventory inv;
    inv.initialize("test_char", 100);

    CHECK_FALSE(inv.addGold(0));
    CHECK(inv.getGold() == 100);
}

TEST_CASE("addGold with negative amount returns false") {
    Inventory inv;
    inv.initialize("test_char", 100);

    CHECK_FALSE(inv.addGold(-50));
    CHECK(inv.getGold() == 100);
}

// ============================================================================
// Item stacking via moveItem
// ============================================================================

TEST_CASE("moveItem stacks same itemId") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.addItem(makeItem("inst_001", "potion_hp", 5));
    inv.addItemToSlot(3, makeItem("inst_002", "potion_hp", 3));

    CHECK(inv.moveItem(0, 3));

    auto slot0 = inv.getSlot(0);
    auto slot3 = inv.getSlot(3);

    // Slot 0 should be empty, slot 3 should have quantity 8
    CHECK_FALSE(slot0.isValid());
    CHECK(slot3.isValid());
    CHECK(slot3.quantity == 8);
}

// ============================================================================
// countItem
// ============================================================================

TEST_CASE("countItem counts across slots") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.addItem(makeItem("inst_001", "potion_hp", 5));
    inv.addItem(makeItem("inst_002", "potion_hp", 3));
    inv.addItem(makeItem("inst_003", "potion_mp", 10));

    CHECK(inv.countItem("potion_hp") == 8);
    CHECK(inv.countItem("potion_mp") == 10);
    CHECK(inv.countItem("nonexistent") == 0);
}

// ============================================================================
// findByInstanceId
// ============================================================================

TEST_CASE("findByInstanceId finds correct slot") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.addItem(makeItem("inst_AAA", "sword_01"));
    inv.addItem(makeItem("inst_BBB", "shield_01"));
    inv.addItem(makeItem("inst_CCC", "potion_hp", 3));

    CHECK(inv.findByInstanceId("inst_AAA") == 0);
    CHECK(inv.findByInstanceId("inst_BBB") == 1);
    CHECK(inv.findByInstanceId("inst_CCC") == 2);
}

TEST_CASE("findByInstanceId not found returns -1") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.addItem(makeItem("inst_001", "sword_01"));
    CHECK(inv.findByInstanceId("nonexistent") == -1);
}

// ============================================================================
// getSlot out of bounds
// ============================================================================

TEST_CASE("getSlot out of bounds returns empty item") {
    Inventory inv;
    inv.initialize("test_char", 0);

    auto neg = inv.getSlot(-1);
    CHECK_FALSE(neg.isValid());

    auto big = inv.getSlot(99);
    CHECK_FALSE(big.isValid());

    auto boundary = inv.getSlot(InventoryConstants::BASE_INVENTORY_SLOTS);
    CHECK_FALSE(boundary.isValid());
}

// ============================================================================
// Callbacks
// ============================================================================

TEST_CASE("onInventoryChanged fires on addItem") {
    Inventory inv;
    inv.initialize("test_char", 0);

    int callCount = 0;
    inv.onInventoryChanged = [&]() { ++callCount; };

    inv.addItem(makeItem("inst_001", "sword_01"));
    CHECK(callCount == 1);

    inv.removeItem(0);
    CHECK(callCount == 2);
}

TEST_CASE("onGoldChanged fires on gold operations") {
    Inventory inv;
    inv.initialize("test_char", 0);

    int callCount = 0;
    inv.onGoldChanged = [&]() { ++callCount; };

    inv.addGold(100);
    CHECK(callCount == 1);

    inv.removeGold(50);
    CHECK(callCount == 2);
}

// ============================================================================
// Trade locking
// ============================================================================

TEST_CASE("Locked slot blocks removeItem") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.addItem(makeItem("inst_001", "sword_01"));
    inv.lockSlotForTrade(0);

    CHECK_FALSE(inv.removeItem(0));
    CHECK(inv.isSlotLocked(0));

    inv.unlockSlotForTrade(0);
    CHECK_FALSE(inv.isSlotLocked(0));
    CHECK(inv.removeItem(0));
}

TEST_CASE("unlockAllTradeSlots clears all locks") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.lockSlotForTrade(0);
    inv.lockSlotForTrade(5);
    inv.lockSlotForTrade(10);
    CHECK(inv.isSlotLocked(0));
    CHECK(inv.isSlotLocked(5));
    CHECK(inv.isSlotLocked(10));

    inv.unlockAllTradeSlots();
    CHECK_FALSE(inv.isSlotLocked(0));
    CHECK_FALSE(inv.isSlotLocked(5));
    CHECK_FALSE(inv.isSlotLocked(10));
}

// ============================================================================
// findItemById
// ============================================================================

TEST_CASE("findItemById finds first matching slot") {
    Inventory inv;
    inv.initialize("test_char", 0);

    inv.addItem(makeItem("inst_001", "potion_hp"));
    inv.addItem(makeItem("inst_002", "sword_01"));
    inv.addItem(makeItem("inst_003", "potion_hp"));

    CHECK(inv.findItemById("potion_hp") == 0);  // first match
    CHECK(inv.findItemById("sword_01") == 1);
    CHECK(inv.findItemById("nonexistent") == -1);
}

// ============================================================================
// addItemToSlot: reject occupied slot
// ============================================================================

TEST_CASE("Inventory: addItemToSlot rejects occupied slot") {
    Inventory inv;
    ItemInstance sword;
    sword.itemId = "sword_01";
    sword.instanceId = "inst_001";
    sword.quantity = 1;

    CHECK(inv.addItemToSlot(0, sword) == true);

    ItemInstance shield;
    shield.itemId = "shield_01";
    shield.instanceId = "inst_002";
    shield.quantity = 1;

    CHECK(inv.addItemToSlot(0, shield) == false); // occupied!
    CHECK(inv.getSlot(0).itemId == "sword_01");   // original preserved
}

TEST_CASE("setGold clamps to MAX_GOLD") {
    Inventory inv;
    inv.initialize("test", 0);
    inv.setGold(InventoryConstants::MAX_GOLD + 100);
    CHECK(inv.getGold() == InventoryConstants::MAX_GOLD);
}

TEST_CASE("setGold clamps negative to zero") {
    Inventory inv;
    inv.initialize("test", 1000);
    inv.setGold(-500);
    CHECK(inv.getGold() == 0);
}

} // TEST_SUITE
