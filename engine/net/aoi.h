#pragma once
#include "engine/ecs/entity_handle.h"
#include <vector>
#include <algorithm>

namespace fate {

struct AOIConfig {
    float activationRadius = 320.0f;   // 10 tiles * 32px
    float deactivationRadius = 384.0f; // 20% larger for hysteresis
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
