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

struct MobCostumeDrop {
    std::string costumeDefId;
    float dropChance = 0.01f;
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

    bool loadMobDrops(pqxx::connection& conn) {
        try {
            pqxx::work txn(conn);
            auto result = txn.exec(
                "SELECT mob_def_id, costume_def_id, drop_chance "
                "FROM mob_costume_drops"
            );
            for (const auto& row : result) {
                MobCostumeDrop drop;
                drop.costumeDefId = row["costume_def_id"].as<std::string>();
                drop.dropChance   = row["drop_chance"].as<float>();
                mobDrops_[row["mob_def_id"].as<std::string>()].push_back(drop);
            }
            txn.commit();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    const std::vector<MobCostumeDrop>& getMobDrops(const std::string& mobDefId) const {
        static const std::vector<MobCostumeDrop> empty;
        auto it = mobDrops_.find(mobDefId);
        return it != mobDrops_.end() ? it->second : empty;
    }

    const std::unordered_map<std::string, CachedCostumeDef>& all() const { return costumes_; }
    size_t size() const { return costumes_.size(); }
    void reload(pqxx::connection& conn) { clear(); loadFromDatabase(conn); loadMobDrops(conn); }
    void clear() { costumes_.clear(); mobDrops_.clear(); }

private:
    std::unordered_map<std::string, CachedCostumeDef> costumes_;
    std::unordered_map<std::string, std::vector<MobCostumeDrop>> mobDrops_;
};

} // namespace fate

#else

namespace fate {

class CostumeCache {
public:
    const CachedCostumeDef* get(const std::string&) const { return nullptr; }
    size_t size() const { return 0; }
};

} // namespace fate

#endif // __has_include(<pqxx/pqxx>)
