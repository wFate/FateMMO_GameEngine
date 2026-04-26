#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include <string>

namespace fate {

struct TileLayerComponent {
    FATE_COMPONENT(TileLayerComponent)
    std::string layer; // "ground", "detail", "fringe", "collision"
};

} // namespace fate

FATE_REFLECT(fate::TileLayerComponent,
    FATE_FIELD(layer, String)
)
