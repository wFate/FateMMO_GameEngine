#include "engine/particle/particle_system.h"
#include "engine/particle/particle_emitter_component.h"
#ifdef FATE_HAS_GAME
#include "engine/components/transform.h"
#endif // FATE_HAS_GAME

namespace fate {

void ParticleSystem::update(float dt) {
    if (!world_) return;

#ifdef FATE_HAS_GAME
    world_->forEach<ParticleEmitterComponent, Transform>(
        [&](Entity*, ParticleEmitterComponent* emitterComp, Transform* transform) {
            emitterComp->emitter.update(dt, transform->position);
        }
    );
#endif // FATE_HAS_GAME
}

} // namespace fate
