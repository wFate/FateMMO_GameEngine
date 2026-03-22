#include <doctest/doctest.h>
#include "game/shared/pet_system.h"
#include "game/components/pet_component.h"

using namespace fate;

TEST_CASE("PetAutoLoot: pet with autoLoot enabled is detected") {
    PetComponent petComp;
    petComp.equippedPet.petDefinitionId = "pet_wolf";
    petComp.equippedPet.instanceId = "inst_001";
    petComp.equippedPet.autoLootEnabled = true;
    petComp.autoLootRadius = 64.0f;

    CHECK(petComp.hasPet());
    CHECK(petComp.equippedPet.autoLootEnabled);
    CHECK(petComp.autoLootRadius == doctest::Approx(64.0f));
}

TEST_CASE("PetAutoLoot: pet with autoLoot disabled") {
    PetComponent petComp;
    petComp.equippedPet.petDefinitionId = "pet_wolf";
    petComp.equippedPet.instanceId = "inst_001";
    petComp.equippedPet.autoLootEnabled = false;

    CHECK(petComp.hasPet());
    CHECK_FALSE(petComp.equippedPet.autoLootEnabled);
}

TEST_CASE("PetAutoLoot: distance check with autoLootRadius") {
    float playerX = 100.0f, playerY = 100.0f;
    float itemX = 140.0f, itemY = 130.0f;  // ~50px away
    float autoLootRadius = 64.0f;

    float dx = playerX - itemX;
    float dy = playerY - itemY;
    float distSq = dx * dx + dy * dy;
    float radiusSq = autoLootRadius * autoLootRadius;

    CHECK(distSq < radiusSq);  // 2500 < 4096

    float farX = 200.0f, farY = 200.0f;  // ~141px away
    dx = playerX - farX;
    dy = playerY - farY;
    distSq = dx * dx + dy * dy;
    CHECK(distSq > radiusSq);  // 20000 > 4096
}
