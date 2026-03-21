#pragma once
#include "game/shared/pet_system.h"
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

private:
    std::unordered_map<std::string, PetDefinition> definitions_;
};

} // namespace fate
