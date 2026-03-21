#pragma once
#include "engine/ecs/component_registry.h"
#include <string>
#include <cstdint>

namespace fate {

struct DroppedItemComponent {
    FATE_COMPONENT_COLD(DroppedItemComponent)

    std::string itemId;
    int quantity = 1;
    int enchantLevel = 0;
    std::string rolledStatsJson;
    std::string rarity;

    bool isGold = false;
    int goldAmount = 0;

    uint32_t ownerEntityId = 0;  // 0 = free for all
    float spawnTime = 0.0f;
    float despawnAfter = 120.0f; // 2 minutes
    std::string sceneId;         // scene where this item was dropped

    uint32_t claimedBy = 0;  // 0 = unclaimed

    bool tryClaim(uint32_t claimantEntityId) {
        if (claimedBy != 0) return false;
        claimedBy = claimantEntityId;
        return true;
    }

    void releaseClaim() { claimedBy = 0; }
};

} // namespace fate
