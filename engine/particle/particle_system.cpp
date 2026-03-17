#include "engine/particle/particle_system.h"

namespace fate {

void ParticleSystem::update(float dt) {
    // Particle emitter updates happen via forEach<ParticleEmitterComponent, Transform>
    // The game layer registers this system and provides the component iteration
    // in game_app.cpp. This base implementation is a hook point for the ECS system loop.
    // Actual particle update logic goes in the game's onUpdate or in a registered
    // render pass callback that calls emitter.update() for each entity.
}

} // namespace fate
