#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace fate {

struct CachedCostumeDef {
    std::string costumeDefId;
    std::string costumeName;
    std::string displayName;
    uint8_t     slotType    = 0;  // EquipmentSlot enum value
    uint16_t    visualIndex = 0;
    uint8_t     rarity      = 0;  // 0=Common..4=Legendary
    std::string source;           // "drop","shop","collection","craft","event"
};

} // namespace fate

// CostumeCache requires pqxx (server-only dependency)
#if __has_include(<pqxx/pqxx>)
#include <pqxx/pqxx>

namespace fate {

class CostumeCache {
public:
    bool loadFromDatabase(pqxx::connection& conn) {
        try {
            pqxx::work txn(conn);
            auto result = txn.exec(
                "SELECT costume_def_id, costume_name, display_name, "
                "slot_type, visual_index, rarity, source "
                "FROM costume_definitions"
            );
            for (const auto& row : result) {
                CachedCostumeDef def;
                def.costumeDefId = row["costume_def_id"].as<std::string>();
                def.costumeName  = row["costume_name"].as<std::string>();
                def.displayName  = row["display_name"].is_null() ? "" : row["display_name"].as<std::string>();
                def.slotType     = static_cast<uint8_t>(row["slot_type"].as<int>());
                def.visualIndex  = static_cast<uint16_t>(row["visual_index"].as<int>());
                def.rarity       = static_cast<uint8_t>(row["rarity"].as<int>());
                def.source       = row["source"].as<std::string>();
                costumes_[def.costumeDefId] = def;
            }
            txn.commit();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    const CachedCostumeDef* get(const std::string& costumeDefId) const {
        auto it = costumes_.find(costumeDefId);
        return it != costumes_.end() ? &it->second : nullptr;
    }

    std::vector<const CachedCostumeDef*> getBySlot(uint8_t slotType) const {
        std::vector<const CachedCostumeDef*> result;
        for (const auto& [id, def] : costumes_) {
            if (def.slotType == slotType) result.push_back(&def);
        }
        return result;
    }

    std::vector<const CachedCostumeDef*> getByRarity(uint8_t rarity) const {
        std::vector<const CachedCostumeDef*> result;
        for (const auto& [id, def] : costumes_) {
            if (def.rarity == rarity) result.push_back(&def);
        }
        return result;
    }

    const std::unordered_map<std::string, CachedCostumeDef>& all() const { return costumes_; }
    size_t size() const { return costumes_.size(); }
    void clear() { costumes_.clear(); }

private:
    std::unordered_map<std::string, CachedCostumeDef> costumes_;
};

} // namespace fate

#endif // __has_include(<pqxx/pqxx>)
