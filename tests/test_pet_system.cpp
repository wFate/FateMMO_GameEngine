#include <doctest/doctest.h>
#include "game/shared/pet_system.h"

using namespace fate;

static PetDefinition makeTestDef(ItemRarity rarity = ItemRarity::Common) {
    PetDefinition def;
    def.petId = "pet_wolf";
    def.displayName = "Wolf";
    def.rarity = rarity;
    def.baseHP = 10;
    def.baseCritRate = 0.01f;
    def.baseExpBonus = 0.02f;
    def.hpPerLevel = 2.0f;
    def.critPerLevel = 0.002f;
    def.expBonusPerLevel = 0.005f;
    return def;
}

TEST_CASE("PetSystem: stat calculation at level 1") {
    auto def = makeTestDef();
    PetInstance pet;
    pet.level = 1;

    CHECK(PetSystem::effectiveHP(def, pet) == 10);
    CHECK(PetSystem::effectiveCritRate(def, pet) == doctest::Approx(0.01f));
    CHECK(PetSystem::effectiveExpBonus(def, pet) == doctest::Approx(0.02f));
}

TEST_CASE("PetSystem: stat calculation scales with level") {
    auto def = makeTestDef();
    PetInstance pet;
    pet.level = 10;

    CHECK(PetSystem::effectiveHP(def, pet) == 28);
    CHECK(PetSystem::effectiveCritRate(def, pet) == doctest::Approx(0.01f + 0.002f * 9));
    CHECK(PetSystem::effectiveExpBonus(def, pet) == doctest::Approx(0.02f + 0.005f * 9));
}

TEST_CASE("PetSystem: addXP levels up pet") {
    auto def = makeTestDef();
    PetInstance pet;
    pet.level = 1;
    pet.currentXP = 0;
    pet.xpToNextLevel = 100;

    int playerLevel = 50;
    PetSystem::addXP(def, pet, 150, playerLevel);

    CHECK(pet.level == 2);
    CHECK(pet.currentXP == 50);
}

TEST_CASE("PetSystem: pet cannot outlevel player") {
    auto def = makeTestDef();
    PetInstance pet;
    pet.level = 5;
    pet.currentXP = 0;
    pet.xpToNextLevel = 100;

    int playerLevel = 5;
    PetSystem::addXP(def, pet, 9999, playerLevel);

    CHECK(pet.level == 5);
    CHECK(pet.currentXP == 0);
}

TEST_CASE("PetSystem: createInstance sets defaults") {
    auto def = makeTestDef();
    auto pet = PetSystem::createInstance(def, "inst_001");
    CHECK(pet.instanceId == "inst_001");
    CHECK(pet.petDefinitionId == "pet_wolf");
    CHECK(pet.petName == "Wolf");
    CHECK(pet.level == 1);
    CHECK(pet.autoLootEnabled == false);
    CHECK(pet.isSoulbound == false);
}
