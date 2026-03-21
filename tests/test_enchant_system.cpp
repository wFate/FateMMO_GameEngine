#include <doctest/doctest.h>
#include "game/shared/enchant_system.h"

using namespace fate;

TEST_SUITE("EnchantSystem") {

TEST_CASE("Safe levels +1 to +8 have 100% success rate") {
    for (int level = 1; level <= 8; ++level) {
        CHECK(EnchantSystem::getSuccessRate(level) == 100);
    }
}

TEST_CASE("Risky levels have correct rates") {
    CHECK(EnchantSystem::getSuccessRate(9) == 50);
    CHECK(EnchantSystem::getSuccessRate(10) == 40);
    CHECK(EnchantSystem::getSuccessRate(11) == 30);
    CHECK(EnchantSystem::getSuccessRate(12) == 20);
    CHECK(EnchantSystem::getSuccessRate(13) == 10);
    CHECK(EnchantSystem::getSuccessRate(14) == 5);
    CHECK(EnchantSystem::getSuccessRate(15) == 2);
}

TEST_CASE("No break risk at or below safe threshold") {
    for (int level = 1; level <= 8; ++level) {
        CHECK_FALSE(EnchantSystem::hasBreakRisk(level));
    }
}

TEST_CASE("Break risk above safe threshold") {
    for (int level = 9; level <= 15; ++level) {
        CHECK(EnchantSystem::hasBreakRisk(level));
    }
}

TEST_CASE("MAX_ENCHANT_LEVEL is 15") {
    CHECK(EnchantConstants::MAX_ENCHANT_LEVEL == 15);
}

TEST_CASE("SAFE_ENCHANT_LEVEL is 8") {
    CHECK(EnchantConstants::SAFE_ENCHANT_LEVEL == 8);
}

TEST_CASE("Gold costs for safe levels") {
    CHECK(EnchantSystem::getGoldCost(1) == 100);
    CHECK(EnchantSystem::getGoldCost(4) == 500);
    CHECK(EnchantSystem::getGoldCost(7) == 2000);
    CHECK(EnchantSystem::getGoldCost(8) == 2000);
}

TEST_CASE("Gold costs for risky levels") {
    CHECK(EnchantSystem::getGoldCost(9) == 10000);
    CHECK(EnchantSystem::getGoldCost(12) == 100000);
    CHECK(EnchantSystem::getGoldCost(15) == 2000000);
}

TEST_CASE("Weapon damage multiplier at +15") {
    float mult = EnchantSystem::getWeaponDamageMultiplier(15);
    // 2.875 base * 1.05 (+11) * 1.10 (+12) * 1.15 (+15) = ~3.82
    CHECK(mult == doctest::Approx(3.82f).epsilon(0.05f));
}

TEST_CASE("Armor bonus at +15 is 29") {
    CHECK(EnchantSystem::getArmorBonus(15) == 29);
}

TEST_CASE("Armor bonus at +8 is 8") {
    CHECK(EnchantSystem::getArmorBonus(8) == 8);
}

TEST_CASE("Secret bonus at +15 is doubled") {
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Hat, 15) == 20);
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Shoes, 15) == 20);
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Armor, 15) == 20);
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::SubWeapon, 15) == 20);
}

TEST_CASE("Secret bonus at +12 is 10") {
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Hat, 12) == 10);
}

TEST_CASE("No secret bonus for Gloves") {
    CHECK(EnchantSystem::getSecretBonusValue(EquipmentSlot::Gloves, 15) == 0);
}

TEST_CASE("Repair returns level between 1 and SAFE_ENCHANT_LEVEL") {
    for (int i = 0; i < 100; ++i) {
        int repaired = EnchantSystem::rollRepairLevel();
        CHECK(repaired >= 1);
        CHECK(repaired <= EnchantConstants::SAFE_ENCHANT_LEVEL);
    }
}

} // TEST_SUITE
