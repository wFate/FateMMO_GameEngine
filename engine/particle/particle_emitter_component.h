#pragma once
#include "engine/ecs/component_registry.h"
#include "engine/ecs/reflect.h"
#include "engine/particle/particle_emitter.h"

namespace fate {

struct ParticleEmitterComponent {
    FATE_COMPONENT(ParticleEmitterComponent)

    ParticleEmitter emitter;
    bool autoDestroy = false;
};

} // namespace fate

FATE_REFLECT_EMPTY(fate::ParticleEmitterComponent)
