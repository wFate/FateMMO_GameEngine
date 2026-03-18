#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <random>
#include "game/shared/item_instance.h"

namespace pqxx { class connection; }

namespace fate {

class ItemDefinitionCache;

struct LootDropEntry {
    std::string itemId;
    float dropChance = 0.0f;  // 0.0–1.0
    int minQuantity = 1;
    int maxQuantity = 1;
};

struct LootDropResult {
    ItemInstance item;
    std::string itemName;  // for logging
};

class LootTableCache {
public:
    void initialize(pqxx::connection& conn, const ItemDefinitionCache& itemDefs);
    std::vector<LootDropResult> rollLoot(const std::string& lootTableId) const;
    size_t tableCount() const { return tables_.size(); }

    // Public + inline for testing (test target doesn't link server sources)
    static int rollEnchantLevel(const std::string& subtype) {
        if (enchantableSubtypes().count(subtype) == 0) return 0;
        // Weighted: +0=40%, +1=25%, +2=15%, +3=10%, +4=5%, +5=3%, +6=1.5%, +7=0.5%
        std::uniform_real_distribution<float> dist(0.0f, 100.0f);
        float roll = dist(rng());
        if (roll < 40.0f) return 0;
        if (roll < 65.0f) return 1;
        if (roll < 80.0f) return 2;
        if (roll < 90.0f) return 3;
        if (roll < 95.0f) return 4;
        if (roll < 98.0f) return 5;
        if (roll < 99.5f) return 6;
        return 7;
    }

private:
    const ItemDefinitionCache* itemDefs_ = nullptr;
    std::unordered_map<std::string, std::vector<LootDropEntry>> tables_;

    static std::mt19937& rng() {
        thread_local std::mt19937 gen{std::random_device{}()};
        return gen;
    }

    static const std::unordered_set<std::string>& enchantableSubtypes() {
        static const std::unordered_set<std::string> s = {
            "Sword", "Wand", "Bow", "Shield",
            "Head", "Armor", "Gloves", "Boots", "Feet"
        };
        return s;
    }
};

} // namespace fate
