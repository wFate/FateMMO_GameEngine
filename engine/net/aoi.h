#pragma once
#include "engine/ecs/entity_handle.h"
#include <cstdint>
#include <vector>
#include <algorithm>
#include <iterator>

namespace fate {

struct AOIConfig {
    // Phase 3a/3b — distance hysteresis re-enabled with wider gap.
    // 640 px = 20 tiles activation (must remain > MAX_MOB_AGGRO_PX = 512 so
    // mobs are visible before they aggro; pinned by tests/test_replication.cpp).
    // 1280 px = full Near-tier viewport (40 tiles); 2.0× hysteresis ratio.
    // The previous 640/768 (128 px gap) was too narrow for moving mobs at the
    // boundary and produced visible flicker (project_aoi_flickering_fix).
    float activationRadius = 640.0f;
    float deactivationRadius = 1280.0f;
    // Anti-flap floor: an entity that just entered AOI is held for at least
    // this many ticks before distance can release it. 10 ticks = 0.5 s at the
    // 20 Hz server tick. Forced-leave on despawn bypasses this floor.
    uint32_t minVisibleTicks = 10;
};

struct VisibilitySet {
    std::vector<EntityHandle> current;
    std::vector<EntityHandle> previous;
    std::vector<EntityHandle> entered;
    std::vector<EntityHandle> left;
    std::vector<EntityHandle> stayed;

    void computeDiff() {
        entered.clear(); left.clear(); stayed.clear();
        std::sort(current.begin(), current.end());
        std::sort(previous.begin(), previous.end());
        std::set_difference(current.begin(), current.end(), previous.begin(), previous.end(), std::back_inserter(entered));
        std::set_difference(previous.begin(), previous.end(), current.begin(), current.end(), std::back_inserter(left));
        std::set_intersection(current.begin(), current.end(), previous.begin(), previous.end(), std::back_inserter(stayed));
    }

    void advance() {
        previous = std::move(current);
        current.clear();
    }
};

} // namespace fate
