#include "server/db/inventory_repository.h"
#include "engine/core/logger.h"

namespace fate {

std::vector<InventorySlotRecord> InventoryRepository::loadInventory(const std::string& characterId) {
    std::vector<InventorySlotRecord> slots;
    try {
        pqxx::work txn(conn_);
        auto result = txn.exec_params(
            "SELECT instance_id, character_id, item_id, "
            "slot_index, bag_slot_index, bag_item_slot, "
            "rolled_stats, socket_stat, socket_value, "
            "enchant_level, is_protected, is_soulbound, is_broken, "
            "is_equipped, equipped_slot, quantity "
            "FROM character_inventory WHERE character_id = $1 "
            "ORDER BY slot_index",
            characterId);
        txn.commit();

        slots.reserve(result.size());
        for (const auto& row : result) {
            InventorySlotRecord rec;
            rec.instance_id    = row["instance_id"].as<std::string>();
            rec.character_id   = row["character_id"].as<std::string>();
            rec.item_id        = row["item_id"].as<std::string>();
            rec.slot_index     = row["slot_index"].is_null()     ? -1 : row["slot_index"].as<int>();
            rec.bag_slot_index = row["bag_slot_index"].is_null() ? -1 : row["bag_slot_index"].as<int>();
            rec.bag_item_slot  = row["bag_item_slot"].is_null()  ? -1 : row["bag_item_slot"].as<int>();
            rec.rolled_stats   = row["rolled_stats"].as<std::string>("{}");
            rec.socket_stat    = row["socket_stat"].is_null()    ? "" : row["socket_stat"].as<std::string>();
            rec.socket_value   = row["socket_value"].is_null()   ?  0 : row["socket_value"].as<int>();
            rec.enchant_level  = row["enchant_level"].is_null()  ?  0 : row["enchant_level"].as<int>();
            rec.is_protected   = row["is_protected"].as<bool>();
            rec.is_soulbound   = row["is_soulbound"].as<bool>();
            rec.is_broken      = row["is_broken"].as<bool>(false);
            rec.is_equipped    = row["is_equipped"].as<bool>();
            rec.equipped_slot  = row["equipped_slot"].is_null()  ? "" : row["equipped_slot"].as<std::string>();
            rec.quantity       = row["quantity"].is_null()        ?  1 : row["quantity"].as<int>();
            slots.push_back(std::move(rec));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("InvRepo", "loadInventory failed for %s: %s", characterId.c_str(), e.what());
    }
    return slots;
}

bool InventoryRepository::saveInventory(const std::string& characterId,
                                         const std::vector<InventorySlotRecord>& slots) {
    try {
        pqxx::work txn(conn_);

        // Delete existing inventory for this character
        txn.exec_params("DELETE FROM character_inventory WHERE character_id = $1", characterId);

        // Insert each slot
        for (const auto& s : slots) {
            // Nullable int columns: -1 means NULL
            std::optional<int> slotIdx      = s.slot_index     >= 0 ? std::optional<int>(s.slot_index)     : std::nullopt;
            std::optional<int> bagSlotIdx   = s.bag_slot_index >= 0 ? std::optional<int>(s.bag_slot_index) : std::nullopt;
            std::optional<int> bagItemSlot  = s.bag_item_slot  >= 0 ? std::optional<int>(s.bag_item_slot)  : std::nullopt;
            std::optional<int> sockVal      = s.socket_value   != 0 ? std::optional<int>(s.socket_value)   : std::nullopt;

            // Nullable string columns: empty means NULL
            std::optional<std::string> sockStat = s.socket_stat.empty()   ? std::nullopt : std::optional<std::string>(s.socket_stat);
            std::optional<std::string> eqSlot   = s.equipped_slot.empty() ? std::nullopt : std::optional<std::string>(s.equipped_slot);

            txn.exec_params(
                "INSERT INTO character_inventory "
                "(character_id, item_id, slot_index, bag_slot_index, bag_item_slot, "
                "rolled_stats, socket_stat, socket_value, enchant_level, "
                "is_protected, is_soulbound, is_broken, is_equipped, equipped_slot, quantity) "
                "VALUES ($1, $2, $3, $4, $5, $6::jsonb, $7, $8, $9, $10, $11, $12, $13, $14, $15)",
                characterId, s.item_id,
                slotIdx, bagSlotIdx, bagItemSlot,
                s.rolled_stats.empty() ? "{}" : s.rolled_stats,
                sockStat, sockVal, s.enchant_level,
                s.is_protected, s.is_soulbound, s.is_broken, s.is_equipped, eqSlot, s.quantity);
        }

        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("InvRepo", "saveInventory failed for %s: %s", characterId.c_str(), e.what());
    }
    return false;
}

} // namespace fate
