#include "server/cache/item_definition_cache.h"
#include "engine/core/logger.h"
#include <pqxx/pqxx>

namespace fate {

void ItemDefinitionCache::initialize(pqxx::connection& conn) {
    definitions_.clear();
    try {
        pqxx::work txn(conn);
        auto result = txn.exec(
            "SELECT item_id, name, type, subtype, class_req, level_req, "
            "damage_min, damage_max, armor, attributes, description, "
            "gold_value, max_stack, icon_path, possible_stats, "
            "is_socketable, is_soulbound, rarity, max_enchant "
            "FROM item_definitions"
        );
        txn.commit();

        for (const auto& row : result) {
            CachedItemDefinition def;
            def.itemId      = row["item_id"].as<std::string>();
            def.displayName = row["name"].as<std::string>();
            def.itemType    = row["type"].as<std::string>();
            def.subtype     = row["subtype"].as<std::string>("");
            def.classReq    = row["class_req"].as<std::string>("All");
            def.levelReq    = row["level_req"].as<int>(1);
            def.damageMin   = row["damage_min"].as<int>(0);
            def.damageMax   = row["damage_max"].as<int>(0);
            def.armor       = row["armor"].as<int>(0);
            def.description = row["description"].as<std::string>("");
            def.goldValue   = row["gold_value"].as<int>(0);
            def.maxStack    = row["max_stack"].as<int>(1);
            def.iconPath    = row["icon_path"].as<std::string>("");
            def.isSocketable = row["is_socketable"].as<bool>(false);
            def.isSoulbound  = row["is_soulbound"].as<bool>(false);
            def.rarity       = row["rarity"].as<std::string>("Common");
            def.maxEnchant   = row["max_enchant"].as<int>(12);

            std::string possibleStatsJson = row["possible_stats"].as<std::string>("[]");
            def.possibleStats = parsePossibleStats(possibleStatsJson);

            std::string attrJson = row["attributes"].as<std::string>("{}");
            def.attributes = nlohmann::json::parse(attrJson, nullptr, false);
            if (def.attributes.is_discarded()) {
                def.attributes = nlohmann::json::object();
            }

            definitions_[def.itemId] = std::move(def);
        }
        LOG_INFO("ItemDefCache", "Loaded %zu item definitions", definitions_.size());
    } catch (const std::exception& e) {
        LOG_ERROR("ItemDefCache", "Failed to load item definitions: %s", e.what());
    }
}

std::vector<const CachedItemDefinition*> ItemDefinitionCache::getItemsByType(
    const std::string& itemType) const {
    std::vector<const CachedItemDefinition*> result;
    for (const auto& [id, def] : definitions_) {
        if (def.itemType == itemType) result.push_back(&def);
    }
    return result;
}

std::vector<PossibleStat> ItemDefinitionCache::parsePossibleStats(const std::string& json) {
    std::vector<PossibleStat> stats;
    auto parsed = nlohmann::json::parse(json, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) return stats;
    for (const auto& entry : parsed) {
        if (!entry.is_object()) continue;
        PossibleStat ps;
        // Handle both DB formats: {"stat":"hp"} and {"name":"hp"}
        if (entry.contains("stat"))
            ps.stat = entry["stat"].get<std::string>();
        else if (entry.contains("name"))
            ps.stat = entry["name"].get<std::string>();
        ps.min      = entry.value("min", 0);
        ps.max      = entry.value("max", 0);
        // Handle both: {"weighted": true} and {"weight": 1.0}
        if (entry.contains("weighted"))
            ps.weighted = entry["weighted"].get<bool>();
        else if (entry.contains("weight"))
            ps.weighted = entry["weight"].get<float>() > 0.0f;
        if (!ps.stat.empty()) stats.push_back(ps);
    }
    return stats;
}

} // namespace fate
