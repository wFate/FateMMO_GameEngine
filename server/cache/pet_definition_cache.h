#pragma once
#include "game/shared/pet_system.h"
#include "game/shared/game_types.h"
#include "engine/core/logger.h"
#include <pqxx/pqxx>
#include <string>
#include <unordered_map>

namespace fate {

class PetDefinitionCache {
public:
    void addDefinition(const PetDefinition& def) {
        definitions_[def.petId] = def;
    }

    const PetDefinition* getDefinition(const std::string& petDefId) const {
        auto it = definitions_.find(petDefId);
        return it != definitions_.end() ? &it->second : nullptr;
    }

    size_t size() const { return definitions_.size(); }

    bool initialize(pqxx::connection& conn) {
        try {
            pqxx::work txn(conn);
            auto rows = txn.exec(
                "SELECT pet_id, display_name, rarity, base_hp, base_crit_rate, "
                "base_exp_bonus, hp_per_level, crit_per_level, exp_bonus_per_level "
                "FROM pet_definitions"
            );
            txn.commit();

            for (const auto& row : rows) {
                PetDefinition def;
                def.petId       = row["pet_id"].as<std::string>();
                def.displayName = row["display_name"].as<std::string>();
                def.rarity      = parseItemRarity(row["rarity"].as<std::string>("Common"));
                def.baseHP      = row["base_hp"].as<int>(10);
                def.baseCritRate    = row["base_crit_rate"].as<float>(0.01f);
                def.baseExpBonus    = row["base_exp_bonus"].as<float>(0.0f);
                def.hpPerLevel      = row["hp_per_level"].as<float>(2.0f);
                def.critPerLevel    = row["crit_per_level"].as<float>(0.002f);
                def.expBonusPerLevel = row["exp_bonus_per_level"].as<float>(0.0f);
                definitions_[def.petId] = def;
            }

            LOG_INFO("PetDefCache", "Loaded %zu pet definitions from DB", definitions_.size());
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("PetDefCache", "Failed to load pet definitions: %s", e.what());
            return false;
        }
    }

private:
    std::unordered_map<std::string, PetDefinition> definitions_;
};

} // namespace fate
