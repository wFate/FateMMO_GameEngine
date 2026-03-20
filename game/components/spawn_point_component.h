#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"

namespace fate {

struct SpawnPointComponent {
    FATE_COMPONENT_COLD(SpawnPointComponent)
    bool isTownSpawn = false;
};

} // namespace fate

FATE_REFLECT(fate::SpawnPointComponent,
    FATE_FIELD(isTownSpawn, Bool)
)
