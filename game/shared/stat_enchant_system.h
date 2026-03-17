#pragma once
#include <array>
#include <random>
#include "game/shared/game_types.h"
#include "game/shared/item_instance.h"
#include "game/shared/character_stats.h"

namespace fate {

class StatEnchantSystem {
public:
    StatEnchantSystem() = delete;

    static bool canStatEnchant(EquipmentSlot slot) {
        switch (slot) {
            case EquipmentSlot::Belt:
            case EquipmentSlot::Ring:
            case EquipmentSlot::Necklace:
            case EquipmentSlot::Cloak:
                return true;
            default:
                return false;
        }
    }

    // Probability table: tier 0=25%, 1=30%, 2=25%, 3=12%, 4=6%, 5=2%
    // Cumulative: 25, 55, 80, 92, 98, 100
    static int rollStatEnchant() {
        thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> dist(1, 100);
        int roll = dist(rng);

        if (roll <= 25) return 0;
        if (roll <= 55) return 1;
        if (roll <= 80) return 2;
        if (roll <= 92) return 3;
        if (roll <= 98) return 4;
        return 5;
    }

    static int getStatValue(StatType scrollType, int tier) {
        if (tier <= 0) return 0;
        if (scrollType == StatType::MaxHealth || scrollType == StatType::MaxMana) {
            return tier * 10;
        }
        return tier;
    }

    /// Apply an item's stat enchant bonus to CharacterStats equipBonus fields.
    /// Call for each equipped accessory, after equipment bonuses, before recalculateStats().
    static void applyToEquipBonuses(const ItemInstance& item, CharacterStats& stats) {
        if (item.statEnchantValue <= 0) return;
        switch (item.statEnchantType) {
            case StatType::Strength:     stats.equipBonusSTR += item.statEnchantValue; break;
            case StatType::Intelligence: stats.equipBonusINT += item.statEnchantValue; break;
            case StatType::Dexterity:    stats.equipBonusDEX += item.statEnchantValue; break;
            case StatType::Vitality:     stats.equipBonusVIT += item.statEnchantValue; break;
            case StatType::Wisdom:       stats.equipBonusWIS += item.statEnchantValue; break;
            case StatType::MaxHealth:    stats.equipBonusHP  += item.statEnchantValue; break;
            case StatType::MaxMana:      stats.equipBonusMP  += item.statEnchantValue; break;
            default: break;
        }
    }

    static void applyStatEnchant(ItemInstance& item, StatType type, int tier) {
        int value = getStatValue(type, tier);
        if (value <= 0) {
            // Fail — remove enchant entirely
            item.statEnchantType = StatType::Strength;  // reset to neutral default
            item.statEnchantValue = 0;
        } else {
            item.statEnchantType = type;
            item.statEnchantValue = value;
        }
    }
};

} // namespace fate
