#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <pqxx/pqxx>

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
    explicit PetRepository(pqxx::connection& conn) : conn_(conn) {}

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
    pqxx::connection& conn_;
};

} // namespace fate
