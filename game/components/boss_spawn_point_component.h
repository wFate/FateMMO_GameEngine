#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/entity_handle.h"
#include "engine/core/types.h"
#include <string>
#include <vector>

namespace fate {

struct BossSpawnPointComponent {
    FATE_COMPONENT_COLD(BossSpawnPointComponent)

    std::string bossDefId;
    std::vector<Vec2> spawnCoordinates;
    int levelOverride = 0;  // 0 = use DB range

    // Tracked state
    EntityHandle bossEntityHandle;
    bool bossAlive = false;
    float respawnAt = 0.0f;
    int lastSpawnIndex = -1;
    bool initialized = false;
};

} // namespace fate
