#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include "game/shared/game_types.h"

namespace fate {

struct PetDefinition {
    std::string petId;
    std::string displayName;
    ItemRarity rarity = ItemRarity::Common;

    int baseHP           = 0;
    float baseCritRate   = 0.0f;
    float baseExpBonus   = 0.0f;

    float hpPerLevel       = 0.0f;
    float critPerLevel     = 0.0f;
    float expBonusPerLevel = 0.0f;
};

struct PetInstance {
    std::string instanceId;
    std::string petDefinitionId;
    std::string petName;
    int level            = 1;
    int64_t currentXP    = 0;
    int64_t xpToNextLevel = 100;
    bool autoLootEnabled = false;
    bool isSoulbound     = false;
};

class PetSystem {
public:
    PetSystem() = delete;

    static constexpr int MAX_PET_LEVEL = 50;
    static constexpr float PET_XP_SHARE = 0.5f;

    static int effectiveHP(const PetDefinition& def, const PetInstance& pet) {
        return def.baseHP + static_cast<int>(std::round(def.hpPerLevel * (pet.level - 1)));
    }

    static float effectiveCritRate(const PetDefinition& def, const PetInstance& pet) {
        return def.baseCritRate + def.critPerLevel * (pet.level - 1);
    }

    static float effectiveExpBonus(const PetDefinition& def, const PetInstance& pet) {
        return def.baseExpBonus + def.expBonusPerLevel * (pet.level - 1);
    }

    /// Apply pet stat bonuses to CharacterStats equipBonus fields (additive).
    /// Call this after equipment bonuses are set, before recalculateStats().
    static void applyToEquipBonuses(const PetDefinition& def, const PetInstance& pet,
                                     int& outBonusHP, float& outBonusCritRate) {
        outBonusHP += effectiveHP(def, pet);
        outBonusCritRate += effectiveCritRate(def, pet);
    }

    static void addXP(const PetDefinition& def, PetInstance& pet, int64_t amount, int playerLevel);
    static int64_t calculateXPToNextLevel(int petLevel);
    static PetInstance createInstance(const PetDefinition& def, const std::string& instanceId);
};

} // namespace fate
