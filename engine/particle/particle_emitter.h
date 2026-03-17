#pragma once
#include "engine/particle/particle.h"
#include "engine/render/sprite_batch.h"
#include <vector>

namespace fate {

class ParticleEmitter {
public:
    void init(const EmitterConfig& config, size_t maxParticles = 256);

    void update(float dt, const Vec2& emitterPos);
    void burst(const Vec2& position, int count = -1);

    const std::vector<SpriteVertex>& vertices() const { return vertices_; }
    size_t activeCount() const { return activeCount_; }

    EmitterConfig& config() { return config_; }
    const EmitterConfig& config() const { return config_; }
    bool isFinished() const;

private:
    EmitterConfig config_;
    std::vector<Particle> particles_;
    std::vector<SpriteVertex> vertices_;
    size_t maxParticles_ = 256;
    size_t activeCount_ = 0;
    float spawnAccumulator_ = 0.0f;
    int pendingBurst_ = 0;
    Vec2 burstPosition_;

    void spawn(const Vec2& pos);
    void buildVertices(const Vec2& emitterPos);
    static float randomRange(float min, float max);
};

} // namespace fate
