#include "game/shared/pet_system.h"
#include <algorithm>
#include <cmath>

namespace fate {

void PetSystem::addXP(const PetDefinition& /*def*/, PetInstance& pet, int64_t amount, int playerLevel) {
    if (amount <= 0) return;

    int levelCap = (std::min)(playerLevel, MAX_PET_LEVEL);
    if (pet.level >= levelCap) return;

    pet.currentXP += amount;

    while (pet.currentXP >= pet.xpToNextLevel && pet.level < levelCap) {
        pet.currentXP -= pet.xpToNextLevel;
        pet.level++;
        pet.xpToNextLevel = calculateXPToNextLevel(pet.level);
    }

    if (pet.level >= levelCap) {
        pet.currentXP = 0;
    }
}

int64_t PetSystem::calculateXPToNextLevel(int petLevel) {
    int64_t val = static_cast<int64_t>(std::round(50.0 * petLevel * petLevel));
    return (std::max)(val, int64_t{100});
}

PetInstance PetSystem::createInstance(const PetDefinition& def, const std::string& instanceId) {
    PetInstance pet;
    pet.instanceId = instanceId;
    pet.petDefinitionId = def.petId;
    pet.petName = def.displayName;
    pet.level = 1;
    pet.currentXP = 0;
    pet.xpToNextLevel = calculateXPToNextLevel(1);
    pet.autoLootEnabled = false;
    pet.isSoulbound = false;
    return pet;
}

} // namespace fate
