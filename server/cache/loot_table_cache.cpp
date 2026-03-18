#include "server/cache/loot_table_cache.h"
#include "server/cache/item_definition_cache.h"
#include "game/shared/item_stat_roller.h"
#include "engine/core/logger.h"
#include <pqxx/pqxx>
#include <chrono>

namespace fate {

void LootTableCache::initialize(pqxx::connection& conn, const ItemDefinitionCache& itemDefs) {
    itemDefs_ = &itemDefs;
    tables_.clear();
    try {
        pqxx::work txn(conn);
        auto result = txn.exec(
            "SELECT loot_table_id, item_id, drop_chance, min_quantity, max_quantity "
            "FROM loot_drops ORDER BY loot_table_id"
        );
        txn.commit();
        for (const auto& row : result) {
            LootDropEntry entry;
            std::string tableId = row["loot_table_id"].as<std::string>();
            entry.itemId       = row["item_id"].as<std::string>();
            entry.dropChance   = row["drop_chance"].as<float>(0.0f);
            entry.minQuantity  = row["min_quantity"].as<int>(1);
            entry.maxQuantity  = row["max_quantity"].as<int>(1);
            tables_[tableId].push_back(entry);
        }
        LOG_INFO("LootTableCache", "Loaded %zu loot tables", tables_.size());
    } catch (const std::exception& e) {
        LOG_ERROR("LootTableCache", "Failed to load loot tables: %s", e.what());
    }
}

std::vector<LootDropResult> LootTableCache::rollLoot(const std::string& lootTableId) const {
    std::vector<LootDropResult> results;
    auto it = tables_.find(lootTableId);
    if (it == tables_.end()) return results;

    std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);

    for (const auto& entry : it->second) {
        float roll = chanceDist(rng());
        if (roll > entry.dropChance) continue;

        const CachedItemDefinition* def = itemDefs_ ? itemDefs_->getDefinition(entry.itemId) : nullptr;
        if (!def) {
            LOG_WARN("LootTableCache", "Unknown item_id '%s' in loot table '%s'",
                     entry.itemId.c_str(), lootTableId.c_str());
            continue;
        }

        int qty = entry.minQuantity;
        if (entry.maxQuantity > entry.minQuantity) {
            std::uniform_int_distribution<int> qtyDist(entry.minQuantity, entry.maxQuantity);
            qty = qtyDist(rng());
        }

        ItemInstance item;
        item.instanceId = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
            "_" + std::to_string(reinterpret_cast<uintptr_t>(&entry));
        item.itemId = entry.itemId;
        item.quantity = qty;

        if (def->hasPossibleStats()) {
            item.rolledStats = ItemStatRoller::rollStats(def->possibleStats);
        }

        item.enchantLevel = rollEnchantLevel(def->subtype);

        if (def->isSocketable && def->isAccessory()) {
            StatType socketTypes[] = {StatType::Strength, StatType::Dexterity, StatType::Intelligence};
            std::uniform_int_distribution<int> socketDist(0, 2);
            item.socket = ItemStatRoller::rollSocket(socketTypes[socketDist(rng())]);
        }

        item.isSoulbound = def->isSoulbound;

        LootDropResult result;
        result.item = std::move(item);
        result.itemName = def->displayName;
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace fate
