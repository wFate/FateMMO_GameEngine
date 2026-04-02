#pragma once
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include "game/shared/game_types.h"
#include "game/shared/item_stat_roller.h"
#include <nlohmann/json.hpp>

namespace pqxx { class connection; }

namespace fate {

struct CachedItemDefinition {
    std::string itemId;
    std::string displayName;
    std::string description;
    std::string itemType;   // Weapon, Armor, Consumable, etc.
    std::string subtype;    // Sword, Wand, Bow, Chest, Ring, etc.
    std::string rarity;     // Common, Uncommon, Rare, Epic, Legendary, Mythic
    int levelReq = 1;
    std::string classReq = "All";
    int damageMin = 0;
    int damageMax = 0;
    int armor = 0;
    int maxEnchant = 12;
    std::string visualStyle;    // from item_definitions.visual_style column
    bool isSocketable = false;
    bool isSoulbound = false;
    int maxStack = 1;
    int goldValue = 0;
    std::string iconPath;
    std::vector<PossibleStat> possibleStats;
    nlohmann::json attributes;

    bool isWeapon() const { return itemType == "Weapon"; }
    bool isArmor() const { return itemType == "Armor"; }
    bool isAccessory() const {
        return subtype == "Necklace" || subtype == "Ring" ||
               subtype == "Cloak" || subtype == "Belt";
    }
    bool isEquipment() const { return isWeapon() || isArmor() || isAccessory(); }
    bool hasPossibleStats() const { return !possibleStats.empty(); }

    int getIntAttribute(const std::string& key, int defaultVal = 0) const {
        auto it = attributes.find(key);
        if (it != attributes.end() && it->is_number_integer()) return it->get<int>();
        return defaultVal;
    }
    float getFloatAttribute(const std::string& key, float defaultVal = 0.0f) const {
        auto it = attributes.find(key);
        if (it != attributes.end() && it->is_number()) return it->get<float>();
        return defaultVal;
    }
    std::string getStringAttribute(const std::string& key, const std::string& defaultVal = "") const {
        auto it = attributes.find(key);
        if (it != attributes.end() && it->is_string()) return it->get<std::string>();
        return defaultVal;
    }
};

class ItemDefinitionCache {
public:
    void initialize(pqxx::connection& conn);
    const CachedItemDefinition* getDefinition(const std::string& itemId) const {
        auto it = definitions_.find(itemId);
        return it != definitions_.end() ? &it->second : nullptr;
    }
    std::optional<StatType> getStatTypeForScroll(const std::string& itemId) const {
        auto* def = getDefinition(itemId);
        if (!def) return std::nullopt;
        if (def->attributes.find("stat_type") == def->attributes.end()) return std::nullopt;
        return static_cast<StatType>(def->getIntAttribute("stat_type"));
    }
    std::vector<const CachedItemDefinition*> getItemsByType(const std::string& itemType) const;
    std::string getVisualStyle(const std::string& itemId) const {
        auto it = definitions_.find(itemId);
        return it != definitions_.end() ? it->second.visualStyle : "";
    }
    size_t size() const { return definitions_.size(); }
    const auto& allItems() const { return definitions_; }
    void reload(pqxx::connection& conn) { initialize(conn); }
private:
    std::unordered_map<std::string, CachedItemDefinition> definitions_;
    static std::vector<PossibleStat> parsePossibleStats(const std::string& json);
};

} // namespace fate
