#pragma once
#include "engine/net/aoi.h"
#include "engine/core/types.h"
#include <algorithm>
#include <cmath>

namespace fate {

struct TargetValidator {
    static bool isInAOI(const VisibilitySet& aoi, uint64_t targetId) {
        EntityHandle target(static_cast<uint32_t>(targetId));
        return std::binary_search(aoi.current.begin(), aoi.current.end(), target);
    }

    static bool isInRange(Vec2 playerPos, Vec2 targetPos,
                          float maxRange, float latencyTolerance = 16.0f) {
        float dx = playerPos.x - targetPos.x;
        float dy = playerPos.y - targetPos.y;
        float distSq = dx * dx + dy * dy;
        float allowed = maxRange + latencyTolerance;
        return distSq <= allowed * allowed;
    }
};

} // namespace fate
