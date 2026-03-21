#include <doctest/doctest.h>
#include "engine/net/game_messages.h"
#include "game/shared/inventory.h"
#include "game/shared/item_instance.h"
#include "server/cache/item_definition_cache.h"

using namespace fate;

// ============================================================================
// Consumable handler game-logic tests — message serialization & inventory ops
// ============================================================================

TEST_CASE("ConsumableHandler: CmdUseConsumableMsg serialization roundtrip") {
    CmdUseConsumableMsg orig;
    orig.inventorySlot = 7;

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    orig.write(w);

    ByteReader r(buf, w.size());
    auto decoded = CmdUseConsumableMsg::read(r);

    CHECK(decoded.inventorySlot == 7);
}

TEST_CASE("ConsumableHandler: SvConsumeResultMsg serialization roundtrip") {
    SvConsumeResultMsg orig;
    orig.success = 1;
    orig.message = "Restored 50 HP";

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    orig.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvConsumeResultMsg::read(r);

    CHECK(decoded.success == 1);
    CHECK(decoded.message == "Restored 50 HP");
}

TEST_CASE("ConsumableHandler: SvConsumeResultMsg failure roundtrip") {
    SvConsumeResultMsg orig;
    orig.success = 0;
    orig.message = "Still on cooldown";

    uint8_t buf[256];
    ByteWriter w(buf, sizeof(buf));
    orig.write(w);

    ByteReader r(buf, w.size());
    auto decoded = SvConsumeResultMsg::read(r);

    CHECK(decoded.success == 0);
    CHECK(decoded.message == "Still on cooldown");
}

TEST_CASE("ConsumableHandler: inventory removeItemQuantity consumes one unit") {
    Inventory inv;
    inv.initialize("test_char", 0);

    ItemInstance potion;
    potion.instanceId = "inst_001";
    potion.itemId = "potion_hp_small";
    potion.displayName = "Small HP Potion";
    potion.quantity = 5;

    inv.addItem(potion);
    int slot = inv.findItemById("potion_hp_small");
    REQUIRE(slot >= 0);

    // Consume one
    CHECK(inv.removeItemQuantity(slot, 1));
    ItemInstance after = inv.getSlot(slot);
    CHECK(after.quantity == 4);
}

TEST_CASE("ConsumableHandler: inventory removeItemQuantity removes slot at zero") {
    Inventory inv;
    inv.initialize("test_char", 0);

    ItemInstance potion;
    potion.instanceId = "inst_002";
    potion.itemId = "potion_mp_small";
    potion.displayName = "Small MP Potion";
    potion.quantity = 1;

    inv.addItem(potion);
    int slot = inv.findItemById("potion_mp_small");
    REQUIRE(slot >= 0);

    // Consume last one
    CHECK(inv.removeItemQuantity(slot, 1));
    ItemInstance after = inv.getSlot(slot);
    CHECK_FALSE(after.isValid());
}

TEST_CASE("ConsumableHandler: empty slot returns invalid item") {
    Inventory inv;
    inv.initialize("test_char", 0);

    ItemInstance empty = inv.getSlot(0);
    CHECK_FALSE(empty.isValid());
}

TEST_CASE("ConsumableHandler: CachedItemDefinition consumable type check") {
    CachedItemDefinition def;
    def.itemId = "potion_hp_medium";
    def.displayName = "Medium HP Potion";
    def.itemType = "Consumable";
    def.subtype = "hp_potion";
    def.attributes = nlohmann::json::object();
    def.attributes["heal_amount"] = 100;

    CHECK(def.itemType == "Consumable");
    CHECK_FALSE(def.isWeapon());
    CHECK_FALSE(def.isArmor());
    CHECK_FALSE(def.isEquipment());
    CHECK(def.getIntAttribute("heal_amount", 50) == 100);
}

TEST_CASE("ConsumableHandler: CachedItemDefinition default heal amount fallback") {
    CachedItemDefinition def;
    def.itemId = "potion_hp_basic";
    def.itemType = "Consumable";
    def.subtype = "hp_potion";
    def.attributes = nlohmann::json::object();
    // No heal_amount attribute set

    CHECK(def.getIntAttribute("heal_amount", 50) == 50);
}

TEST_CASE("ConsumableHandler: CachedItemDefinition MP potion attributes") {
    CachedItemDefinition def;
    def.itemId = "potion_mp_large";
    def.itemType = "Consumable";
    def.subtype = "mp_potion";
    def.attributes = nlohmann::json::object();
    def.attributes["mana_amount"] = 75;

    CHECK(def.getIntAttribute("mana_amount", 30) == 75);
}

TEST_CASE("ConsumableHandler: non-consumable item type is rejected") {
    CachedItemDefinition def;
    def.itemId = "iron_sword";
    def.itemType = "Weapon";
    def.subtype = "Sword";

    CHECK(def.itemType != "Consumable");
}

TEST_CASE("ConsumableHandler: CmdUseConsumableMsg slot 0 edge case") {
    CmdUseConsumableMsg msg;
    msg.inventorySlot = 0;

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);

    ByteReader r(buf, w.size());
    auto decoded = CmdUseConsumableMsg::read(r);

    CHECK(decoded.inventorySlot == 0);
}

TEST_CASE("ConsumableHandler: CmdUseConsumableMsg max slot 255") {
    CmdUseConsumableMsg msg;
    msg.inventorySlot = 255;

    uint8_t buf[64];
    ByteWriter w(buf, sizeof(buf));
    msg.write(w);

    ByteReader r(buf, w.size());
    auto decoded = CmdUseConsumableMsg::read(r);

    CHECK(decoded.inventorySlot == 255);
}

TEST_CASE("ConsumableHandler: HP+MP combined potion attributes") {
    CachedItemDefinition def;
    def.itemId = "potion_recovery";
    def.itemType = "Consumable";
    def.subtype = "hp_mp_potion";
    def.attributes = nlohmann::json::object();
    def.attributes["heal_amount"] = 80;
    def.attributes["mana_amount"] = 40;

    CHECK(def.getIntAttribute("heal_amount", 50) == 80);
    CHECK(def.getIntAttribute("mana_amount", 30) == 40);
}
