#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "game/shared/faction.h"

namespace fate {

struct FactionComponent {
    FATE_COMPONENT_COLD(FactionComponent)
    Faction faction = Faction::None;
};

} // namespace fate

FATE_REFLECT_EMPTY(fate::FactionComponent)
