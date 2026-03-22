#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <pqxx/pqxx>
#include "server/db/db_pool.h"

namespace fate {

struct PetRecord {
    int id = 0;
    std::string characterId;
    std::string petDefId;
    std::string petName;
    int level = 1;
    int64_t currentXP = 0;
    bool isEquipped = false;
    bool isSoulbound = true;
    bool autoLootEnabled = false;
};

class PetRepository {
public:
    // Legacy: direct connection (for temp repos in async fibers)
    explicit PetRepository(pqxx::connection& conn) : connRef_(&conn), pool_(nullptr) {}
    // Pool-based: acquires connection per operation
    explicit PetRepository(DbPool& pool) : connRef_(nullptr), pool_(&pool) {}

    // Load all pets for a character
    std::vector<PetRecord> loadPets(const std::string& characterId);

    // Load equipped pet (if any)
    std::optional<PetRecord> loadEquippedPet(const std::string& characterId);

    // Save pet state (upsert)
    bool savePet(const PetRecord& pet);

    // Save all pets in batch
    bool saveAllPets(const std::string& characterId, const std::vector<PetRecord>& pets);

    // Equip/unequip
    bool equipPet(const std::string& characterId, int petId);
    bool unequipAllPets(const std::string& characterId);

    // Add XP to equipped pet
    bool addPetXP(const std::string& characterId, int petId, int64_t xp);

private:
    pqxx::connection* connRef_ = nullptr;
    DbPool* pool_ = nullptr;

    DbPool::Guard acquireConn() {
        if (pool_) return pool_->acquire_guard();
        return DbPool::Guard::wrap(*connRef_);
    }
};

} // namespace fate
