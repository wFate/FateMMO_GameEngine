#include "server/db/pet_repository.h"
#include "engine/core/logger.h"

namespace fate {

std::vector<PetRecord> PetRepository::loadPets(const std::string& characterId) {
    std::vector<PetRecord> pets;
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT id, character_id, pet_def_id, pet_name, level, current_xp, "
            "is_equipped, is_soulbound, auto_loot_enabled "
            "FROM character_pets WHERE character_id = $1",
            characterId);
        txn.commit();
        pets.reserve(result.size());
        for (const auto& row : result) {
            PetRecord r;
            r.id              = row["id"].as<int>();
            r.characterId     = row["character_id"].as<std::string>();
            r.petDefId        = row["pet_def_id"].as<std::string>();
            r.petName         = row["pet_name"].as<std::string>();
            r.level           = row["level"].as<int>();
            r.currentXP       = row["current_xp"].as<int64_t>();
            r.isEquipped      = row["is_equipped"].as<bool>();
            r.isSoulbound     = row["is_soulbound"].as<bool>();
            r.autoLootEnabled = row["auto_loot_enabled"].as<bool>();
            pets.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("PetRepo", "loadPets failed for %s: %s", characterId.c_str(), e.what());
    }
    return pets;
}

std::optional<PetRecord> PetRepository::loadEquippedPet(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        auto result = txn.exec_params(
            "SELECT id, character_id, pet_def_id, pet_name, level, current_xp, "
            "is_equipped, is_soulbound, auto_loot_enabled "
            "FROM character_pets WHERE character_id = $1 AND is_equipped = TRUE LIMIT 1",
            characterId);
        txn.commit();
        if (result.empty()) return std::nullopt;

        const auto& row = result[0];
        PetRecord r;
        r.id              = row["id"].as<int>();
        r.characterId     = row["character_id"].as<std::string>();
        r.petDefId        = row["pet_def_id"].as<std::string>();
        r.petName         = row["pet_name"].as<std::string>();
        r.level           = row["level"].as<int>();
        r.currentXP       = row["current_xp"].as<int64_t>();
        r.isEquipped      = true;
        r.isSoulbound     = row["is_soulbound"].as<bool>();
        r.autoLootEnabled = row["auto_loot_enabled"].as<bool>();
        return r;
    } catch (const std::exception& e) {
        LOG_ERROR("PetRepo", "loadEquippedPet failed for %s: %s", characterId.c_str(), e.what());
    }
    return std::nullopt;
}

bool PetRepository::savePet(const PetRecord& pet) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        if (pet.id > 0) {
            txn.exec_params(
                "UPDATE character_pets SET pet_name = $2, level = $3, current_xp = $4, "
                "is_equipped = $5, auto_loot_enabled = $6 WHERE id = $1",
                pet.id, pet.petName, pet.level, pet.currentXP,
                pet.isEquipped, pet.autoLootEnabled);
        } else {
            txn.exec_params(
                "INSERT INTO character_pets (character_id, pet_def_id, pet_name, level, "
                "current_xp, is_equipped, is_soulbound, auto_loot_enabled) "
                "VALUES ($1, $2, $3, $4, $5, $6, $7, $8)",
                pet.characterId, pet.petDefId, pet.petName, pet.level,
                pet.currentXP, pet.isEquipped, pet.isSoulbound, pet.autoLootEnabled);
        }
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("PetRepo", "savePet failed: %s", e.what());
    }
    return false;
}

bool PetRepository::saveAllPets(const std::string& characterId, const std::vector<PetRecord>& pets) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        for (const auto& pet : pets) {
            if (pet.id > 0) {
                txn.exec_params(
                    "UPDATE character_pets SET pet_name = $2, level = $3, current_xp = $4, "
                    "is_equipped = $5, auto_loot_enabled = $6 WHERE id = $1",
                    pet.id, pet.petName, pet.level, pet.currentXP,
                    pet.isEquipped, pet.autoLootEnabled);
            }
        }
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("PetRepo", "saveAllPets failed for %s: %s", characterId.c_str(), e.what());
    }
    return false;
}

bool PetRepository::equipPet(const std::string& characterId, int petId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        // Unequip all first
        txn.exec_params(
            "UPDATE character_pets SET is_equipped = FALSE WHERE character_id = $1",
            characterId);
        // Equip selected
        txn.exec_params(
            "UPDATE character_pets SET is_equipped = TRUE WHERE id = $1 AND character_id = $2",
            petId, characterId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("PetRepo", "equipPet failed: %s", e.what());
    }
    return false;
}

bool PetRepository::unequipAllPets(const std::string& characterId) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "UPDATE character_pets SET is_equipped = FALSE WHERE character_id = $1",
            characterId);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("PetRepo", "unequipAllPets failed: %s", e.what());
    }
    return false;
}

bool PetRepository::addPetXP(const std::string& characterId, int petId, int64_t xp) {
    try {
        auto guard = acquireConn();
        pqxx::work txn(guard.connection());
        txn.exec_params(
            "UPDATE character_pets SET current_xp = current_xp + $3 "
            "WHERE id = $1 AND character_id = $2",
            petId, characterId, xp);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("PetRepo", "addPetXP failed: %s", e.what());
    }
    return false;
}

} // namespace fate
