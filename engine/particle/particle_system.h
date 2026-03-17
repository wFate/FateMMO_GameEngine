#pragma once
#include "engine/ecs/world.h" // System base class defined here

namespace fate {

class ParticleSystem : public System {
public:
    const char* name() const override { return "ParticleSystem"; }
    void update(float dt) override;
};

} // namespace fate
