#include "engine/particle/particle_emitter.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace fate {

float ParticleEmitter::randomRange(float min, float max) {
    float t = static_cast<float>(rand()) / RAND_MAX;
    return min + t * (max - min);
}

void ParticleEmitter::init(const EmitterConfig& config, size_t maxParticles) {
    config_ = config;
    maxParticles_ = maxParticles;
    particles_.resize(maxParticles);
    vertices_.reserve(maxParticles * 4);
    activeCount_ = 0;
    spawnAccumulator_ = 0.0f;
    pendingBurst_ = 0;
}

void ParticleEmitter::spawn(const Vec2& pos) {
    if (activeCount_ >= maxParticles_) return;

    auto& p = particles_[activeCount_];
    p.position = pos;
    p.velocity = {randomRange(config_.velocityMin.x, config_.velocityMax.x),
                  randomRange(config_.velocityMin.y, config_.velocityMax.y)};
    p.color = config_.colorStart;
    p.colorEnd = config_.colorEnd;
    p.size = randomRange(config_.sizeMin, config_.sizeMax);
    p.sizeEnd = p.size * 0.5f;
    p.life = randomRange(config_.lifetimeMin, config_.lifetimeMax);
    p.maxLife = p.life;
    p.rotation = 0.0f;
    p.rotationSpeed = randomRange(config_.rotationSpeedMin, config_.rotationSpeedMax);
    activeCount_++;
}

void ParticleEmitter::update(float dt, const Vec2& emitterPos) {
    // Process pending burst
    if (pendingBurst_ > 0) {
        for (int i = 0; i < pendingBurst_; ++i) {
            spawn(burstPosition_);
        }
        pendingBurst_ = 0;
    }

    // Continuous spawning
    if (config_.spawnRate > 0.0f) {
        spawnAccumulator_ += dt * config_.spawnRate;
        while (spawnAccumulator_ >= 1.0f && activeCount_ < maxParticles_) {
            spawn(emitterPos);
            spawnAccumulator_ -= 1.0f;
        }
    }

    // Update existing particles
    for (size_t i = 0; i < activeCount_;) {
        auto& p = particles_[i];
        p.life -= dt;
        if (p.life <= 0.0f) {
            // Swap with last active particle
            particles_[i] = particles_[activeCount_ - 1];
            activeCount_--;
            continue;
        }

        p.velocity.x += config_.gravity.x * dt;
        p.velocity.y += config_.gravity.y * dt;
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.rotation += p.rotationSpeed * dt;
        ++i;
    }

    buildVertices(emitterPos);
}

void ParticleEmitter::burst(const Vec2& position, int count) {
    int n = (count < 0) ? config_.burstCount : count;
    if (n <= 0) return;
    pendingBurst_ = n;
    burstPosition_ = position;
}

bool ParticleEmitter::isFinished() const {
    // Only finished for burst emitters (no continuous spawn) with all particles dead
    return config_.spawnRate <= 0.0f && config_.burstCount > 0 && activeCount_ == 0 && pendingBurst_ == 0;
}

void ParticleEmitter::buildVertices(const Vec2& emitterPos) {
    vertices_.clear();
    for (size_t i = 0; i < activeCount_; ++i) {
        auto& p = particles_[i];
        float t = 1.0f - (p.life / p.maxLife); // 0 at birth, 1 at death

        // Lerp color and size
        Color c;
        c.r = p.color.r + (p.colorEnd.r - p.color.r) * t;
        c.g = p.color.g + (p.colorEnd.g - p.color.g) * t;
        c.b = p.color.b + (p.colorEnd.b - p.color.b) * t;
        c.a = p.color.a + (p.colorEnd.a - p.color.a) * t;

        float size = p.size + (p.sizeEnd - p.size) * t;
        float half = size * 0.5f;

        Vec2 pos = p.position;
        if (!config_.worldSpace) {
            pos.x += emitterPos.x;
            pos.y += emitterPos.y;
        }

        // Generate 4 vertices for this particle quad
        // Simple axis-aligned (rotation applied via cos/sin if rotationSpeed != 0)
        float cosR = std::cos(p.rotation);
        float sinR = std::sin(p.rotation);

        auto rotatePoint = [&](float lx, float ly) -> std::pair<float, float> {
            return {pos.x + lx * cosR - ly * sinR,
                    pos.y + lx * sinR + ly * cosR};
        };

        auto [x0, y0] = rotatePoint(-half, -half);
        auto [x1, y1] = rotatePoint( half, -half);
        auto [x2, y2] = rotatePoint( half,  half);
        auto [x3, y3] = rotatePoint(-half,  half);

        vertices_.push_back({x0, y0, 0.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f});
        vertices_.push_back({x1, y1, 1.0f, 0.0f, c.r, c.g, c.b, c.a, 0.0f});
        vertices_.push_back({x2, y2, 1.0f, 1.0f, c.r, c.g, c.b, c.a, 0.0f});
        vertices_.push_back({x3, y3, 0.0f, 1.0f, c.r, c.g, c.b, c.a, 0.0f});
    }
}

} // namespace fate
