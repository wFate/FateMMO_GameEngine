#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/render/lighting.h"

namespace fate {

struct PointLightComponent {
    FATE_COMPONENT(PointLightComponent)

    PointLight light;
};

} // namespace fate

FATE_REFLECT_EMPTY(fate::PointLightComponent)
