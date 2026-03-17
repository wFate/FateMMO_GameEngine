#include <doctest/doctest.h>
#include "game/shared/stat_enchant_system.h"
#include "game/shared/item_instance.h"

using namespace fate;

TEST_CASE("StatEnchantSystem: canStatEnchant only for accessories") {
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Belt) == true);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Ring) == true);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Necklace) == true);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Cloak) == true);

    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Weapon) == false);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Armor) == false);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Hat) == false);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Shoes) == false);
    CHECK(StatEnchantSystem::canStatEnchant(EquipmentSlot::Gloves) == false);
}

TEST_CASE("StatEnchantSystem: getStatValue for primary stats") {
    CHECK(StatEnchantSystem::getStatValue(StatType::Strength, 0) == 0);
    CHECK(StatEnchantSystem::getStatValue(StatType::Strength, 1) == 1);
    CHECK(StatEnchantSystem::getStatValue(StatType::Dexterity, 5) == 5);
}

TEST_CASE("StatEnchantSystem: getStatValue for HP/MP uses x10 scaling") {
    CHECK(StatEnchantSystem::getStatValue(StatType::MaxHealth, 0) == 0);
    CHECK(StatEnchantSystem::getStatValue(StatType::MaxHealth, 1) == 10);
    CHECK(StatEnchantSystem::getStatValue(StatType::MaxHealth, 5) == 50);
    CHECK(StatEnchantSystem::getStatValue(StatType::MaxMana, 3) == 30);
}

TEST_CASE("StatEnchantSystem: applyStatEnchant sets fields") {
    ItemInstance item = ItemInstance::createSimple("inst1", "ring_iron", 1);
    StatEnchantSystem::applyStatEnchant(item, StatType::Dexterity, 3);
    CHECK(item.statEnchantType == StatType::Dexterity);
    CHECK(item.statEnchantValue == 3);
}

TEST_CASE("StatEnchantSystem: applyStatEnchant replaces previous") {
    ItemInstance item = ItemInstance::createSimple("inst1", "ring_iron", 1);
    StatEnchantSystem::applyStatEnchant(item, StatType::Strength, 4);
    CHECK(item.statEnchantValue == 4);

    StatEnchantSystem::applyStatEnchant(item, StatType::Dexterity, 2);
    CHECK(item.statEnchantType == StatType::Dexterity);
    CHECK(item.statEnchantValue == 2);
}

TEST_CASE("StatEnchantSystem: fail tier removes enchant") {
    ItemInstance item = ItemInstance::createSimple("inst1", "ring_iron", 1);
    StatEnchantSystem::applyStatEnchant(item, StatType::Strength, 5);
    CHECK(item.statEnchantValue == 5);

    StatEnchantSystem::applyStatEnchant(item, StatType::Intelligence, 0);
    CHECK(item.statEnchantValue == 0);
    CHECK(item.statEnchantType == StatType::Strength);  // reset to neutral default
}

TEST_CASE("StatEnchantSystem: rollStatEnchant returns 0-5") {
    for (int i = 0; i < 100; ++i) {
        int tier = StatEnchantSystem::rollStatEnchant();
        CHECK(tier >= 0);
        CHECK(tier <= 5);
    }
}
