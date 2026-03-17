#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "game/shared/pet_system.h"

namespace fate {

struct PetComponent {
    FATE_COMPONENT_COLD(PetComponent)

    PetInstance equippedPet;       // Empty instanceId = no pet equipped
    float autoLootRadius = 64.0f; // Pixels (2 tiles)

    [[nodiscard]] bool hasPet() const { return !equippedPet.instanceId.empty(); }
};

} // namespace fate

FATE_REFLECT_EMPTY(fate::PetComponent)
