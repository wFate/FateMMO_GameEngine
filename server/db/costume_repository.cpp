#include "server/db/costume_repository.h"
#include "engine/core/logger.h"

namespace fate {

std::vector<OwnedCostume> CostumeRepository::loadOwnedCostumes(const std::string& characterId) {
    std::vector<OwnedCostume> result;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto rows = txn.exec_params(
            "SELECT costume_def_id FROM player_costumes WHERE character_id = $1",
            characterId
        );
        for (const auto& row : rows) {
            OwnedCostume c;
            c.costumeDefId = row["costume_def_id"].as<std::string>();
            result.push_back(std::move(c));
        }
        txn.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("CostumeRepo", "Failed to load owned costumes for %s: %s", characterId.c_str(), e.what());
    }
    return result;
}

std::vector<EquippedCostume> CostumeRepository::loadEquippedCostumes(const std::string& characterId) {
    std::vector<EquippedCostume> result;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto rows = txn.exec_params(
            "SELECT slot_type, costume_def_id FROM player_equipped_costumes WHERE character_id = $1",
            characterId
        );
        for (const auto& row : rows) {
            EquippedCostume c;
            c.slotType     = static_cast<uint8_t>(row["slot_type"].as<int>());
            c.costumeDefId = row["costume_def_id"].as<std::string>();
            result.push_back(std::move(c));
        }
        txn.commit();
    } catch (const std::exception& e) {
        LOG_ERROR("CostumeRepo", "Failed to load equipped costumes for %s: %s", characterId.c_str(), e.what());
    }
    return result;
}

bool CostumeRepository::loadToggleState(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto rows = txn.exec_params(
            "SELECT show_costumes FROM characters WHERE character_id = $1",
            characterId
        );
        txn.commit();
        if (!rows.empty()) return rows[0]["show_costumes"].as<bool>();
    } catch (const std::exception& e) {
        LOG_ERROR("CostumeRepo", "Failed to load toggle state for %s: %s", characterId.c_str(), e.what());
    }
    return true; // default on
}

bool CostumeRepository::grantCostume(const std::string& characterId, const std::string& costumeDefId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "INSERT INTO player_costumes (character_id, costume_def_id) "
            "VALUES ($1, $2) ON CONFLICT DO NOTHING",
            characterId, costumeDefId
        );
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("CostumeRepo", "Failed to grant costume %s for %s: %s", costumeDefId.c_str(), characterId.c_str(), e.what());
        return false;
    }
}

bool CostumeRepository::equipCostume(const std::string& characterId, uint8_t slotType,
                                      const std::string& costumeDefId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "INSERT INTO player_equipped_costumes (character_id, slot_type, costume_def_id) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (character_id, slot_type) DO UPDATE SET costume_def_id = $3",
            characterId, static_cast<int>(slotType), costumeDefId
        );
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("CostumeRepo", "Failed to equip costume slot %d for %s: %s", static_cast<int>(slotType), characterId.c_str(), e.what());
        return false;
    }
}

bool CostumeRepository::unequipCostume(const std::string& characterId, uint8_t slotType) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "DELETE FROM player_equipped_costumes WHERE character_id = $1 AND slot_type = $2",
            characterId, static_cast<int>(slotType)
        );
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("CostumeRepo", "Failed to unequip costume slot %d for %s: %s", static_cast<int>(slotType), characterId.c_str(), e.what());
        return false;
    }
}

bool CostumeRepository::saveToggleState(const std::string& characterId, bool show) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "UPDATE characters SET show_costumes = $2 WHERE character_id = $1",
            characterId, show
        );
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("CostumeRepo", "Failed to save toggle state for %s: %s", characterId.c_str(), e.what());
        return false;
    }
}

} // namespace fate
