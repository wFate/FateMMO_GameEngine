#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"

namespace fate {

struct BossSpawnPointComponent {
    FATE_COMPONENT_COLD(BossSpawnPointComponent)
};

} // namespace fate
