#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "game/shared/game_types.h"

namespace fate {

// ============================================================================
// Rolled Stat — a single stat roll on an item
// ============================================================================
struct RolledStat {
    StatType statType{};
    int value = 0;
};

// ============================================================================
// Item Socket — a gem-like socket that boosts a primary stat
// ============================================================================
struct ItemSocket {
    StatType statType{};   // Only Strength, Dexterity, Intelligence are valid
    int value = 0;         // 1-10
    bool isEmpty = true;

    [[nodiscard]] bool isValid() const {
        if (isEmpty) return false;
        if (value < 1 || value > 10) return false;
        return statType == StatType::Strength
            || statType == StatType::Dexterity
            || statType == StatType::Intelligence;
    }
};

// ============================================================================
// Item Instance — a concrete instance of an item in the game world
// ============================================================================
struct ItemInstance {
    std::string instanceId;          // UUID
    std::string itemId;              // references item_definitions.item_id
    int quantity = 0;
    int enchantLevel = 0;
    std::vector<RolledStat> rolledStats;
    ItemSocket socket;
    bool isProtected = false;
    bool isSoulbound = false;
    std::string boundToCharacterId;
    int64_t acquiredAt = 0;

    // Stat enchant (accessories only — Belt, Ring, Necklace, Cloak)
    StatType statEnchantType  = StatType::Strength;
    int statEnchantValue      = 0;  // 0 = no enchant active

    // ---- Properties --------------------------------------------------------

    [[nodiscard]] bool isValid() const {
        return !itemId.empty() && quantity > 0;
    }

    [[nodiscard]] bool isBound() const {
        return isSoulbound || !boundToCharacterId.empty();
    }

    [[nodiscard]] bool canTrade() const {
        return isValid() && !isBound();
    }

    [[nodiscard]] bool hasRolledStats() const {
        return !rolledStats.empty();
    }

    [[nodiscard]] bool hasSocket() const {
        return socket.isValid();
    }

    [[nodiscard]] bool isMaxEnchant() const {
        return enchantLevel >= EnchantConstants::MAX_ENCHANT_LEVEL;
    }

    // ---- Methods -----------------------------------------------------------

    /// Returns the total value for a given stat, summing rolled stats and socket.
    [[nodiscard]] int getStatValue(StatType type) const {
        int total = 0;
        for (const auto& rs : rolledStats) {
            if (rs.statType == type) {
                total += rs.value;
            }
        }
        if (!socket.isEmpty && socket.isValid() && socket.statType == type) {
            total += socket.value;
        }
        return total;
    }

    /// Returns a default-constructed, invalid item instance.
    [[nodiscard]] static ItemInstance empty() {
        return {};
    }

    /// Creates a fully-specified item instance with rolled stats.
    [[nodiscard]] static ItemInstance create(
        std::string instanceId,
        std::string itemId,
        std::vector<RolledStat> stats,
        int qty = 1)
    {
        ItemInstance inst;
        inst.instanceId = std::move(instanceId);
        inst.itemId = std::move(itemId);
        inst.rolledStats = std::move(stats);
        inst.quantity = qty;
        return inst;
    }

    /// Creates a simple item with no rolled stats.
    [[nodiscard]] static ItemInstance createSimple(
        std::string instanceId,
        std::string itemId,
        int qty = 1)
    {
        return create(std::move(instanceId), std::move(itemId), {}, qty);
    }
};

} // namespace fate
