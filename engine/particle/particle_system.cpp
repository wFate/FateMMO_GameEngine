#include "engine/particle/particle_system.h"
#include "engine/particle/particle_emitter_component.h"
#include "game/components/transform.h"

namespace fate {

void ParticleSystem::update(float dt) {
    if (!world_) return;

    world_->forEach<ParticleEmitterComponent, Transform>(
        [&](Entity*, ParticleEmitterComponent* emitterComp, Transform* transform) {
            emitterComp->emitter.update(dt, transform->position);
        }
    );
}

} // namespace fate
