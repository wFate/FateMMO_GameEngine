#pragma once
#include "engine/ecs/archetype.h"
#include <vector>

namespace fate {

struct ArchetypeQuery {
    std::vector<CompId> required;
    std::vector<ArchetypeId> matchedArchetypes;
    uint64_t cachedVersion = 0;

    void refresh(ArchetypeStorage& storage) {
        if (cachedVersion == storage.version()) return;

        matchedArchetypes.clear();
        for (size_t i = 0; i < storage.archetypeCount(); ++i) {
            const auto& arch = storage.getArchetype(static_cast<ArchetypeId>(i));
            bool match = true;
            for (auto typeId : required) {
                if (!arch.hasType(typeId)) { match = false; break; }
            }
            if (match && arch.count > 0) matchedArchetypes.push_back(arch.id);
        }
        cachedVersion = storage.version();
    }
};

} // namespace fate
