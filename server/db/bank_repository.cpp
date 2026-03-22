#include "server/db/bank_repository.h"
#include "engine/core/logger.h"

namespace fate {

std::vector<BankSlotRecord> BankRepository::loadBankItems(const std::string& characterId) {
    std::vector<BankSlotRecord> items;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT slot_index, item_id, quantity, rolled_stats::text, "
            "socket_stat, socket_value, enchant_level, is_protected, instance_id::text "
            "FROM character_bank WHERE character_id = $1 ORDER BY slot_index",
            characterId);
        txn.commit();
        items.reserve(result.size());
        for (const auto& row : result) {
            BankSlotRecord r;
            r.slotIndex     = row["slot_index"].as<int>();
            r.itemId        = row["item_id"].as<std::string>();
            r.quantity       = row["quantity"].as<int>();
            r.rolledStatsJson = row["rolled_stats"].as<std::string>("{}");
            r.socketStat    = row["socket_stat"].is_null() ? "" : row["socket_stat"].as<std::string>();
            r.socketValue   = row["socket_value"].is_null() ? 0 : row["socket_value"].as<int>();
            r.enchantLevel  = row["enchant_level"].is_null() ? 0 : row["enchant_level"].as<int>();
            r.isProtected   = row["is_protected"].as<bool>();
            r.instanceId    = row["instance_id"].as<std::string>();
            items.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("BankRepo", "loadBankItems failed for %s: %s", characterId.c_str(), e.what());
    }
    return items;
}

bool BankRepository::depositItem(const std::string& characterId, int slotIndex,
                                  const std::string& itemId, int quantity,
                                  const std::string& rolledStats, const std::string& socketStat,
                                  int socketValue, int enchantLevel, bool isProtected) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "INSERT INTO character_bank (character_id, slot_index, item_id, quantity, "
            "rolled_stats, socket_stat, socket_value, enchant_level, is_protected) "
            "VALUES ($1, $2, $3, $4, $5::jsonb, $6, $7, $8, $9) "
            "ON CONFLICT (character_id, slot_index) DO UPDATE SET "
            "item_id = $3, quantity = $4, rolled_stats = $5::jsonb, "
            "socket_stat = $6, socket_value = $7, enchant_level = $8, is_protected = $9",
            characterId, slotIndex, itemId, quantity, rolledStats,
            socketStat.empty() ? std::optional<std::string>(std::nullopt) : std::optional(socketStat),
            socketValue != 0 ? std::optional(socketValue) : std::optional<int>(std::nullopt),
            enchantLevel, isProtected);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("BankRepo", "depositItem failed: %s", e.what());
    }
    return false;
}

bool BankRepository::withdrawItem(const std::string& characterId, int slotIndex) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "DELETE FROM character_bank WHERE character_id = $1 AND slot_index = $2",
            characterId, slotIndex);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("BankRepo", "withdrawItem failed: %s", e.what());
    }
    return false;
}

bool BankRepository::saveBankItems(const std::string& characterId,
                                    const std::vector<BankSlotRecord>& items) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        // Clear and re-insert (same pattern as inventory)
        txn.exec_params("DELETE FROM character_bank WHERE character_id = $1", characterId);
        for (const auto& r : items) {
            txn.exec_params(
                "INSERT INTO character_bank (character_id, slot_index, item_id, quantity, "
                "rolled_stats, socket_stat, socket_value, enchant_level, is_protected) "
                "VALUES ($1, $2, $3, $4, $5::jsonb, $6, $7, $8, $9)",
                characterId, r.slotIndex, r.itemId, r.quantity, r.rolledStatsJson,
                r.socketStat.empty() ? std::optional<std::string>(std::nullopt) : std::optional(r.socketStat),
                r.socketValue != 0 ? std::optional(r.socketValue) : std::optional<int>(std::nullopt),
                r.enchantLevel, r.isProtected);
        }
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("BankRepo", "saveBankItems failed for %s: %s", characterId.c_str(), e.what());
    }
    return false;
}

int64_t BankRepository::loadBankGold(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT stored_gold FROM character_bank_gold WHERE character_id = $1",
            characterId);
        txn.commit();
        if (!result.empty()) return result[0][0].as<int64_t>();
    } catch (const std::exception& e) {
        LOG_ERROR("BankRepo", "loadBankGold failed for %s: %s", characterId.c_str(), e.what());
    }
    return 0;
}

bool BankRepository::depositGold(const std::string& characterId, int64_t amount) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "INSERT INTO character_bank_gold (character_id, stored_gold) "
            "VALUES ($1, $2) "
            "ON CONFLICT (character_id) DO UPDATE SET stored_gold = character_bank_gold.stored_gold + $2",
            characterId, amount);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("BankRepo", "depositGold failed: %s", e.what());
    }
    return false;
}

bool BankRepository::withdrawGold(const std::string& characterId, int64_t amount) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "UPDATE character_bank_gold SET stored_gold = stored_gold - $2 "
            "WHERE character_id = $1 AND stored_gold >= $2",
            characterId, amount);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("BankRepo", "withdrawGold failed: %s", e.what());
    }
    return false;
}

} // namespace fate
